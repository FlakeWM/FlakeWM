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
 * Originally copyright by (C) 2024-2026 UnionTech Software Technology Co., Ltd.
 * Original license: Apache-2.0 OR LGPL-3.0-only OR GPL-2.0-only OR
 *                   GPL-3.0-only.
 * Redistributed with GPL-3.0-only.
 * Original code is modified to adapt C++ and Wlroots 0.20.2.
 */

import QtQuick

Item {
  id: root

  property bool allowOutput: false
  property bool allowWindow: false
  property bool allowRegion: false
  property int selectedMode: 2
  property int hoveredMode: -1
  property int pressedMode: -1

  Rectangle {
    anchors.fill: parent
    color: "#d9222222"
    radius: 12
    border.width: 1
    border.color: "#33ffffff"
  }

  component ModeButton: Item {
    required property int buttonMode
    required property url iconSource

    width: visible ? 50 : 0
    height: 50

    Rectangle {
      anchors.fill: parent
      anchors.margins: 5
      radius: 8
      color: root.pressedMode === parent.buttonMode
               ? "#42ffffff"
               : root.selectedMode === parent.buttonMode
                   ? "#330088ff"
                   : root.hoveredMode === parent.buttonMode
                       ? "#24ffffff"
                       : "transparent"
      border.width: root.selectedMode === parent.buttonMode ? 1 : 0
      border.color: "#990088ff"
    }

    Image {
      anchors.centerIn: parent
      width: 30
      height: 30
      source: parent.iconSource
      sourceSize.width: width
      sourceSize.height: height
      opacity: root.selectedMode === parent.buttonMode ? 1.0 : 0.82
    }
  }

  Row {
    anchors.centerIn: parent

    ModeButton {
      buttonMode: 2
      iconSource: "qrc:/flakewm/icons/select_region.svg"
      visible: root.allowRegion
    }
    ModeButton {
      buttonMode: 1
      iconSource: "qrc:/flakewm/icons/select_window.svg"
      visible: root.allowWindow
    }
    ModeButton {
      buttonMode: 0
      iconSource: "qrc:/flakewm/icons/select_output.svg"
      visible: root.allowOutput
    }
  }
}
