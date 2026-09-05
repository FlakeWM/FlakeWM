/*
 * Copyright (C) 2026 CharOfString <root@charofstring.cc>
 *
 * This file is part of FlakeWM.
 *
 * FlakeWM is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * FlakeWM is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * FlakeWM. If not, see <https://www.gnu.org/licenses/>.
 * ----------------------------------------------------------------------------
 * Referenced:
 *   - waylib/examples/outputviewport/Main.qml: composition of
 *     OutputRenderWindow, OutputItem, OutputViewport, cursorDelegate and
 *     OutputLayer.
 *   - waylib/examples/tinywl/Main.qml: the overall approach of hosting shell
 *     surfaces in a compositing scene.
 * ----------------------------------------------------------------------------
 * Defines FlakeWM's root compositing scene: it maps the logical output layout
 * into a render window, creates a viewport and cursor for every Wayland output,
 * draws the desktop background, and presents xdg-toplevel and popup surfaces.
 * ----------------------------------------------------------------------------
 * The current horizontal output layout, background style and minimal window
 * policy are FlakeWM's own implementation.
 */

import QtQuick
import Waylib.Server
import FlakeWM

Item {
  id: root

  // Waylib uses this window to render the Qt Quick scene graph to each
  // wlroots output; it is not a regular desktop application window. Its size
  // follows the entire logical output layout so that global coordinates can
  // be used uniformly.
  OutputRenderWindow {
    id: renderWindow

    width: Compositor.outputLayout.implicitWidth
    height: Compositor.outputLayout.implicitHeight
    color: "#17191f"

    Item {
      id: scene

      width: Compositor.outputLayout.implicitWidth
      height: Compositor.outputLayout.implicitHeight

      DynamicCreatorComponent {
        // Each WOutput discovered by C++ creates an OutputItem via
        // Compositor.outputs. The injected property name
        // waylandOutput must match compositor.cc exactly.
        creator: Compositor.outputs

        OutputItem {
          id: outputItem

          required property WaylandOutput waylandOutput

          output: waylandOutput
          devicePixelRatio: waylandOutput.scale
          layout: Compositor.outputLayout

          // References the cursor delegate from outputviewport/Main.qml:
          // the global cursor position is first mapped to this output's
          // local coordinates, then the hotspot is subtracted.
          // OutputLayer.Cursor lets Waylib promote it to a hardware
          // cursor plane when available, otherwise it falls back to
          // software compositing.
          cursorDelegate: Cursor {
            required property QtObject outputCursor

            readonly property point localPosition: parent.mapFromGlobal(
              Compositor.cursor.position.x,
              Compositor.cursor.position.y)

            cursor: outputCursor.cursor
            output: outputCursor.output.output
            x: localPosition.x - hotSpot.x
            y: localPosition.y - hotSpot.y
            visible: valid && outputCursor.visible

            OutputLayer.enabled: true
            OutputLayer.keepLayer: true
            OutputLayer.flags: OutputLayer.Cursor
            OutputLayer.cursorHotSpot: hotSpot
            OutputLayer.outputs: [viewport]
          }

          OutputViewport {
            id: viewport

            // The viewport is the render boundary between the scene
            // graph and a physical WOutput. C++ only commits the
            // enabled output state after receiving its initialized
            // signal.
            output: waylandOutput
            devicePixelRatio: outputItem.devicePixelRatio
            anchors.centerIn: parent
          }

          // FlakeWM's own placeholder background. Future workspace /
          // layer-shell scenes should be inserted above the background
          // and below the cursor layer, rather than letting clients
          // handle the desktop background.
          Rectangle {
            anchors.fill: parent
            color: "#17191f"

            Text {
              anchors.centerIn: parent
              color: "#d8dee9"
              font.pixelSize: 24
              text: "FlakeWM"
            }
          }
        }
      }

      DynamicCreatorComponent {
        // Each xdg_toplevel currently becomes a direct child of the
        // scene. This only proves the protocol and rendering pipeline
        // work; position, stacking, focus, move/resize still need
        // window-management policy to be complete.
        creator: Compositor.toplevels

        XdgToplevelSurfaceItem {
          required property WaylandXdgToplevelSurface waylandSurface

          shellSurface: waylandSurface
        }
      }

      DynamicCreatorComponent {
        // Popups need a dedicated item to handle xdg_popup geometry.
        // Currently still missing a complete positioning/constraint
        // strategy relative to the parent toplevel; not yet suitable
        // as a final daily-driver implementation.
        creator: Compositor.popups

        XdgPopupSurfaceItem {
          required property WaylandXdgPopupSurface waylandSurface

          shellSurface: waylandSurface
        }
      }
    }
  }
}
