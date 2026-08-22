import QtQuick
import DesktopPet

// Card — design-mock .card: white surface, 8px radius, hairline border,
// subtle resting shadow. Content is centered in a padded column.
Rectangle {
    id: root

    default property alias _data: _content.data

    property string title: ""
    property string hint: ""
    property alias actionItem: _actionSlot.data

    property real padding: Theme.spaceCard

    radius: Theme.radiusLg
    color: Theme.surfaceColor
    border.width: 1
    border.color: Theme.dark ? "#ffffff14" : Theme.hairlineColor

    implicitHeight: _col.implicitHeight + 2 * padding

    Column {
        id: _col
        x: root.padding
        y: root.padding
        width: parent.width - 2 * root.padding
        spacing: 12

        Item {
            width: parent.width
            height: Math.max(_titleCol.implicitHeight, _actionSlot.childrenRect.height)

            Column {
                id: _titleCol
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                Text {
                    text: root.title
                    visible: root.title.length > 0
                    color: Theme.textColor
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }
                Text {
                    text: root.hint
                    visible: root.hint.length > 0
                    color: Theme.text3Color
                    font.pixelSize: 11
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
            id: _content
            width: parent.width
            spacing: 12
        }
    }
}
