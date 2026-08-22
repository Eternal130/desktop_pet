import QtQuick
import DesktopPet

// Chip — pill-shaped tag for motions / expressions / behavior mappings.
Rectangle {
    id: root

    property string text: ""
    property bool checked: false
    signal activated()

    height: 26
    width: label.implicitWidth + 24
    radius: height / 2
    color: checked ? Theme.accentAlpha(0.14)
                   : Theme.withAlpha(Theme.textColor, 0.05)

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        color: root.checked ? Theme.accentColor : Theme.text2Color
        font.pixelSize: 12
        font.weight: root.checked ? Font.DemiBold : Font.Normal
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.activated()
    }
}
