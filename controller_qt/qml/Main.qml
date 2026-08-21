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
        // T24 cold-start measurement: log the wall-clock delta between main()
        // entry (the coldStartT0Ms context property set in main.cpp before
        // QGuiApplication construction) and the first-frame QML completion.
        // console.log routes through Qt's message handler bridge into the
        // spdlog rotating-file sink (Logging.cpp qtMessageHandler), and
        // flush_on(info) makes the line appear on disk immediately so the
        // measurement script can grep it. Greppable tag: COLD_START_MS=.
        console.log("COLD_START_MS=" + (Date.now() - coldStartT0Ms))
        Theme.setTheme(initialTheme)
        _initializingTheme = false
    }

    // T28 + Wave 7 todo 15: persist geometry + theme when the window closes,
    // then branch on closeAction. The saveWindowState call is the primary save
    // point (matches the T28 spec "Persist on close/move/resize"). The
    // closeAction branch (added in todo 15) decides minimize-to-tray vs
    // confirm-then-exit, gated by confirmOnExit.
    //
    // closeAction is "exit" or "minimize" (B2 guard — NOT "hide_to_tray";
    // PanelConfig.hpp:48 uses "minimize"). panelConfig is the
    // PanelConfigController context property registered in main.cpp.
    onClosing: function(close) {
        // ALWAYS save window state first — both paths benefit from persisted
        // geometry + theme for the next launch.
        windowStateSaver.saveWindowState(root.x, root.y, root.width,
                                         root.height, Theme.currentTheme)
        if (panelConfig.closeAction === "minimize") {
            // closeAction=="minimize" → hide the window, tray icon (if any)
            // shows the app is still alive. setQuitOnLastWindowClosed(false)
            // (TrayManager ctor) guarantees the process does NOT quit when the
            // last window hides. close.accepted = false rejects the OS close
            // so the ApplicationWindow is not torn down (can be re-shown).
            root.hide()
            close.accepted = false
        } else {
            // closeAction=="exit" → run the exit path (optionally confirmed).
            // If confirmOnExit → show the exit dialog (default-focus Cancel)
            // + reject this close; the dialog's Ok button calls root.doExit()
            // which runs stopAll → saveState → tray.shutdown → Qt.quit. If
            // !confirmOnExit → doExit() immediately.
            if (panelConfig.confirmOnExit) {
                exitConfirmDialog.open()
                close.accepted = false
            } else {
                // No confirmation needed — accept the close (default) and
                // quit. doExit calls Qt.quit which posts a quit event; the
                // close event's acceptance is moot once the process exits.
                root.doExit()
            }
        }
    }

    // Wave 7 todo 15 — the unified exit path. Called from onClosing (no
    // confirm) + exitConfirmDialog (Ok clicked) + tray "退出" menu item.
    // Sequence: stopAll (graceful instance teardown, blocking ≤5s/instance) →
    // saveWindowState (final geometry flush) → tray.shutdown (hide icon
    // early for a clean visual transition) → Qt.quit (process exit). The
    // stopAll call blocks the GUI thread; the user has confirmed the exit.
    function doExit() {
        instanceManager.stopAll()
        windowStateSaver.saveWindowState(root.x, root.y, root.width,
                                         root.height, Theme.currentTheme)
        trayManager.shutdown()
        Qt.quit()
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
    //
    // "monitor" gets the currentInstance bound (Wave 8 todo 18) so
    // MonitorPage can read instance.monitorModel() for the live charts; null
    // when no sidebar row is selected (the page renders its empty state).
    // Current top-level page name ("welcome"|"monitor"|"settings") — drives
    // the TitleBar nav active indicator and is updated by switchPage().
    property string currentPage: "welcome"

    function switchPage(name) {
        let target = null
        if (name === "welcome")        target = welcomePageComp
        else if (name === "instance")  target = instanceDetailPageComp
        else if (name === "settings")  target = settingsPageComp
        else if (name === "monitor")   target = monitorPageComp
        if (target !== null) {
            root.currentPage = (name === "instance") ? "welcome" : name
            if (name === "monitor") {
                pageStack.replace(target, { instance: root.currentInstance })
            } else {
                pageStack.replace(target)
            }
        }
    }

    // ── Tray window-toggle helpers (Wave 7 todo 13) ───────────────────────
    // toggleVisibility: tray double-click OR "显示/隐藏" menu item → flip
    //   root.visible. raise()+requestActivate() mirrors the Java reference's
    //   toFront()+requestFocus() so a previously-hidden window surfaces on
    //   top of the Z order when re-shown.
    // showWindow: "设置" menu item → ensure the window is visible + on top
    //   before switching to SettingsPage. No-op if already visible.
    //
    // Both honor the QGuiApplication::setQuitOnLastWindowClosed(false) call
    // made by TrayManager's constructor — hiding the window does NOT quit.
    function toggleVisibility() {
        if (root.visible) {
            root.hide()
        } else {
            root.show()
            root.raise()
            root.requestActivate()
        }
    }
    function showWindow() {
        if (!root.visible) root.show()
        root.raise()
        root.requestActivate()
    }

    // ── Sidebar instance selection (Phase 5, todo 7) ──────────────────────
    // Set when the user clicks a sidebar row. Passed to the detail page's
    // `instance` property on switch; cleared by the delete flow.
    property var currentInstance: null
    property string currentInstanceUuid: ""

    // Called from Sidebar.onInstanceSelected. Loads the InstanceSession from
    // the manager, records its UUID (instance.config().id is not reachable
    // from QML — config() isn't Q_INVOKABLE — so the sidebar passes the uuid
    // role directly), and replaces the StackView with the detail page bound
    // to that instance. The initial-properties argument is how StackView
    // pushes the `instance` property into the new top item.
    function selectInstance(row, uuid) {
        root.currentInstanceUuid = uuid
        root.currentInstance = instanceManager.instanceAt(row)
        pageStack.replace(instanceDetailPageComp,
                          { instance: root.currentInstance })
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

    // Route the current detail page's deleteRequested() → the manager's two-
    // step delete (requestDelete emits deleteConfirmed, which the confirm
    // dialog below catches). ignoreUnknownSignals lets the same Connections
    // sit over WelcomePage / SettingsPage / MonitorPage (which never emit
    // deleteRequested) without QML warnings.
    Connections {
        target: pageStack.currentItem
        ignoreUnknownSignals: true
        function onDeleteRequested() {
            if (root.currentInstanceUuid !== "")
                instanceManager.requestDelete(root.currentInstanceUuid)
        }
    }

    // InstanceManager::deleteConfirmed(uuid) → confirm dialog (blueprint §4.2:
    // default-focus Cancel so Tab/Enter does NOT delete). The pending UUID is
    // stashed on the dialog; Ok calls deleteInstance (the real teardown),
    // Cancel is a no-op.
    Connections {
        target: instanceManager
        ignoreUnknownSignals: true
        function onDeleteConfirmed(uuid) {
            deleteConfirmDialog.pendingUuid = uuid
            deleteConfirmDialog.open()
        }
    }

    // ── Delete confirm dialog (delete-protection, blueprint §4.2) ──────────
    // Custom QtQuick.Controls Dialog (not QtQuick.Dialogs MessageDialog) so we
    // get full control of focus. onOpened forces focus onto Cancel — pressing
    // Enter at the moment the dialog appears activates Cancel (safe). The
    // Delete button requires an explicit click or Tab-then-Enter. Cancel is
    // declared first so natural LTR tab order is Cancel → Delete.
    Dialog {
        id: deleteConfirmDialog
        anchors.centerIn: parent
        modal: true
        focus: true
        title: qsTr("Delete Instance")
        width: 360
        property string pendingUuid: ""

        onOpened: cancelButton.forceActiveFocus()

        contentItem: Column {
            spacing: 16
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("Delete this instance? This cannot be undone.")
                color: Theme.textColor
                font.pixelSize: 13
            }
            Row {
                spacing: 10
                Button {
                    id: cancelButton
                    text: qsTr("Cancel")
                    onClicked: deleteConfirmDialog.close()
                }
                Button {
                    id: deleteButton
                    text: qsTr("Delete")
                    onClicked: {
                        instanceManager.deleteInstance(deleteConfirmDialog.pendingUuid)
                        deleteConfirmDialog.pendingUuid = ""
                        deleteConfirmDialog.close()
                    }
                }
            }
        }
    }

    // ── Exit confirm dialog (exit-protection, blueprint §4.2, todo 15) ─────
    // Same focus-control pattern as deleteConfirmDialog: custom QtQuick.
    // Controls Dialog (NOT QtQuick.Dialogs MessageDialog) so we can force
    // default-focus onto Cancel. onOpened → exitCancelButton.forceActiveFocus
    // so pressing Enter at the moment the dialog appears activates Cancel
    // (safe). Cancel is declared first so natural LTR tab order is
    // Cancel → Exit. Ok/Ok-button requires an explicit click or Tab-then-Enter.
    Dialog {
        id: exitConfirmDialog
        anchors.centerIn: parent
        modal: true
        focus: true
        title: qsTr("Exit Confirmation")
        width: 360

        onOpened: exitCancelButton.forceActiveFocus()

        contentItem: Column {
            spacing: 16
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("Are you sure you want to exit the desktop pet controller? " +
                           "All running instances will be stopped.")
                color: Theme.textColor
                font.pixelSize: 13
            }
            Row {
                spacing: 10
                Button {
                    id: exitCancelButton
                    text: qsTr("Cancel")
                    onClicked: exitConfirmDialog.close()
                }
                Button {
                    id: exitOkButton
                    text: qsTr("Exit")
                    onClicked: {
                        exitConfirmDialog.close()
                        root.doExit()
                    }
                }
            }
        }
    }

    // Left sidebar — instance list + Add Instance flow (Phase 5 todo 7).
    Sidebar {
        z: 1
        onInstanceSelected: function(row, uuid) {
            root.selectInstance(row, uuid)
        }
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

    // ── Tray context menu (Wave 7 todo 13) ───────────────────────────────
    // QtQuick.Controls Menu — NOT QMenu (QMenu is QtWidgets, which the app
    // deliberately does not link — M3 resolution). The Menu pops when
    // TrayManager emits requestContextMenu (right-click on the tray icon);
    // 3 items per blueprint §4.1.6 (m6 fix — Show/Hide is ONE toggle entry,
    // not separate Show + Hide). Each item calls the matching Q_INVOKABLE
    // on trayManager so the signal is emitted uniformly from both the C++
    // activated() handler (DoubleClick → visibilityToggled) and the menu
    // clicks — testable via QSignalSpy in TrayManagerTest.
    Menu {
        id: trayMenu
        Action {
            text: qsTr("显示/隐藏")
            onTriggered: trayManager.activateToggleVisibility()
        }
        Action {
            text: qsTr("设置")
            onTriggered: trayManager.activateSettings()
        }
        Action {
            text: qsTr("退出")
            onTriggered: trayManager.activateQuit()
        }
    }

    // TrayManager (C++) signals → Main.qml behavior. ignoreUnknownSignals:
    // trayManager may be inert when no system tray is available (headless /
    // GNOME / CI) — the signals still connect fine, they just never fire.
    Connections {
        target: trayManager
        ignoreUnknownSignals: true
        // Right-click on tray icon → pop the Menu. menu.popup() with no args
        // opens at the current cursor on most platforms (Windows / X11);
        // todo 15 (closeAction) may refine the position if needed.
        function onRequestContextMenu() { trayMenu.popup() }
        // Double-click on tray OR "显示/隐藏" menu item → toggle visibility.
        function onVisibilityToggled() { root.toggleVisibility() }
        // "设置" menu item → switch to SettingsPage + ensure window visible.
        function onShowSettings() {
            root.switchPage("settings")
            root.showWindow()
        }
        // "退出" menu item → graceful shutdown via the unified exit path.
        // Bypasses confirmOnExit (the user explicitly chose "退出" from the
        // tray — they've already confirmed intent, matching the Java reference
        // where the tray exit calls performFullShutdown directly). doExit runs
        // stopAll → saveState → tray.shutdown → Qt.quit. Logging::shutdown
        // runs in main.cpp's post-app.exec() path so the log sink flushes.
        function onQuitRequested() { root.doExit() }
    }
}
