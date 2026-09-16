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
 * Adapted from GXDE-KWin, originally licensed under GPLv2.
 * Now re-licensed under GPLv3.
 */

import QtQuick

Item {
    id: root
    property int hoveredItem: -1
    property int pressedItem: -1
    property bool pointerInside: false

    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: 10
        color: root.pointerInside ? "#ffffff" : "transparent"
        border.width: 1
        border.color: Qt.rgba(0, 0, 0, 0.10)
        Behavior on color {
            ColorAnimation { duration: 90; easing.type: Easing.OutCubic }
        }
    }

    component TileItem: Rectangle {
        required property int itemIndex
        radius: 4
        color: root.pressedItem === itemIndex ? "#b9b9b9"
             : root.hoveredItem === itemIndex ? "#dcdcdc"
                                               : "#e6e6e6"
        Behavior on color {
            ColorAnimation { duration: 90; easing.type: Easing.OutCubic }
        }
    }

    Rectangle {
        x: 8; y: 8; width: 90; height: 64
        radius: 6
        color: "#f6f6f6"
        border.width: 1
        border.color: Qt.rgba(0, 0, 0, 0.10)

        TileItem { x: 5; y: 5; width: 40; height: 56; itemIndex: 0 }
        TileItem { x: 47; y: 5; width: 40; height: 56; itemIndex: 1 }
    }

    Rectangle {
        x: 106; y: 8; width: 90; height: 64
        radius: 6
        color: "#f6f6f6"
        border.width: 1
        border.color: Qt.rgba(0, 0, 0, 0.10)

        TileItem { x: 5; y: 5; width: 40; height: 27; itemIndex: 2 }
        TileItem { x: 5; y: 34; width: 40; height: 27; itemIndex: 3 }
        TileItem { x: 47; y: 5; width: 40; height: 27; itemIndex: 4 }
        TileItem { x: 47; y: 34; width: 40; height: 27; itemIndex: 5 }
    }
}
