import QtQuick
import QtQuick.Controls

// Frameless main window with custom titlebar + 8-direction edge resize.
//
// T23 (Phase 4.1): the OS chrome is removed via Qt.FramelessWindowHint; the
// custom titlebar (TitleBar.qml) provides drag-to-move + minimize/close, and
// ResizeHandles.qml adds the four edges + four corners.
//
// z-order: ResizeHandles (z=2) sits above TitleBar (z=1) so the 6px top edge
// strip takes precedence over the titlebar drag in the y=0..6 overlap zone;
// the rest of the titlebar (y=6..32) drags the window.
ApplicationWindow {
    id: root
    width: 1200
    height: 760
    visible: true
    title: qsTr("Desktop Pet Controller (Qt)")
    flags: Qt.FramelessWindowHint | Qt.Window
    color: "#1e1e2e"   // Catppuccin Mocha "base" — dark window background

    // Custom titlebar at the top.
    TitleBar {
        z: 1
    }

    // Eight-direction edge + corner resize handles, layered above the
    // titlebar so the top edge strip wins in the overlap zone.
    ResizeHandles {
        z: 2
    }

    // Content area placeholder (Phase 5 fills this with the real UI).
    Text {
        anchors.centerIn: parent
        text: qsTr("Desktop Pet Controller (Qt)")
        color: "#cdd6f4"   // Catppuccin Mocha "text"
        font.pixelSize: 24
    }
}
