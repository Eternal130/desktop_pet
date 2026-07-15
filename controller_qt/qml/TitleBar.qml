import QtQuick

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

    // Catppuccin Mocha "mantle" — one shade darker than the window base
    // (#1e1e2e) to give the titlebar visual depth.
    color: "#181825"

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
        color: "#cdd6f4"   // Catppuccin Mocha "text"
        font.pixelSize: 13
        font.weight: Font.Medium
    }

    // ── Window control buttons (right side) ──────────────────────────────
    Row {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        spacing: 0

        // Minimize button
        Rectangle {
            width: 46
            height: root.height
            color: minimizeArea.containsMouse ? "#313244" : "transparent"

            Text {
                anchors.centerIn: parent
                text: "\u2014"   // em dash — a clean single-line minimize glyph
                color: "#cdd6f4"
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
            color: closeArea.containsMouse ? "#f38ba8" : "transparent"

            Text {
                anchors.centerIn: parent
                text: "\u2715"   // ✕ heavy multiplication X
                // Invert contrast when the close button turns red on hover.
                color: closeArea.containsMouse ? "#1e1e2e" : "#cdd6f4"
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
