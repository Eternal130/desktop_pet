import QtQuick
import DesktopPet

// Chip — pill-shaped tag for motions / expressions / behavior mappings.
// P2-11: unchecked chips are ghost pills (transparent at rest) with a
// text-alpha hover wash; the checked accent tint/underline-free style is
// unchanged.
Rectangle {
    id: root

    property string text: ""
    property bool checked: false
    signal activated()

    height: 26
    width: label.implicitWidth + 24
    radius: height / 2
    color: checked ? Qt.alpha(Theme.accentColor, 0.14)
                   : (_chipHover.containsMouse ? Qt.alpha(Theme.textColor, 0.06)
                                               : "transparent")

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        color: root.checked ? Theme.accentColor : Theme.text2Color
        font.pixelSize: 12
        font.weight: root.checked ? Font.DemiBold : Font.Normal
    }

    MouseArea {
        id: _chipHover
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.activated()
    }
}
