import QtQuick
import QtQuick.Controls
import FluentUI
import DesktopPet

// SettingRow — Fluent settings-page row: label block on the left,
// arbitrary control on the right, hairline separator between rows.
//   SettingRow { title: "音量"; desc: "播放器总音量"; <control> }
// The trailing control should anchor to the right; this component only
// provides the row chrome + label column.
Item {
    id: root

    property string title: ""
    property string desc: ""

    default property alias _content: _slot.data

    // Reserve room for the control column (right-anchored slot).
    property real controlWidth: 0   // 0 = auto (control manages itself)

    readonly property real _rowPadV: 10
    readonly property real _labelRightPad: 16

    implicitHeight: Math.max(_labels.implicitHeight, _slot.implicitHeight) + 2 * _rowPadV

    // hairline between consecutive SettingRows inside a Column
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        visible: false   // separator drawn by parent via Separator below
        color: Theme.borderColor
    }

    Column {
        id: _labels
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: _slot.left
        anchors.rightMargin: _labelRightPad
        spacing: 2

        Text {
            width: parent.width
            text: root.title
            visible: root.title.length > 0
            color: Theme.textColor
            font: FluTextStyle.BodyStrong
        }
        Text {
            width: parent.width
            text: root.desc
            visible: root.desc.length > 0
            wrapMode: Text.WordWrap
            color: Theme.mutedTextColor
            font: FluTextStyle.Caption
        }
    }

    Item {
        id: _slot
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: controlWidth > 0 ? controlWidth : childrenRect.width
        height: childrenRect.height
    }
}
