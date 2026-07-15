import QtQuick
import QtQuick.Controls
import DesktopPet

// Frameless main window with custom titlebar + 8-direction edge resize.
//
// T23 (Phase 4.1): the OS chrome is removed via Qt.FramelessWindowHint; the
// custom titlebar (TitleBar.qml) provides drag-to-move + minimize/close, and
// ResizeHandles.qml adds the four edges + four corners.
//
// T24 (Phase 4.2): the content area is a StackView that swaps between the four
// placeholder pages (Welcome / InstanceDetail / Settings / Monitor). TitleBar
// nav buttons call root.switchPage(name), which delegates to pageStack.
//
// z-order: ResizeHandles (z=2) sits above TitleBar (z=1) so the 6px top edge
// strip takes precedence over the titlebar drag in the y=0..6 overlap zone;
// the rest of the titlebar (y=6..32) drags the window. StackView sits at z=0
// (default) below both, anchored below the 32px titlebar.
ApplicationWindow {
    id: root
    width: 1200
    height: 760
    visible: true
    title: qsTr("Desktop Pet Controller (Qt)")
    flags: Qt.FramelessWindowHint | Qt.Window
    color: "#1e1e2e"   // Catppuccin Mocha "base" — dark window background

    // ── Page switching entry point (called by TitleBar nav buttons) ─────────
    // Maps a page name ("welcome" | "instance" | "settings" | "monitor") to
    // its Component and replaces the top of pageStack. Using replace (not
    // push) avoids an unbounded back-stack for top-level nav.
    function switchPage(name) {
        let target = null
        if (name === "welcome")        target = welcomePageComp
        else if (name === "instance")  target = instanceDetailPageComp
        else if (name === "settings")  target = settingsPageComp
        else if (name === "monitor")   target = monitorPageComp
        if (target !== null) {
            pageStack.replace(target)
        }
    }

    // Page area: fills the window below the 32px titlebar.
    StackView {
        id: pageStack
        anchors.fill: parent
        anchors.topMargin: 32   // clear the custom titlebar

        initialItem: welcomePageComp

        // Page components. Declared as children of StackView so they share
        // its scope; replace() takes the Component as the new top item.
        Component { id: welcomePageComp;          WelcomePage {} }
        Component { id: instanceDetailPageComp;   InstanceDetailPage {} }
        Component { id: settingsPageComp;         SettingsPage {} }
        Component { id: monitorPageComp;          MonitorPage {} }
    }

    // Custom titlebar at the top.
    TitleBar {
        z: 1
    }

    // Eight-direction edge + corner resize handles, layered above the
    // titlebar so the top edge strip wins in the overlap zone.
    ResizeHandles {
        z: 2
    }
}
