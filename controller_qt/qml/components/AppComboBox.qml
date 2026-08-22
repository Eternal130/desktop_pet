import QtQuick
import QtQuick.Controls.Basic
import DesktopPet

// AppComboBox — bordered Fluent combo (design mock model/preset selector).
// Exposes currentIndex / currentText / find() + activated(int) like ComboBox.
ComboBox {
    id: root

    property color borderColor: Theme.dark ? "#ffffff49" : "#00000045"

    implicitHeight: 32
    font.pixelSize: 13

    background: Rectangle {
        radius: Theme.radiusMd
        color: root.popup.visible
            ? Theme.surfaceColor
            : (hover.hovered ? Theme.hoverColor : Theme.surfaceColor)
        border.width: 1
        border.color: root.borderColor
    }
    contentItem: Text {
        leftPadding: 10
        rightPadding: 26
        text: root.displayText
        color: Theme.textColor
        font: root.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Text {
        x: root.width - width - 10
        y: (root.height - height) / 2
        text: "▾"
        color: Theme.text3Color
        font.pixelSize: 12
    }
    delegate: ItemDelegate {
        id: dlg
        required property var model
        required property int index
        width: root.width
        height: 32
        readonly property string label: {
            if (typeof dlg.model === "string") return dlg.model
            if (dlg.model[root.textRole] !== undefined) return dlg.model[root.textRole]
            if (dlg.model.modelData !== undefined) return dlg.model.modelData
            return ""
        }
        contentItem: Text {
            leftPadding: 8
            text: dlg.label
            color: dlg.index === root.currentIndex ? Theme.accentColor : Theme.textColor
            font: root.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: 4
            color: dlg.hovered ? Theme.hoverColor : "transparent"
        }
        onClicked: {
            root.currentIndex = dlg.index
            root.popup.close()
            root.activated(dlg.index)
        }
    }
    popup: Popup {
        y: root.height + 4
        width: root.width
        padding: 6
        implicitHeight: Math.min(contentItem.implicitHeight + 12, 320)
        background: Rectangle {
            radius: Theme.radiusMd
            color: Theme.surfaceColor
            border.width: 1
            border.color: Theme.borderColor
        }
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: root.popup.visible ? root.delegateModel : null
            currentIndex: root.highlightedIndex
            spacing: 2
            ScrollIndicator.vertical: ScrollIndicator {}
        }
    }

    HoverHandler { id: hover }
}
