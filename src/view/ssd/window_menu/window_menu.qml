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
 * Inspired by GXDE-Wlcom, originally licensed under GPLv3.
 * Now re-licensed under GPLv3.
 */

import QtQuick

Item {
    id: root
    property var menuItems: []
    property int hoveredIndex: -1
    property int pressedIndex: -1
    property bool darkMode: false

    readonly property int shadowMargin: 10
    readonly property int contentWidth: 270
    readonly property int contentMargin: 12
    readonly property int itemHeight: 26

    // DTK's 10 px, 18% black shadow. These soft concentric masks avoid a
    // QtGraphicalEffects dependency; the actual backdrop blur is compositor
    // side and is clipped to the rounded menu surface.
    Repeater {
        model: 6
        Rectangle {
            required property int index
            x: root.shadowMargin - (5 - index)
            y: root.shadowMargin + 2 - (5 - index)
            width: root.contentWidth + 2 * (5 - index)
            height: root.contentMargin * 2 + root.itemHeight * 9
                    + 2 * (5 - index)
            radius: 8 + (5 - index)
            color: "transparent"
            border.width: 2
            border.color: Qt.rgba(0, 0, 0, 0.012 + index * 0.003)
        }
    }

    Rectangle {
        id: panel
        x: root.shadowMargin
        y: root.shadowMargin
        width: root.contentWidth
        height: root.contentMargin * 2 + root.itemHeight * 9
        radius: 8
        color: root.darkMode ? Qt.rgba(0.124, 0.124, 0.124, 0.285)
                             : Qt.rgba(0.924, 0.924, 0.924, 0.2706)
        border.width: 1
        border.color: root.darkMode ? Qt.rgba(1, 1, 1, 0.12)
                                    : Qt.rgba(0, 0, 0, 0.14)

        Repeater {
            model: root.menuItems
            Item {
                required property var modelData
                required property int index
                x: 1
                y: root.contentMargin + index * root.itemHeight
                width: panel.width - 2
                height: root.itemHeight

                Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: 7
                    anchors.rightMargin: 7
                    radius: 4
                    visible: parent.modelData.enabled
                             && root.hoveredIndex === parent.index
                    color: root.darkMode ? "#024cca" : "#0081ff"
                    opacity: root.pressedIndex === parent.index ? 0.78 : 1.0
                }

                Text {
                    x: 14
                    width: 18
                    anchors.verticalCenter: parent.verticalCenter
                    text: parent.modelData.checkable
                          && parent.modelData.checked ? "✓" : ""
                    font.family: "Source Han Sans SC"
                    font.pixelSize: 14
                    color: root.hoveredIndex === parent.index
                           && parent.modelData.enabled
                           ? (root.darkMode ? "#f1f6ff" : "white")
                           : (root.darkMode ? "white" : "black")
                    opacity: parent.modelData.enabled ? 1.0 : 0.6
                }

                Text {
                    x: 36
                    width: parent.width - 50
                    anchors.verticalCenter: parent.verticalCenter
                    text: parent.modelData.text
                    elide: Text.ElideRight
                    font.family: "Source Han Sans SC"
                    font.pixelSize: 14
                    color: root.hoveredIndex === parent.index
                           && parent.modelData.enabled
                           ? (root.darkMode ? "#f1f6ff" : "white")
                           : (root.darkMode ? "white" : "black")
                    opacity: parent.modelData.enabled ? 1.0 : 0.6
                }
            }
        }
    }
}
