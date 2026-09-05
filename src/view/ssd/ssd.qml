import QtQuick

Item {
  id: root

  property bool active: false
  property bool maximized: false
  property bool dialog: false
  property bool canMinimize: true
  property bool canMaximize: true
  property string title: ""
  property string appId: ""
  property int hoveredPart: 0
  property int pressedPart: 0
  readonly property int chromeInset: 0

  clip: true

  Rectangle {
    id: background

    width: parent.width
    height: parent.height + (root.maximized ? 0 : 7)
    color: "#ffffff"
    radius: root.maximized ? 0 : 7

    Behavior on radius {
      NumberAnimation {
        duration: 160
        easing.type: Easing.OutCubic
      }
    }
  }

  Image {
    anchors.left: parent.left
    anchors.leftMargin: root.chromeInset + 4
    y: root.chromeInset + 4
    width: 35
    height: 35
    source: root.appId.length > 0 ? "image://window-icons/" + root.appId : ""
    fillMode: Image.PreserveAspectFit
    smooth: true
    visible: status === Image.Ready
    opacity: root.active ? 1.0 : 0.65

    Behavior on opacity {
      NumberAnimation {
        duration: 160
        easing.type: Easing.OutCubic
      }
    }
  }

  Text {
    anchors.horizontalCenter: parent.horizontalCenter
    y: root.chromeInset
    width: Math.max(0, Math.min(implicitWidth, buttons.x * 2 - 16))
    height: 40
    text: root.title.length > 0 ? root.title : "FlakeWM"
    color: root.dialog ? "#000000" : (root.active ? "#303030" : "#969696")
    font.family: "Source Han Sans SC"
    font.pixelSize: 14
    horizontalAlignment: Text.AlignHCenter
    verticalAlignment: Text.AlignVCenter
    elide: Text.ElideRight

    Behavior on color {
      ColorAnimation {
        duration: 160
        easing.type: Easing.OutCubic
      }
    }
  }

  component WindowButton: Item {
    required property int part
    property bool hovered: root.hoveredPart === part
    property bool pressed: root.pressedPart === part
    property bool available: true
    property url normalSource
    property url hoverSource
    property url pressSource

    width: available ? 40 : 0
    height: 40
    opacity: available ? 1.0 : 0.0
    clip: true

    Image {
      anchors.fill: parent
      source: parent.normalSource
      opacity: !parent.hovered && !parent.pressed ? 1.0 : 0.0
    }
    Image {
      anchors.fill: parent
      source: parent.hoverSource
      opacity: parent.hovered && !parent.pressed ? 1.0 : 0.0
    }
    Image {
      anchors.fill: parent
      source: parent.pressSource
      opacity: parent.pressed ? 1.0 : 0.0
    }

    Behavior on width {
      NumberAnimation {
        duration: 150
        easing.type: Easing.OutCubic
      }
    }
    Behavior on opacity {
      NumberAnimation {
        duration: 110
        easing.type: Easing.OutCubic
      }
    }
  }

  Row {
    id: buttons

    anchors.right: parent.right
    anchors.rightMargin: root.chromeInset
    y: root.chromeInset
    height: 40

    WindowButton {
      id: minimizeButton

      part: 2
      available: root.canMinimize
      normalSource: "qrc:/flakewm/icons/minimize_normal.svg"
      hoverSource: "qrc:/flakewm/icons/minimize_hover.svg"
      pressSource: "qrc:/flakewm/icons/minimize_press.svg"
    }

    WindowButton {
      id: maximizeButton

      part: 3
      available: root.canMaximize
      normalSource: root.maximized ? "qrc:/flakewm/icons/unmaximize_normal.svg" : "qrc:/flakewm/icons/maximize_normal.svg"
      hoverSource: root.maximized ? "qrc:/flakewm/icons/unmaximize_hover.svg" : "qrc:/flakewm/icons/maximize_hover.svg"
      pressSource: root.maximized ? "qrc:/flakewm/icons/unmaximize_press.svg" : "qrc:/flakewm/icons/maximize_press.svg"
    }

    WindowButton {
      id: closeButton

      part: 4
      normalSource: "qrc:/flakewm/icons/close_normal.svg"
      hoverSource: "qrc:/flakewm/icons/close_hover.svg"
      pressSource: "qrc:/flakewm/icons/close_press.svg"
    }
  }
}
