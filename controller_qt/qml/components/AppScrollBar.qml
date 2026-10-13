import QtQuick
import QtQuick.Controls
import DesktopPet

// AppScrollBar — minimal Fluent-style scrollbar for Flickable/ListView.
// Follows the app Quick Controls style (was pinned to Basic); the contentItem
// + background overrides below keep the visual identical under FluentWinUI3.
ScrollBar {
    implicitWidth: 8
    contentItem: Rectangle {
        radius: 4
        color: Theme.withAlpha(Theme.textColor, parent.pressed ? 0.45 : 0.25)
    }
    background: null
}
