import QtQuick
import QtQuick.Controls
import FluentUI
import DesktopPet

// SectionCard — FluFrame card with a section header row.
// Optional `action` slot sits on the header's right (e.g. "重新检测" button);
// default slot is the card body.
FluFrame {
    id: root

    property string title: ""
    property string hint: ""
    default property alias _body: _bodyCol.data
    property alias actionItem: _actionSlot.data

    padding: 20

    implicitHeight: _col.implicitHeight + 2 * padding

    Column {
        id: _col
        width: parent.width
        spacing: 14

        Item {
            width: parent.width
            height: Math.max(_titleCol.implicitHeight,
                             _actionSlot.childrenRect.height)

            Column {
                id: _titleCol
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                Text {
                    text: root.title
                    visible: root.title.length > 0
                    color: Theme.accentColor
                    font: FluTextStyle.BodyStrong
                }
                Text {
                    text: root.hint
                    visible: root.hint.length > 0
                    color: Theme.mutedTextColor
                    font: FluTextStyle.Caption
                }
            }

            Item {
                id: _actionSlot
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: childrenRect.width
                height: childrenRect.height
            }
        }

        Column {
            id: _bodyCol
            width: parent.width
            spacing: 12
        }
    }
}
