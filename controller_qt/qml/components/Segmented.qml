import QtQuick
import DesktopPet

// Segmented — design-mock .seg: bordered pill group, selected segment gets
// accent tint + bottom underline. options: [{label, value}], or [string]s.
Rectangle {
    id: root

    property var options: []            // strings or {label, value}
    property string currentValue: ""
    signal selected(string value)

    function labelAt(i) {
        return typeof options[i] === "string" ? options[i] : options[i].label
    }
    function valueAt(i) {
        return typeof options[i] === "string" ? options[i]
                : (options[i].value !== undefined ? options[i].value : options[i].label)
    }

    implicitWidth: _row.childrenRect.width + 2
    implicitHeight: 30
    radius: Theme.radiusMd
    color: "transparent"
    border.width: 1
    border.color: Theme.dark ? "#ffffff38" : "#00000038"

    Row {
        id: _row
        anchors.centerIn: parent
        Repeater {
            model: root.options.length
            delegate: Rectangle {
                readonly property bool sel: root.currentValue === root.valueAt(index)
                width: _segLabel.implicitWidth + 28
                height: root.height - 2
                color: sel ? Theme.accentAlpha(0.14) : "transparent"
                radius: Theme.radiusMd - 1
                Text {
                    id: _segLabel
                    anchors.centerIn: parent
                    text: root.labelAt(index)
                    color: sel ? Theme.accentColor : Theme.text2Color
                    font.pixelSize: 12
                    font.weight: sel ? Font.DemiBold : Font.Normal
                }
                Rectangle {
                    // selected underline (design mock inset box-shadow)
                    anchors.bottom: parent.bottom
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: parent.width - 16
                    height: 2
                    radius: 1
                    visible: sel
                    color: Theme.accentColor
                }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.selected(root.valueAt(index))
                }
            }
        }
    }
}
