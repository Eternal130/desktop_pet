import QtQuick
import QtQuick.Controls
import DesktopPet

// Custom frameless-window titlebar.
//
// Layout: 32px-tall dark strip anchored to the top of the ApplicationWindow.
// Provides:
//   * Drag-to-move the window (DragHandler -> QWindow::startSystemMove)
//   * Minimize button (left-click -> QWindow::showMinimized)
//   * Close button    (left-click -> QWindow::close)
//
// Parent must be an ApplicationWindow. The containing window is resolved via
// the attached property `Window.window` (QtQuick.Window attached type).
Rectangle {
    id: root

    // Titlebar geometry: spans the full window width, sits at the top.
    anchors.left: parent.left
    anchors.right: parent.right
    anchors.top: parent.top
    height: 32

    // Catppuccin Mocha "mantle" by default (深紫梦幻) — one shade darker than
    // the window base for visual depth. Binds to Theme so swapping the palette
    // re-renders the titlebar instantly (T26).
    color: Theme.titleBarColor

    // ── Drag to move ──────────────────────────────────────────────────────
    // DragHandler with target:null is the modern Qt 6 idiom: it does not move
    // any item itself; we hook its activation to trigger the OS-native
    // system move. grabPermissions lets it take over from child button
    // MouseAreas once a drag threshold is crossed, so a click on a button
    // stays a click (no drag) but a click-drag on the bar moves the window.
    DragHandler {
        target: null
        grabPermissions: PointerHandler.CanTakeOverFromHandlersOfDifferentType
                       | PointerHandler.CanTakeOverFromAnything
        onActiveChanged: if (active) root.Window.window.startSystemMove()
    }

    // ── Title text ───────────────────────────────────────────────────────
    Text {
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        text: qsTr("Desktop Pet Controller (Qt)")
        color: Theme.textColor   // themed (T26)
        font.pixelSize: 13
        font.weight: Font.Medium
    }

    // ── Page navigation buttons (horizontally centered) ───────────────────
    // T24: top-level page switcher. Each button calls the host window's
    // switchPage(name), defined in Main.qml, which replaces the StackView's
    // top item. InstanceDetail is intentionally absent here — it is reached
    // from within Welcome (per-instance entry), not from global nav.
    Row {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        spacing: 0

        // Home → Welcome page
        Rectangle {
            width: 64
            height: root.height
            color: homeNavArea.containsMouse ? Theme.hoverColor : "transparent"
            Text {
                anchors.centerIn: parent
                text: qsTr("Home")
                color: Theme.textColor   // themed (T26)
                font.pixelSize: 12
            }
            MouseArea {
                id: homeNavArea
                anchors.fill: parent
                hoverEnabled: true
                onClicked: root.Window.window.switchPage("welcome")
            }
        }

        // Monitor → Monitor page
        Rectangle {
            width: 72
            height: root.height
            color: monitorNavArea.containsMouse ? Theme.hoverColor : "transparent"
            Text {
                anchors.centerIn: parent
                text: qsTr("Monitor")
                color: Theme.textColor
                font.pixelSize: 12
            }
            MouseArea {
                id: monitorNavArea
                anchors.fill: parent
                hoverEnabled: true
                onClicked: root.Window.window.switchPage("monitor")
            }
        }

        // Settings → Settings page
        Rectangle {
            width: 72
            height: root.height
            color: settingsNavArea.containsMouse ? Theme.hoverColor : "transparent"
            Text {
                anchors.centerIn: parent
                text: qsTr("Settings")
                color: Theme.textColor
                font.pixelSize: 12
            }
            MouseArea {
                id: settingsNavArea
                anchors.fill: parent
                hoverEnabled: true
                onClicked: root.Window.window.switchPage("settings")
            }
        }
    }

    // ── Window control buttons (right side) ──────────────────────────────
    Row {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        spacing: 0

        // (Theme switcher removed — single modern minimal theme; T28
        // persistence is a harmless no-op for unknown stored names.)

        // Minimize button
        Rectangle {
            width: 46
            height: root.height
            color: minimizeArea.containsMouse ? Theme.hoverColor : "transparent"

            Text {
                anchors.centerIn: parent
                text: "\u2014"   // em dash — a clean single-line minimize glyph
                color: Theme.textColor
                font.pixelSize: 14
            }
            MouseArea {
                id: minimizeArea
                anchors.fill: parent
                hoverEnabled: true
                onClicked: root.Window.window.showMinimized()
            }
        }

        // Close button — turns red on hover (destructive-action cue)
        Rectangle {
            width: 46
            height: root.height
            color: closeArea.containsMouse ? Theme.closeHoverColor : "transparent"

            Text {
                anchors.centerIn: parent
                text: "\u2715"   // ✕ heavy multiplication X
                // Invert contrast when the close button turns red on hover:
                // bg-colored glyph on the close-hover surface.
                color: closeArea.containsMouse ? Theme.bgColor : Theme.textColor
                font.pixelSize: 13
            }
            MouseArea {
                id: closeArea
                anchors.fill: parent
                hoverEnabled: true
                onClicked: root.Window.window.close()
            }
        }
    }
}
