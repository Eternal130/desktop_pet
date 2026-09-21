import QtQuick
import QtQuick.Controls
import QtQuick.Window
import DesktopPet

// Native Fluent-style shell (design doc fluent-ui-redesign.html), no third-
// party QML library. Frameless window + custom titlebar + nav pane with
// groups / badges / instance section / search filter.
//
// Preserved business surface: switchPage(name) / selectInstance(row, uuid),
// tray Connections, delete/exit confirm dialogs, closeAction branching,
// context properties (instanceManager, panelConfig, trayManager, ...).
Window {
    id: root

    width: initialWidth > 0 ? initialWidth : 1180
    height: initialHeight > 0 ? initialHeight : 720
    minimumWidth: 920
    minimumHeight: 600
    x: initialX >= 0 ? initialX : (Screen.width - width) / 2
    y: initialY >= 0 ? initialY : (Screen.height - height) / 2
    visible: true
    title: qsTr("Desktop Pet Controller")
    color: Theme.bgColor

    flags: Qt.Window | Qt.FramelessWindowHint

    // ── Page bookkeeping ────────────────────────────────────────────────
    property string currentPage: "welcome"
    property var currentInstance: null
    property string currentInstanceUuid: ""

    function switchPage(name) {
        navPane.activeKey = name
        _loadPage(name)
    }

    function selectInstance(row, uuid) {
        root.currentInstanceUuid = uuid
        root.currentInstance = instanceManager.instanceAt(row)
        switchPage("instance")
    }

    function _loadPage(name) {
        root.currentPage = name
        if (name === "instance" && root.currentInstance === null)
            root.currentInstance = instanceManager.instanceAt(0)
        // P4 (§B.5): plugin pages load through the plugin Repeater below
        // (per-page Loader in a delegate context); the builtin loader is
        // switched OFF for them. No engine->load — pages stay inside this
        // window's routing.
        if (typeof pluginPages !== "undefined"
                && pluginPages.isPluginPage(name)) {
            pageLoader.sourceComponent = undefined
            return
        }
        pageLoader.sourceComponent = {
            "welcome":   welcomePageComp,
            "instance":  instanceDetailPageComp,
            "monitor":   monitorPageComp,
            "voicepack": voicePackPageComp,
            "models":    modelLibraryPageComp,
            "assets":    assetPageComp,
            "settings":  settingsPageComp
        }[name] ?? welcomePageComp
    }

    // Dev/QA seam (ScreenshotRunner): scroll SettingsPage's anchor column
    // to a section ("plugins" etc.) before/while it becomes current. The
    // pending value is consumed at page creation via settingsPageComp's
    // initialAnchor binding; calling while settings is already current
    // scrolls immediately. Normal runs never touch it (default "").
    property string settingsInitialAnchor: ""
    function devSetSettingsAnchor(section) {
        root.settingsInitialAnchor = section
        if (root.currentPage === "settings" && pageLoader.item)
            pageLoader.item._scrollTo(section)
    }

    function toggleVisibility() {
        if (root.visible) root.hide()
        else { root.show(); root.raise(); root.requestActivate() }
    }
    function showWindow() {
        if (!root.visible) root.show()
        root.raise()
        root.requestActivate()
    }

    // S4 (event-driven stop): the exit sequence no longer blocks on stopAll.
    // doExit initiates the async graceful stop of every running renderer and
    // the actual quit happens in onStopAllFinished (or via the watchdog if a
    // bug wedges the wave — the stop state machine bounds itself at ~7s).
    property bool exitInProgress: false
    property bool exitDone: false

    function doExit() {
        if (root.exitInProgress)
            return
        root.exitInProgress = true
        // stopAll initiates the async stop of every running renderer and
        // returns how many are winding down; stopAllFinished completes the
        // exit. Zero running instances → quit right away.
        if (instanceManager.stopAll() > 0) {
            exitWatchdog.start()
            return
        }
        root.finishExit()
    }

    function finishExit() {
        if (root.exitDone)
            return
        root.exitDone = true
        windowStateSaver.saveWindowState(root.x, root.y, root.width,
                                         root.height, Theme.currentTheme)
        trayManager.shutdown()
        Qt.quit()
    }

    // Bounds the async exit wait: the ProcessManager stop state machine
    // guarantees completion (grace window + force-kill + post-kill reap,
    // ~7s worst case), so 12s can only ever trigger on a logic bug — and a
    // wedged exit is worse than a forceful one.
    Timer {
        id: exitWatchdog
        interval: 12000
        onTriggered: {
            if (root.exitInProgress && !root.exitDone)
                root.finishExit()
        }
    }

    onClosing: function(close) {
        windowStateSaver.saveWindowState(root.x, root.y, root.width,
                                         root.height, Theme.currentTheme)
        if (panelConfig.closeAction === "minimize") {
            root.hide()
            close.accepted = false
        } else if (panelConfig.confirmOnExit || panelConfig.closeAction === "ask") {
            exitConfirmDialog.open()
            close.accepted = false
        } else {
            root.doExit()
        }
    }

    function _runningCount() {
        let n = 0
        for (let i = 0; i < instanceManager.count; ++i) {
            const s = instanceManager.instanceAt(i)
            if (s && s.status === "running") ++n
        }
        return n
    }

    // ── Canvas gradient (design body: 135deg 3-stop, sits under everything;
    // content pages stay transparent so cards float on it) ────────────────
    Rectangle {
        id: canvasLayer
        z: 0
        anchors.fill: parent
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Theme.bgGradA }
            GradientStop { position: 0.5; color: Theme.bgGradB }
            GradientStop { position: 1.0; color: Theme.bgGradC }
        }
    }

    // ── Window resize edges (frameless windows lose the system border;
    // 8 invisible strips call startSystemResize, same family as the
    // titlebar's startSystemMove) ────────────────────────────────────────
    component ResizeEdge : MouseArea {
        id: edge
        property int edges: 0          // combination of Qt.LeftEdge etc.
        z: 50
        hoverEnabled: true
        cursorShape: {
            const l = edges & Qt.LeftEdge, r = edges & Qt.RightEdge
            const t = edges & Qt.TopEdge,  b = edges & Qt.BottomEdge
            if ((l && t) || (r && b)) return Qt.SizeFDiagCursor
            if ((l && b) || (r && t)) return Qt.SizeBDiagCursor
            if (l || r) return Qt.SizeHorCursor
            return Qt.SizeVerCursor
        }
        onPressed: (mouse) => {
            const accepted = root.startSystemResize(edge.edges)
            if (accepted) mouse.accepted = true
        }
    }

    ResizeEdge { // left
        anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
        width: 6; edges: Qt.LeftEdge
    }
    ResizeEdge { // right
        anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
        width: 6; edges: Qt.RightEdge
    }
    ResizeEdge { // top
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        height: 6; edges: Qt.TopEdge
    }
    ResizeEdge { // bottom
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        height: 6; edges: Qt.BottomEdge
    }
    ResizeEdge { // top-left
        anchors.left: parent.left; anchors.top: parent.top
        width: 12; height: 12; edges: Qt.LeftEdge | Qt.TopEdge
    }
    ResizeEdge { // top-right
        anchors.right: parent.right; anchors.top: parent.top
        width: 12; height: 12; edges: Qt.RightEdge | Qt.TopEdge
    }
    ResizeEdge { // bottom-left
        anchors.left: parent.left; anchors.bottom: parent.bottom
        width: 12; height: 12; edges: Qt.LeftEdge | Qt.BottomEdge
    }
    ResizeEdge { // bottom-right
        anchors.right: parent.right; anchors.bottom: parent.bottom
        width: 12; height: 12; edges: Qt.RightEdge | Qt.BottomEdge
    }

    // ── Titlebar (design .titlebar: 44px translucent band + hairline) ────
    Rectangle {
        id: titleBar
        z: 10
        height: 44
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        color: Theme.titleBarOverlay

        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: Theme.hairlineColor
        }

        Rectangle {
            width: 18; height: 18; radius: 4
            x: 16; y: (parent.height - height) / 2
            color: assetManager.logoUrl.length > 0
                   ? "transparent"
                   : Theme.accentColor
            PanelLogoGlyph {
                anchors.centerIn: parent
                size: 18
            }
        }
        Text {
            x: 44
            y: (parent.height - height) / 2
            text: {
                const pageNames = {
                    "welcome":   qsTr("主页"),
                    "monitor":   qsTr("资源监控"),
                    "voicepack": qsTr("语音包"),
                    "models":    qsTr("模型库"),
                    "assets":    qsTr("资源管理"),
                    "settings":  qsTr("设置")
                }
                if (root.currentInstance && root.currentPage === "instance")
                    return root.currentInstance.label + " — " + root.title
                // Plugin pages are not in pageNames — ask the model for the
                // registered title instead of falling straight through to
                // root.title (which produced "Title — Title" duplication);
                // a missing/empty title still falls back to root.title.
                let pageName = pageNames[root.currentPage] ?? ""
                if (pageName === ""
                        && typeof pluginPages !== "undefined"
                        && pluginPages.isPluginPage(root.currentPage)) {
                    pageName = pluginPages.titleFor(root.currentPage)
                }
                return (pageName !== "" ? pageName : root.title) +
                       " — " + root.title
            }
            color: Theme.text2Color
            font.pixelSize: 12
        }

        Row {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom

            TitleBarButton { kind: "min";  onActivated: root.showMinimized() }
            TitleBarButton {
                kind: root.visibility === Window.Maximized ? "restore" : "max"
                onActivated: root.visibility === Window.Maximized
                           ? root.showNormal() : root.showMaximized()
            }
            TitleBarButton {
                kind: "close"
                isClose: true
                onActivated: root.close()
            }
        }

        DragHandler {
            target: null
            onActiveChanged: if (active) {
                if (root.visibility === Window.Maximized)
                    root.showNormal()
                root.startSystemMove()
            }
        }
        TapHandler {
            onDoubleTapped: root.visibility === Window.Maximized
                            ? root.showNormal() : root.showMaximized()
        }
    }

    // ── Nav pane (design .nav: 75% translucent + right hairline) ─────────
    Rectangle {
        id: navPane
        z: 5
        anchors.left: parent.left
        anchors.top: titleBar.bottom
        anchors.bottom: parent.bottom
        width: 256
        color: Theme.navOverlay

        property string activeKey: "welcome"
        property string searchFilter: ""

        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: Theme.hairlineColor
        }

        // search box (design .nav-search: visible Fluent field stroke).
        // Two-layer trick: outer = stroke color, inner (inset 1px) = a
        // near-pane opaque fill. A transparent inner let the focused
        // accent fill the whole box (blue-background bug) and made the
        // 1px ring too faint at high DPI.
        Rectangle {
            id: searchBox
            x: 10; y: 12
            width: parent.width - 20
            height: 32
            radius: Theme.radiusMd
            color: searchInput.activeFocus ? Theme.accentColor
                : (Theme.dark ? "#ffffff" : "#5c5c5c")

            Rectangle {
                anchors.fill: parent
                anchors.margins: 1
                radius: Theme.radiusMd - 1
                color: Theme.dark ? "#262626" : "#fafafa"

                Rectangle {
                    // Fluent field: heavier bottom edge inside the stroke
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: Theme.dark ? "#ffffff59" : "#00000059"
                }

                Text {
                    visible: searchInput.text.length === 0
                    x: 10
                    y: (parent.height - height) / 2
                    text: qsTr("🔍 搜索页面、实例、语音包…")
                    color: Theme.text3Color
                    font.pixelSize: 12
                }
                TextInput {
                    id: searchInput
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    verticalAlignment: TextInput.AlignVCenter
                    color: Theme.textColor
                    font.pixelSize: 12
                    clip: true
                    onTextChanged: navPane.searchFilter = text.toLowerCase()
                }
            }
        }

        // scrollable nav list between search box and pinned settings item
        Flickable {
            id: navScroll
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: searchBox.bottom
            anchors.topMargin: 8
            anchors.bottom: settingsNavItem.top
            contentHeight: navCol.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar {}

            Column {
                id: navCol
                width: parent.width
                spacing: 0

                NavGroupLabel { text: qsTr("宠物") }

                NavItem {
                    itemKey: "welcome"
                    icon: "🏠"; label: qsTr("主页")
                    match: navPane.searchFilter
                }
                NavItem {
                    itemKey: "instance"
                    icon: "🐱"; label: qsTr("实例详情")
                    // S3: roster count read goes through rosterModel (the
                    // pet::IInstanceApi read path); the page itself and all
                    // create/delete flows still use instanceManager.
                    badgeText: rosterModel.count > 0
                               ? rosterModel.count : ""
                    match: navPane.searchFilter
                }
                NavItem {
                    itemKey: "monitor"
                    icon: "📊"; label: qsTr("资源监控")
                    match: navPane.searchFilter
                }
                NavItem {
                    itemKey: "voicepack"
                    icon: "🎙"; label: qsTr("语音包")
                    badgeText: voicePacks.packCount > 0
                               ? voicePacks.packCount : ""
                    match: navPane.searchFilter
                }
                NavItem {
                    itemKey: "models"
                    icon: "🎭"; label: qsTr("模型库")
                    badgeText: modelLibrary.modelCount > 0
                               ? modelLibrary.modelCount : ""
                    match: navPane.searchFilter
                }
                NavItem {
                    itemKey: "assets"
                    icon: "🗂"; label: qsTr("资源管理")
                    badgeText: assetManager.assetCount() > 0
                               ? assetManager.assetCount() : ""
                    match: navPane.searchFilter
                }

                // P4 (§B.6): dynamic plugin pages — appended after every
                // built-in item, sorted by the manifest order the host
                // registered them in (PluginPageModel keeps (order,title)
                // sort). NavItem's own properties consume the roles here;
                // the PAGE itself loads through the plugin Repeater below.
                NavGroupLabel {
                    text: qsTr("插件")
                    visible: pluginPages.count > 0
                }
                Repeater {
                    model: pluginPages
                    NavItem {
                        required property string pageKey
                        required property string title
                        itemKey: pageKey
                        icon: "🧩"; label: title
                        match: navPane.searchFilter
                    }
                }

                Item { width: 1; height: 12 }

                // design mock: the nav swaps to per-instance items only on
                // the instance page; other pages show just the 4 main items.
                NavGroupLabel {
                    text: qsTr("宠物 · 实例切换")
                    visible: root.currentPage === "instance"
                             && rosterModel.count > 0
                }
                // S3 dogfooding: the roster LIST is RosterApiModel — the
                // read path over pet::IInstanceApi (same shared
                // InstanceApiImpl the plugin SDK serves). Roles are
                // byte-identical to the old instanceManager binding, so the
                // delegate is unchanged. Click still routes through
                // selectInstance(row, uuid) → instanceManager.instanceAt
                // (write/selection paths deliberately stay on the manager
                // until S2); row indexes line up because both models share
                // the manager's roster order.
                Repeater {
                    model: rosterModel
                    delegate: NavInstanceItem {
                        id: instNav
                        required property string label
                        required property string uuid
                        required property bool connected
                        required property int index
                        instanceLabel: instNav.label
                        live: instNav.connected
                        match: navPane.searchFilter
                        visible: root.currentPage === "instance"
                        onClicked: root.selectInstance(instNav.index, instNav.uuid)
                    }
                }
            }
        }

        NavItem {
            id: settingsNavItem
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: profileRow.top
            itemKey: "settings"
            icon: "⚙"; label: qsTr("设置")
            match: navPane.searchFilter
        }

        // profile footer
        Rectangle {
            id: profileRow
            width: parent.width
            height: 54
            anchors.bottom: parent.bottom
            color: "transparent"

            Rectangle {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: Theme.borderColor
            }

            Row {
                x: 16; y: (parent.height - height) / 2
                spacing: 10
                Item {
                    width: 30; height: 30
                    anchors.verticalCenter: parent.verticalCenter

                    Rectangle {
                        anchors.fill: parent
                        radius: 15
                        visible: assetManager.logoUrl.length === 0
                        gradient: Gradient {
                            GradientStop { position: 0; color: "#8b8bf0" }
                            GradientStop { position: 1; color: Theme.accentColor }
                        }
                    }
                    Rectangle {
                        anchors.fill: parent
                        radius: 15
                        visible: assetManager.logoUrl.length > 0
                        color: Theme.dark ? "#2a2a2a" : "#eaeaea"
                        border.width: 1
                        border.color: Theme.borderColor
                    }
                    PanelLogoGlyph {
                        anchors.centerIn: parent
                        size: 24
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("v2.0 · %1 实例运行中").arg(
                        root._runningCount())
                    color: Theme.text2Color
                    font.pixelSize: 12
                }
            }
        }
    }

    // ── Content ─────────────────────────────────────────────────────────
    Loader {
        id: pageLoader
        anchors.left: navPane.right
        anchors.top: titleBar.bottom
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        sourceComponent: welcomePageComp
    }

    // ── Plugin pages (P4, §B.5) ─────────────────────────────────────────
    // Each plugin page is a Loader inside this Repeater's delegate: the
    // loaded file's creation-context chain therefore carries the delegate
    // context, which exposes `model` — including the per-plugin `bridge`
    // role (PluginBridge: pluginId/title/apiVersion/notify/log). That is
    // the stage-1 exposure pattern the SDK template's SamplePage uses.
    //
    // KNOWN DEBT (§B.5, documented): the ideal per-page child QQmlContext
    // would expose ONLY "plugin" — today the context chain still sees the
    // root context properties (first-party trust; stage-2 will tighten).
    // The delegates deliberately use NO required properties: required
    // properties would suppress the `model` context exposure the loaded
    // page depends on.
    Repeater {
        model: pluginPages
        delegate: Loader {
            id: pluginPageHost
            anchors.left: navPane.right
            anchors.top: titleBar.bottom
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            active: root.currentPage === pluginPageHost.pageKey
            visible: active
            property string pageKey: model.pageKey
            property string qmlUrl: model.qmlUrl
            source: active ? pluginPageHost.qmlUrl : ""
        }
    }

    Component { id: welcomePageComp;        WelcomePage {} }
    Component { id: instanceDetailPageComp; InstanceDetailPage { instance: root.currentInstance } }
    Component { id: monitorPageComp;        MonitorPage { instance: root.currentInstance } }
    Component { id: voicePackPageComp;      VoicePackPage {} }
    Component {
        id: modelLibraryPageComp
        ModelLibraryPage {
            onRequestSwitchPage: (name) => root.switchPage(name)
        }
    }
    Component {
        id: assetPageComp
        AssetPage {
            onRequestSwitchPage: (name) => root.switchPage(name)
        }
    }
    Component { id: settingsPageComp;       SettingsPage { initialAnchor: root.settingsInitialAnchor } }

    // ── Dialogs ─────────────────────────────────────────────────────────
    AppDialog {
        id: deleteConfirmDialog
        title: qsTr("删除实例")
        message: qsTr("删除该实例？此操作不可撤销。")
        positiveText: qsTr("删除")
        onPositiveClicked: {
            const gone = pendingUuid
            // S2 dogfooding: the actual delete goes through rosterModel —
            // the pet::IInstanceControlApi write path (the SAME shared
            // implementation the plugin SDK serves via queryApi). The
            // confirm flow (requestDelete → deleteConfirmed) stays on
            // instanceManager; a null/errored result is fire-and-forget
            // here — the roster reset below this model reports the truth.
            rosterModel.deleteInstance(gone)
            pendingUuid = ""
            root.currentInstance = null
            root.currentInstanceUuid = ""
            // stay on the instance page when other instances remain —
            // selectInstance(row, uuid) rebinds the detail page to the
            // first surviving instance; only fall back to Home when the
            // roster is now empty.
            // S4 two-phase delete: a RUNNING instance's row survives until
            // its async stop completes — never rebind to the doomed row
            // (it vanishes via rowsAboutToBeRemoved shortly); bind the next
            // survivor instead.
            if (instanceManager.rowCount() > 0) {
                const first = instanceManager.instanceAt(0)
                if (first && first.uuid === gone) {
                    if (instanceManager.rowCount() > 1)
                        root.selectInstance(1, instanceManager.instanceAt(1).uuid)
                    else
                        root.switchPage("welcome")
                } else if (first) {
                    root.selectInstance(0, first.uuid)
                } else {
                    root.switchPage("welcome")
                }
            } else {
                root.switchPage("welcome")
            }
        }
        property string pendingUuid: ""
    }

    AppDialog {
        id: exitConfirmDialog
        title: qsTr("退出确认")
        message: qsTr("确定要退出桌面宠物控制器吗？所有运行中的实例将被停止。")
        positiveText: qsTr("退出")
        onPositiveClicked: root.doExit()
    }

    Connections {
        target: instanceManager
        ignoreUnknownSignals: true
        function onDeleteConfirmed(uuid) {
            deleteConfirmDialog.pendingUuid = uuid
            deleteConfirmDialog.open()
        }
        // S4: the async stop wave completed — finish the pending exit.
        function onStopAllFinished() {
            if (root.exitInProgress && !root.exitDone)
                root.finishExit()
        }
    }

    // ── Tray menu ───────────────────────────────────────────────────────
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

    // ── Notification bubble stream (气泡信息流) ─────────────────────────
    // Window-in-Window: a Window declared as a child of the Main Window
    // creates its own native always-on-top overlay sharing the engine.
    BubbleStreamWindow { }

    Component.onCompleted: {
        console.log("COLD_START_MS=" + (Date.now() - coldStartT0Ms))
        Theme.setTheme(initialTheme)
    }

    // ── Inline components ───────────────────────────────────────────────
    // Panel logo: the custom logo image when one is applied, else the 🐾
    // glyph. Shared by the titlebar (top-left) and the nav profile footer
    // (bottom-left); re-evaluates on AssetManager::logoUrlChanged.
    component PanelLogoGlyph : Item {
        id: logoGlyph
        property int size: 16
        width: size
        height: size
        Image {
            anchors.fill: parent
            source: assetManager.logoUrl
            fillMode: Image.PreserveAspectFit
            visible: assetManager.logoUrl.length > 0
                     && status === Image.Ready
        }
        Text {
            anchors.centerIn: parent
            visible: assetManager.logoUrl.length === 0
            text: "🐾"
            font.pixelSize: logoGlyph.size * 0.8
        }
    }

    // Caption glyphs drawn as vector shapes (Win11 style) — Unicode
    // ▢/❐/✕ glyphs render at inconsistent sizes across font fallbacks, so
    // minimize/maximize/restore/close are hand-drawn strokes instead.
    component TitleBarGlyph : Item {
        id: glyph
        property string kind: "min"   // min | max | restore | close
        width: 10
        height: 10

        // minimize: thin centered dash, slightly above optical center
        Rectangle {
            visible: glyph.kind === "min"
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: 1.5
            width: parent.width
            height: 1
            radius: 0.5
            color: glyph._c
        }
        // maximize: 10x10 square outline (1px)
        Rectangle {
            visible: glyph.kind === "max"
            anchors.fill: parent
            radius: 1
            border.width: 1
            border.color: glyph._c
            color: "transparent"
        }
        // restore: two offset squares (back 8x8 top-right, front 8x8 bottom-left)
        Rectangle {
            visible: glyph.kind === "restore"
            x: 2; y: 0
            width: 8; height: 8
            radius: 1
            border.width: 1
            border.color: glyph._c
            color: "transparent"
        }
        Rectangle {
            visible: glyph.kind === "restore"
            x: 0; y: 2
            width: 8; height: 8
            radius: 1
            border.width: 1
            border.color: glyph._c
            color: Theme.dark ? "#2b2b2b" : "#ffffff"
        }
        Rectangle {
            visible: glyph.kind === "restore"
            x: 0; y: 2
            width: 8; height: 8
            radius: 1
            border.width: 1
            border.color: glyph._c
            color: "transparent"
        }
        // close: X of two rotated bars
        Rectangle {
            visible: glyph.kind === "close"
            anchors.centerIn: parent
            width: parent.width * 1.35
            height: 1
            rotation: 45
            color: glyph._c
        }
        Rectangle {
            visible: glyph.kind === "close"
            anchors.centerIn: parent
            width: parent.width * 1.35
            height: 1
            rotation: -45
            color: glyph._c
        }

        readonly property color _c: Theme.text2Color
    }

    component TitleBarButton : Rectangle {
        id: btn
        property string glyph: ""   // unused; kept for compat
        property string kind: "min"
        property bool isClose: false
        signal activated()
        width: 46
        height: 44
        color: _hover.hovered
               ? (isClose ? Theme.closeHoverColor
                          : Theme.withAlpha(Theme.textColor, 0.07))
               : "transparent"

        TitleBarGlyph {
            anchors.centerIn: parent
            kind: btn.kind
        }
        HoverHandler { id: _hover }
        TapHandler { onTapped: btn.activated() }
    }

    component NavGroupLabel : Item {
        id: grp
        property string text: ""
        height: visible ? 26 : 0
        width: parent.width
        Text {
            x: 24; y: 12
            text: grp.text
            color: Theme.text3Color
            font.pixelSize: 11
            font.weight: Font.DemiBold
        }
    }

    component NavItem : Item {
        id: item
        property string itemKey: ""
        property string icon: ""
        property string label: ""
        property string badgeText: ""
        property string match: ""

        readonly property bool active: navPane.activeKey === itemKey
        readonly property bool hidden: match.length > 0
            && label.toLowerCase().indexOf(match) === -1

        height: hidden ? 0 : 36
        visible: !hidden
        width: parent.width

        Rectangle {
            x: 8; y: 2
            width: parent.width - 16
            height: parent.height - 4
            radius: Theme.radiusMd
            color: item.active ? Theme.accentAlpha(0.12)
                : (_hover.hovered ? Theme.withAlpha(Theme.textColor, 0.05)
                                  : "transparent")
            Rectangle {
                visible: item.active
                x: -8; y: parent.height * 0.25
                width: 3; height: parent.height * 0.5
                radius: 2
                color: Theme.accentColor
            }
            Row {
                x: 12; y: (parent.height - height) / 2
                spacing: 14
                Text {
                    text: item.icon
                    font.pixelSize: 14
                    opacity: 0.85
                }
                Text {
                    text: item.label
                    color: Theme.textColor
                    font.pixelSize: 13
                    font.weight: item.active ? Font.DemiBold : Font.Normal
                }
            }
            Rectangle {
                visible: item.badgeText.length > 0
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                width: badgeTextItem.implicitWidth + 14
                height: 17
                radius: 9
                color: Theme.accentColor
                Text {
                    id: badgeTextItem
                    anchors.centerIn: parent
                    text: item.badgeText
                    color: "#ffffff"
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                }
            }
            HoverHandler { id: _hover }
            TapHandler { onTapped: root.switchPage(item.itemKey) }
        }
    }

    component NavInstanceItem : Item {
        id: iitem
        property string instanceLabel: ""
        property bool live: false
        property string match: ""
        signal clicked()

        readonly property bool hidden: match.length > 0
            && instanceLabel.toLowerCase().indexOf(match) === -1

        height: hidden ? 0 : 36
        visible: !hidden
        width: parent.width

        Rectangle {
            x: 8; y: 2
            width: parent.width - 16
            height: parent.height - 4
            radius: Theme.radiusMd
            color: _hover.hovered ? Theme.withAlpha(Theme.textColor, 0.05)
                                  : "transparent"
            Row {
                x: 12; y: (parent.height - height) / 2
                spacing: 14
                Text { text: "🐾"; font.pixelSize: 14 }
                Text {
                    text: iitem.instanceLabel
                    color: Theme.textColor
                    font.pixelSize: 13
                }
            }
            Rectangle {
                visible: iitem.live
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                width: 8; height: 8; radius: 4
                color: Theme.successColor
            }
            HoverHandler { id: _hover }
            TapHandler { onTapped: iitem.clicked() }
        }
    }
}
