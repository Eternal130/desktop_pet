import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import DesktopPet

// Project-styled checkbox (visual QA punch list: default Basic checkbox was
// ~28px with a black checkmark — oversized and off-accent). 18px indicator,
// 3px radius, accent-colored checkmark, label vertically centered.
CheckBox {
    id: control

    indicator: Rectangle {
        implicitWidth: 18
        implicitHeight: 18
        x: control.leftPadding
        y: control.height / 2 - height / 2
        radius: 3
        color: control.checked ? Theme.accentColor : "transparent"
        border.color: control.checked ? Theme.accentColor : Theme.mutedTextColor
        border.width: 1

        Text {
            anchors.centerIn: parent
            text: "\u2713"
            color: "#ffffff"
            font.pixelSize: 12
            font.weight: Font.Bold
            visible: control.checked
        }
    }

    contentItem: Text {
        text: control.text
        color: Theme.textColor
        font.pixelSize: 13
        verticalAlignment: Text.AlignVCenter
        leftPadding: control.indicator.width + control.spacing
    }
}
