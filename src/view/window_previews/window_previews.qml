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
 * The file is adapted from GXDE KWin's Window Preview screen.
 */

import QtQuick

Item {
  id: root

  property var entries: []
  property string filterText: ""
  property real decalOpacity: 1.0

  Accessible.role: Accessible.Window
  Accessible.name: "window_previews"
  Accessible.description: "Super + A present windows"

  Repeater {
    model: root.entries

    delegate: Item {
      required property var modelData
      required property int index
      anchors.fill: parent

      Rectangle {
        x: modelData.x
        y: modelData.y
        width: modelData.width
        height: modelData.height
        color: "black"
        opacity: 0.60 * (1.0 - modelData.highlight)
      }

      Image {
        x: modelData.baseX + (modelData.baseWidth - width) / 2
        y: modelData.baseY + (modelData.baseHeight - height) / 2
        width: 64
        height: 64
        source: "image://window-preview-icons/" + encodeURIComponent(modelData.appId)
        sourceSize: Qt.size(64, 64)
        smooth: true
        opacity: 0.90 * 0.75 * modelData.opacity * root.decalOpacity
      }

      Text {
        x: modelData.baseX
        y: modelData.baseY + modelData.baseHeight / 2 + 64 - height / 2
        width: modelData.baseWidth
        text: modelData.title
        color: "white"
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideMiddle
        font.pointSize: 12
        opacity: 0.90 * modelData.opacity * root.decalOpacity
      }

      Rectangle {
        visible: modelData.showClose
        x: modelData.closeX
        y: modelData.closeY
        width: 32
        height: 32
        radius: 6
        color: "#e626292e"
        border.width: 1
        border.color: "#597b7c7f"

        Text {
          anchors.centerIn: parent
          text: "✕"
          color: "white"
          font.pointSize: 15
        }
      }
    }
  }

  Rectangle {
    visible: root.filterText.length > 0
    anchors.horizontalCenter: parent.horizontalCenter
    y: parent.height / 10
    width: Math.min(1024, Math.max(180, filterLabel.implicitWidth + 28))
    height: filterLabel.implicitHeight + 22
    color: "#d91a1a1a"
    radius: 8
    border.width: 1
    border.color: "#40808080"

    Text {
      id: filterLabel
      anchors.centerIn: parent
      width: Math.min(996, implicitWidth)
      text: "Filter:\n" + root.filterText
      color: "white"
      font.pointSize: 20
      elide: Text.ElideMiddle
    }
  }
}
