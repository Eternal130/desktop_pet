import QtQuick
import QtQuick.Controls.Basic
import DesktopPet

// AppScrollBar — minimal Fluent-style scrollbar for Flickable/ListView.
ScrollBar {
    implicitWidth: 8
    contentItem: Rectangle {
        radius: 4
        color: Theme.withAlpha(Theme.textColor, parent.pressed ? 0.45 : 0.25)
    }
    background: null
}
