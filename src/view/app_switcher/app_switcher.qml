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
 * Adapted from GXDE-KWin, originally licensed under GPL-2.0.
 * Now re-licensed under GPLv3.
 */

import QtQuick

Item {
  id: root

  property var entries: []
  property int currentIndex: 0
  property int cellSize: 148

  Accessible.role: Accessible.Window
  Accessible.name: "tabbox_area"
  Accessible.description: "alt + tab switcher area"

  Rectangle {
    anchors.fill: parent
    color: "#33ffffff"
    radius: 6
    antialiasing: true
    border.width: 1
    border.color: "#19000000"

  }

  GridView {
    id: itemsView
    anchors.fill: parent
    anchors.margins: 32
    model: root.entries
    cellWidth: root.cellSize
    cellHeight: root.cellSize
    currentIndex: root.currentIndex
    interactive: false
    clip: true

    delegate: Item {
      required property var modelData
      required property int index
      width: itemsView.cellWidth
      height: itemsView.cellHeight

      Rectangle {
        anchors.fill: parent
        color: root.currentIndex === index ? "#01bdff" : "transparent"
        radius: 4
      }

      Rectangle {
        anchors.fill: parent
        anchors.margins: 10
        color: "transparent"

        Rectangle {
          anchors.fill: icon
          anchors.topMargin: 8
          color: "#32000000"
          radius: 12
        }

        Image {
          id: icon
          anchors.fill: parent
          source: modelData.appId.length > 0
                    ? "image://window-icons/" + modelData.appId : ""
          sourceSize.width: width
          sourceSize.height: height
          fillMode: Image.PreserveAspectFit
          smooth: true
          opacity: modelData.minimized ? 0.72 : 1.0
        }

        Rectangle {
          anchors.centerIn: parent
          width: Math.min(parent.width, parent.height) * 0.72
          height: width
          visible: icon.status !== Image.Ready
          color: "#d94a4a4a"
          radius: 14

          Text {
            anchors.centerIn: parent
            text: modelData.title.length > 0
                    ? modelData.title.charAt(0).toUpperCase() : "?"
            color: "white"
            font.pixelSize: parent.width * 0.42
            font.bold: true
          }
        }
      }

      Accessible.role: Accessible.Graphic
      Accessible.name: "Rect_tabbox_windowImage_" + modelData.title
      Accessible.description: modelData.title
    }
  }
}
