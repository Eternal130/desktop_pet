import QtQuick
import DesktopPet

// SettingRow — settings-page row: label block left, right-anchored control
// slot.  SettingRow { title; desc; <control> }
Item {
    id: root

    property string title: ""
    property string desc: ""

    default property alias _content: _slot.data

    readonly property real _rowPadV: 10

    implicitHeight: Math.max(_labels.implicitHeight, 32) + 2 * _rowPadV

    Column {
        id: _labels
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: _slot.left
        anchors.rightMargin: 16
        spacing: 2

        Text {
            width: parent.width
            text: root.title
            visible: root.title.length > 0
            color: Theme.textColor
            font.pixelSize: 13
            font.weight: Font.DemiBold
        }
        Text {
            width: parent.width
            text: root.desc
            visible: root.desc.length > 0
            wrapMode: Text.WordWrap
            color: Theme.text2Color
            font.pixelSize: 11
        }
    }

    Item {
        id: _slot
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: childrenRect.width
        // Binding-loop fix (same family as InstanceDetailPage's ParamRow):
        // childrenRect.height unions the children's Y positions, and the
        // vertically-anchored slot children (toggles, buttons) position
        // their y off THIS item's height — height ← childrenRect ← y ←
        // height. Derive the height from the children's own heights only
        // (no child's height depends on this item); identical value for
        // every current usage (children are v-centered or at y=0).
        height: {
            let h = 0
            for (let i = 0; i < children.length; ++i)
                h = Math.max(h, children[i].height)
            return h
        }
    }
}
