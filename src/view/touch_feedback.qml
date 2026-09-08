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
 * This file provides touchscreen UI.
 */

import QtQuick

Item {
  id: root

  property bool pressed: false
  property bool moved: false
  property int pulse: 0

  onPulseChanged: clickAnimation.restart()
  onPressedChanged: {
    if (pressed && !moved) {
      holdAnimation.restart()
    } else {
      resetHoldAnimation()
    }
  }
  onMovedChanged: {
    if (pressed && !moved) {
      holdAnimation.restart()
    } else {
      resetHoldAnimation()
    }
  }

  function resetHoldAnimation() {
    holdAnimation.stop()
    holdProgress.opacity = 0
    holdProgress.scale = 0.35
  }

  Rectangle {
    id: clickRipple

    anchors.centerIn: parent
    width: 80
    height: width
    radius: width / 2
    color: "transparent"
    border.width: 3
    border.color: "#d9ffffff"
    opacity: 0
    scale: 0.25
  }

  ParallelAnimation {
    id: clickAnimation

    NumberAnimation {
      target: clickRipple
      property: "scale"
      from: 0.25
      to: 1.0
      duration: 500
      easing.type: Easing.OutCubic
    }
    NumberAnimation {
      target: clickRipple
      property: "opacity"
      from: 0.8
      to: 0.0
      duration: 500
      easing.type: Easing.OutCubic
    }
  }

  Rectangle {
    id: holdProgress

    anchors.centerIn: parent
    width: 96
    height: width
    radius: width / 2
    color: "transparent"
    border.width: 4
    border.color: "#f2ffffff"
    opacity: 0
    scale: 0.35
  }

  SequentialAnimation {
    id: holdAnimation

    PauseAnimation {
      duration: 200
    }
    ParallelAnimation {
      NumberAnimation {
        target: holdProgress
        property: "scale"
        from: 0.35
        to: 1.0
        duration: 800
        easing.type: Easing.OutCubic
      }
      NumberAnimation {
        target: holdProgress
        property: "opacity"
        from: 0.15
        to: 0.85
        duration: 800
        easing.type: Easing.InOutCubic
      }
    }
  }

  Rectangle {
    anchors.centerIn: parent
    width: root.pressed ? 18 : 10
    height: width
    radius: width / 2
    color: "#e6ffffff"
    border.width: 1
    border.color: "#73000000"
    opacity: root.pressed ? 1.0 : 0.0

    Behavior on width {
      NumberAnimation {
        duration: 160
        easing.type: Easing.OutCubic
      }
    }
    Behavior on opacity {
      NumberAnimation {
        duration: 180
        easing.type: Easing.OutCubic
      }
    }
  }
}
