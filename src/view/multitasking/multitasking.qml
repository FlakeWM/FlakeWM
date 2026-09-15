/*
 * Copyright (C) 2026 CharOfString <root@charofstring.cc>
 *
 * This file is part of FLAKEWM.
 *
 * FLAKEWM is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * FLAKEWM is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * FLAKEWM. If not, see <https://www.gnu.org/licenses/>.
 * ----------------------------------------------------------------------------
 * The file is adapted from GXDE KWin's Multitasking screen.
 */

import QtQuick

Item {
  id: root

  property var workspaces: []
  property var entries: []
  property bool canAddWorkspace: true
  property int currentWorkspace: 0
  property int selectedWorkspace: 0
  property int selectedWindow: -1
  property int hoveredWorkspace: -1
  property int hoveredWindow: -1
  property real workspaceOpacity: 1.0
  property int workspaceX: 0
  property int workspaceY: 0
  property int workspaceWidth: 240
  property int workspaceHeight: 135
  property int workspaceGap: 40
  property int windowAreaY: 0
  property int addX: 0
  property int addY: 0
  property int addSize: 64
  property int draggedWorkspace: -1
  property int draggedWorkspaceX: 0

  readonly property color activeColor: "#0081ff"

  Accessible.role: Accessible.Window
  Accessible.name: "multitasking_view"
  Accessible.description: "Super + S workspace and window overview"

  Repeater {
    model: root.workspaces

    delegate: Item {
      required property var modelData
      required property int index

      x: root.draggedWorkspace === index
         ? root.draggedWorkspaceX
         : root.workspaceX + index * (root.workspaceWidth + root.workspaceGap)
      y: root.workspaceY
      width: root.workspaceWidth
      height: root.workspaceHeight
      opacity: root.workspaceOpacity

      Rectangle {
        anchors.fill: parent
        color: "transparent"
        radius: 8
        border.width: root.currentWorkspace === index ? 3 : 1
        border.color: root.currentWorkspace === index
                      ? root.activeColor : "#33ffffff"
      }

      Image {
        visible: root.hoveredWorkspace === index && root.workspaces.length > 1
        x: parent.width - 30
        y: -13
        width: 48
        height: 48
        source: "qrc:/flakewm/icons/multiview_delete.svg"
        sourceSize: Qt.size(48, 48)
        smooth: true
      }
    }
  }

  Image {
    visible: root.canAddWorkspace
    opacity: root.workspaceOpacity
    x: root.addX
    y: root.addY
    width: root.addSize
    height: root.addSize
    source: "qrc:/flakewm/icons/add-light.svg"
    sourceSize: Qt.size(root.addSize, root.addSize)
    smooth: true
  }

  Repeater {
    model: root.entries

    delegate: Item {
      required property var modelData
      required property int index

      x: modelData.x - 3
      y: modelData.y - 3
      width: modelData.width + 6
      height: modelData.height + 6

      Rectangle {
        anchors.fill: parent
        radius: 18
        color: "transparent"
        border.width: 3
        border.color: root.hoveredWindow === index
                      ? "#ff000000" : "#33000000"
      }

      Image {
        visible: root.hoveredWindow === index
        x: parent.width - 28
        y: -14
        width: 48
        height: 48
        source: "qrc:/flakewm/icons/multiview_delete.svg"
        sourceSize: Qt.size(48, 48)
        smooth: true
      }

      Image {
        visible: root.hoveredWindow === index
        x: -19
        y: -14
        width: 48
        height: 48
        source: modelData.keptAbove
                ? "qrc:/flakewm/icons/multiview_top_active.svg"
                : "qrc:/flakewm/icons/multiview_top.svg"
        sourceSize: Qt.size(48, 48)
        smooth: true
      }
    }
  }
}
