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
 */

import QtQuick

Item {
    id: root
    property string hintText: ""

    Rectangle {
        anchors.fill: parent
        radius: 6
        color: Qt.rgba(0.94, 0.94, 0.94, 0.72)
        border.width: 1
        border.color: Qt.rgba(0, 0, 0, 0.16)

        Text {
            anchors.centerIn: parent
            text: root.hintText
            color: "#303030"
            font.family: "Source Han Sans SC"
            font.pixelSize: 14
        }
    }
}
