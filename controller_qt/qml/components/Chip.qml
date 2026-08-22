import QtQuick
import QtQuick.Controls
import FluentUI
import DesktopPet

// Chip — pill-shaped tag for motions / expressions / behavior mappings.
// Selectable variant (`checkable`) mirrors the old FluToggleButton flow
// but with the compact chip silhouette from the redesign mockup.
Rectangle {
    id: root

    property string text: ""
    property bool checked: false
    signal activated()

    height: 26
    width: label.implicitWidth + 24
    radius: height / 2
    color: checked
        ? Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.14)
        : Qt.rgba(Theme.textColor.r, Theme.textColor.g, Theme.textColor.b, 0.05)

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        color: root.checked ? Theme.accentColor : Theme.mutedTextColor
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
