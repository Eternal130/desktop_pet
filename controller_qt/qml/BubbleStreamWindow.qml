import QtQuick
import QtQuick.Window
import DesktopPet

// BubbleStreamWindow — the desktop-level notification bubble stream. A
// frameless always-on-top TOOL window (separate native window from Main; a
// Window child of a Window creates its own native window sharing the
// engine), transparent, anchored at the screen's top-right corner with a
// 24px margin. Bubbles stack top-down, newest first (row 0 = top). Plain
// QtQuick only — no FluentUI imports (must not pull FluWindow chrome into
// an always-on-top overlay). Visible only while bubbles exist AND the
// stream is enabled.
Window {
    id: streamRoot

    width: 340
    height: Math.min(contentColumn.implicitHeight + 16, 480)
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool
           | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    visible: notificationStream.count > 0 && notificationStream.enabled

    x: Screen.desktopAvailableWidth - streamRoot.width - 24
    y: 24

    Column {
        id: contentColumn
        x: 0
        y: 8
        width: parent.width
        spacing: 10

        Repeater {
            model: notificationStream.model

            // Roles avatar/name/text inject automatically into BubbleCard's
            // same-name required properties (Qt injects model roles into
            // required properties; the `model` context object is DISABLED
            // in delegates that declare required properties).
            delegate: BubbleCard {
                required property int index

                width: contentColumn.width
                stackIndex: index
                onClicked: notificationStream.dismiss(index)
            }
        }
    }
}
