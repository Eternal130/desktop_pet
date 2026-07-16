import QtQuick
import QtQuick.Controls
import QtQuick.Window
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
// T25 (Phase 4.3): Sidebar (200px, instance-list shell) is anchored to the
// left edge below the titlebar; the StackView's leftMargin clears it so the
// page body fills only the remaining space.
//
// T28 (Phase 4.6): window geometry + theme are restored from PanelConfig on
// startup (initialX/Y/Width/Height/theme context properties set in main.cpp
// from PanelStateManager::load()) and persisted on close + theme change via
// the windowStateSaver context property (WindowStateSaver Q_INVOKABLE).
//
// z-order: ResizeHandles (z=2) sits above TitleBar (z=1) / Sidebar (z=1) so
// the 6px top edge strip takes precedence over the titlebar drag in the y=0..6
// overlap zone; the rest of the titlebar (y=6..32) drags the window. Sidebar
// shares z=1 with TitleBar but they never overlap (titlebar y=0..32, sidebar
// starts at y=32). StackView sits at z=0 (default) below all three.
ApplicationWindow {
    id: root
    // T28: restore geometry from PanelConfig. initialX/Y/W/H are QML context
    // properties set in main.cpp from PanelStateManager::load(). panelX/Y of -1
    // means "center on screen" (PanelConfig.hpp); the centered coordinate is
    // derived from Screen.width/height. Once TitleBar drag or ResizeHandles
    // assigns x/y/width/height, QML breaks these bindings, so subsequent moves
    // / resizes do NOT re-evaluate the centering expression.
    width: initialWidth > 0 ? initialWidth : 1200
    height: initialHeight > 0 ? initialHeight : 760
    x: initialX >= 0 ? initialX : (Screen.width - width) / 2
    y: initialY >= 0 ? initialY : (Screen.height - height) / 2
    visible: true
    title: qsTr("Desktop Pet Controller (Qt)")
    flags: Qt.FramelessWindowHint | Qt.Window
    // Window background binds to the global Theme singleton (T26); swapping the
    // theme re-renders this instantly. Theme is registered under DesktopPet.
    color: Theme.bgColor

    // T28: guard so the themeChanged Connections handler below does NOT fire
    // during the initial Theme.setTheme(initialTheme) application (that would
    // redundantly re-write panel.json with the values we just read from it).
    // Flipped to false at the end of Component.onCompleted.
    property bool _initializingTheme: true

    // T28: apply the persisted theme once the window is fully constructed.
    // Theme.setTheme is a guarded no-op for unknown names or no-op transitions,
    // so a bogus / default initialTheme is harmless.
    Component.onCompleted: {
        Theme.setTheme(initialTheme)
        _initializingTheme = false
    }

    // T28: persist geometry + theme when the window closes. This is the primary
    // save point (matches the task spec "Persist on close/move/resize"). The
    // themeChanged Connections block below persists theme switches immediately,
    // so a crash between a theme switch and close still preserves the choice.
    onClosing: {
        windowStateSaver.saveWindowState(root.x, root.y, root.width,
                                         root.height, Theme.currentTheme)
    }

    // T28: persist theme changes immediately. Theme switches are rare,
    // user-initiated events (TitleBar dropdown), so one atomic write per switch
    // is cheap and crash-safe — no debounce needed. Guarded by
    // _initializingTheme to skip the redundant save from Component.onCompleted.
    Connections {
        target: Theme
        function onThemeChanged(name) {
            if (!_initializingTheme) {
                windowStateSaver.saveWindowState(root.x, root.y, root.width,
                                                 root.height, name)
            }
        }
    }

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

    // Page area: fills the window to the right of the sidebar, below the
    // 32px titlebar. leftMargin clears the 200px Sidebar (T25).
    StackView {
        id: pageStack
        anchors.fill: parent
        anchors.topMargin: 32   // clear the custom titlebar
        anchors.leftMargin: 200 // clear the sidebar (T25)

        initialItem: welcomePageComp

        // Page components. Declared as children of StackView so they share
        // its scope; replace() takes the Component as the new top item.
        Component { id: welcomePageComp;          WelcomePage {} }
        Component { id: instanceDetailPageComp;   InstanceDetailPage {} }
        Component { id: settingsPageComp;         SettingsPage {} }
        Component { id: monitorPageComp;          MonitorPage {} }
    }

    // Left sidebar — instance list shell (T25). Empty model for now; Phase 5
    // wires the real instance CRUD flow.
    Sidebar {
        z: 1
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
