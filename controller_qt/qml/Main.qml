import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtQuick.Layouts
import FluentUI
import DesktopPet

// FluentUI full-shell rewrite (feat/qt-fluentui-rewrite branch).
//
// Window chrome is now FluentUI: FluWindow (frameless) + FluAppBar (drag /
// minimize / maximize / close) + FluNavigationView (left nav pane hosting the
// three pages). All business surfaces are preserved verbatim:
//   - switchPage(name) — same names ("welcome"|"monitor"|"settings"), still
//     called by the --screenshot harness in main.cpp
//   - selectInstance(row, uuid) — sidebar instance selection
//   - tray Connections, delete/exit confirm dialogs, closeAction branching
//   - context properties (instanceManager, panelConfig, trayManager, ...)
//
// The old custom TitleBar/Sidebar/ResizeHandles files stay on disk but are no
// longer instantiated here.
FluWindow {
    id: root
    width: initialWidth > 0 ? initialWidth : 1200
    height: initialHeight > 0 ? initialHeight : 760
    x: initialX >= 0 ? initialX : (Screen.width - width) / 2
    y: initialY >= 0 ? initialY : (Screen.height - height) / 2
    visible: true
    title: qsTr("Desktop Pet Controller")
    launchMode: FluWindowType.SingleInstance
    // Win11 system backdrop — this FluentUI version drives Mica through the
    // frameless effect, not a FluTheme flag. "mica" tints the window with
    // the user's wallpaper (the official demo's signature look); degrades
    // to "dwm-blur"/"normal" below Win11.
    // Win11 backdrop. NOTE: in this FluentUI version FluWindow only renders
    // a TRANSPARENT base under "dwm-blur" (mica still paints an opaque
    // windowActiveBackgroundColor over the DWM material) — dwm-blur is the
    // effect that actually shows the wallpaper-tinted backdrop, matching
    // the official demo's look. Selected via the demo's own Theme page.
    effect: "dwm-blur"

    appBar: FluAppBar {
        title: qsTr("Desktop Pet Controller")
        showDark: true
        z: 7
    }

    // ── Page bookkeeping ────────────────────────────────────────────────────
    property string currentPage: "welcome"
    property var currentInstance: null
    property string currentInstanceUuid: ""

    function switchPage(name) {
        // Main items: startPageByItem (framework selection + onTapListener
        // load hook). Footer Settings: startPageByItem cannot resolve footer
        // items (verified empirically — getItems() misses them), so drive
        // the load directly; footer selection highlight is a known gap.
        if (name === "settings") {
            navSettings.onTapListener()
            return
        }
        let item = null
        if (name === "welcome")        item = navHome
        else if (name === "instance")  item = navInstance
        else if (name === "monitor")   item = navMonitor
        else if (name === "voicepack") { navVoicePack.onTapListener(); return }
        if (item !== null) navView.startPageByItem(item)
    }

    function selectInstance(row, uuid) {
        root.currentInstanceUuid = uuid
        root.currentInstance = instanceManager.instanceAt(row)
        navView.startPageByItem(navInstance)
    }

    // Tray window-toggle helpers (Wave 7 todo 13 — unchanged behavior).
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

    // Unified exit path (Wave 7 todo 15): stopAll → saveState → tray shutdown
    // → quit. The screenshot harness never calls this (it quits via
    // QApplication::quit from C++), but tray "退出" and close-with-confirm do.
    function doExit() {
        instanceManager.stopAll()
        windowStateSaver.saveWindowState(root.x, root.y, root.width,
                                         root.height, Theme.currentTheme)
        trayManager.shutdown()
        Qt.quit()
    }

    onClosing: function(close) {
        windowStateSaver.saveWindowState(root.x, root.y, root.width,
                                         root.height, Theme.currentTheme)
        if (panelConfig.closeAction === "minimize") {
            root.hide()
            close.accepted = false
        } else if (panelConfig.confirmOnExit) {
            exitConfirmDialog.open()
            close.accepted = false
        } else {
            root.doExit()
        }
    }

    // Central page loader driven by nav onTapListener callbacks. Declared
    // BEFORE navView so the nav pane (later sibling, higher z) always paints
    // above page content; leftMargin mirrors the framework's own
    // loader_content margin (cellWidth expanded / navCompactWidth compact).
    Loader {
        id: pageLoader
        anchors.fill: parent
        anchors.leftMargin: navView.cellWidth
        sourceComponent: welcomePageComp
    }

    FluNavigationView {
        id: navView
        anchors.fill: parent
        items: navItems
        footerItems: FluObject {
            FluPaneItem {
                id: navSettings
                title: qsTr("Settings")
                icon: FluentIcons.Settings
                // Same load contract as main items: onTapListener. The
                // framework's footer selection sync only runs when this is
                // unset, but then page loading would need `url` (which its
                // loader can't reach) — accept no footer highlight for now.
                onTapListener: function() {
                    root.currentPage = "settings"
                    pageLoader.sourceComponent = settingsPageComp
                }
            }
        }
        autoSuggestBox: FluAutoSuggestBox {
            placeholderText: qsTr("Search")
            items: [
                { title: navHome.title, key: "welcome" },
                { title: navInstance.title, key: "instance" },
                { title: navMonitor.title, key: "monitor" },
                { title: navVoicePack.title, key: "voicepack" },
                { title: navSettings.title, key: "settings" }
            ]
            onItemClicked:
                (data) => root.switchPage(data.key ?? data.title)
        }
        // Framework contract: items WITHOUT url get their onTapListener()
        // invoked by setCurrentIndex/startPageByItem — that's our page-load
        // hook. Selection state (accent pill) is managed by the nav view.
        title: qsTr("Desktop Pet")
        onLogoClicked: navView.startPageByItem(navHome)
        Component.onCompleted: navView.startPageByItem(navHome)
    }

    FluObject {
        id: navItems
        FluPaneItem {
            id: navHome
            title: qsTr("Home")
            icon: FluentIcons.Home
            onTapListener: function() {
                root.currentPage = "welcome"
                pageLoader.sourceComponent = welcomePageComp
            }
        }
        FluPaneItem {
            id: navInstance
            title: qsTr("Instance")
            icon: FluentIcons.Contact
            onTapListener: function() {
                if (root.currentInstance === null)
                    root.currentInstance = instanceManager.instanceAt(0)
                pageLoader.sourceComponent = instanceDetailPageComp
            }
        }
        FluPaneItem {
            id: navMonitor
            title: qsTr("Monitor")
            icon: FluentIcons.Diagnostic
            onTapListener: function() {
                root.currentPage = "monitor"
                if (root.currentInstance === null)
                    root.currentInstance = instanceManager.instanceAt(0)
                pageLoader.sourceComponent = monitorPageComp
            }
        }
        FluPaneItem {
            id: navVoicePack
            title: qsTr("Voice Packs")
            icon: FluentIcons.Microphone
            onTapListener: function() {
                root.currentPage = "voicepack"
                pageLoader.sourceComponent = voicePackPageComp
            }
        }
    }

    Component { id: welcomePageComp;        WelcomePage {} }
    Component { id: instanceDetailPageComp; InstanceDetailPage { instance: root.currentInstance } }
    Component { id: monitorPageComp;        MonitorPage { instance: root.currentInstance } }
    Component { id: settingsPageComp;       SettingsPage {} }
    Component { id: voicePackPageComp;      VoicePackPage {} }

    // Detail-page delete request: with FluNavigationView's own content
    // loader the page instance isn't directly reachable from here, so
    // InstanceDetailPage calls instanceManager.requestDelete itself (the
    // uuid travels via root.currentInstanceUuid); deleteConfirmed below
    // still drives the confirm dialog from Main.

    Connections {
        target: instanceManager
        ignoreUnknownSignals: true
        function onDeleteConfirmed(uuid) {
            deleteConfirmDialog.pendingUuid = uuid
            deleteConfirmDialog.open()
        }
    }

    FluContentDialog {
        id: deleteConfirmDialog
        title: qsTr("Delete Instance")
        message: qsTr("Delete this instance? This cannot be undone.")
        buttonFlags: FluContentDialogType.NegativeButton | FluContentDialogType.PositiveButton
        negativeText: qsTr("Cancel")
        positiveText: qsTr("Delete")
        onPositiveClicked: {
            instanceManager.deleteInstance(deleteConfirmDialog.pendingUuid)
            deleteConfirmDialog.pendingUuid = ""
        }
        property string pendingUuid: ""
    }

    FluContentDialog {
        id: exitConfirmDialog
        title: qsTr("Exit Confirmation")
        message: qsTr("Are you sure you want to exit the desktop pet controller? " +
                      "All running instances will be stopped.")
        buttonFlags: FluContentDialogType.NegativeButton | FluContentDialogType.PositiveButton
        negativeText: qsTr("Cancel")
        positiveText: qsTr("Exit")
        onPositiveClicked: root.doExit()
    }

    // ── Tray menu (Wave 7 todo 13) ───────────────────────────────────────
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

    Connections {
        target: trayManager
        ignoreUnknownSignals: true
        function onRequestContextMenu() { trayMenu.popup() }
        function onVisibilityToggled() { root.toggleVisibility() }
        function onShowSettings() {
            root.switchPage("settings")
            root.showWindow()
        }
        function onQuitRequested() { root.doExit() }
    }

    Component.onCompleted: {
        console.log("COLD_START_MS=" + (Date.now() - coldStartT0Ms))
        // FluentUI default darkMode is Light; the AppBar moon toggle lets the
        // user switch at runtime. Do NOT pin darkMode here — respect the
        // in-session choice.
        FluTheme.primaryColor = Theme.accentColor
        Theme.setTheme(initialTheme)
    }
}
