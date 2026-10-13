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

    // ── Wave-2 selection memory ─────────────────────────────────────────
    // The builtin pageLoader DESTROYS a page when another one loads, so
    // every page-local selection used to reset on return: the monitor fell
    // back to row 0, the model library to the first entry, voice packs to
    // index 0. These window-level keys survive page switches — pages read
    // them at Component.onCompleted (falling back to their legal defaults
    // when a stored key no longer resolves: deleted instance / rescanned-
    // away model or pack) and write back on every selection change.
    // selectInstance's sourceComponent=undefined force-rebuild guard is
    // untouched: memory only decides what a freshly created page restores,
    // never the Loader mechanics.
    property string rememberedMonitorUuid: ""
    property string rememberedModelKey: ""
    property string rememberedPackKey: ""

    function switchPage(name) {
        navPane.activeKey = name
        _loadPage(name)
    }

    function selectInstance(row, uuid) {
        root.currentInstanceUuid = uuid
        root.currentInstance = instanceManager.instanceAt(row)
        // Instance→instance navigation: Loader keeps the old page when
        // sourceComponent is unchanged, so sliders/switches would keep
        // the previous instance's state. Clear first to force destroy +
        // rebuild; switchPage re-assigns the component right after and
        // controls reset naturally.
        if (pageLoader.sourceComponent === instanceDetailPageComp)
            pageLoader.sourceComponent = undefined
        switchPage("instance")
    }

    function _loadPage(name) {
        root.currentPage = name
        if (name === "instance" && root.currentInstance === null)
            root.currentInstance = instanceManager.instanceAt(0)
        console.warn("[PROBE] _loadPage", name, "currentInstance=",
                     String(root.currentInstance),
                     "rowCount=" + instanceManager.rowCount())
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
                                         root.height, panelConfig.theme)
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
                                         root.height, panelConfig.theme)
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

    // ── P2-16 batch lifecycle ──────────────────────────────────────────
    // Contract functions for the WelcomePage header buttons (lane A calls
    // these with an existence guard). Roster READ goes through
    // instanceManager (legal per the boundary discipline); every WRITE
    // goes through the instanceControl bridge — the same shared
    // pet::IInstanceControlApi path the detail page's start/stop buttons
    // use. start skips running/pending/connecting (error keeps retry
    // eligibility); stop skips anything that is not running. Both status
    // flips are synchronous (stop → stopped, start → pending), so the
    // single-pass loop never double-triggers an instance.
    function startAllInstances() {
        for (let i = 0; i < instanceManager.rowCount(); ++i) {
            const s = instanceManager.instanceAt(i)
            if (!s)
                continue
            if (s.status === "running" || s.status === "pending"
                    || s.status === "connecting")
                continue
            instanceControl.start(s.uuid)
        }
    }

    function stopAllInstances() {
        for (let i = 0; i < instanceManager.rowCount(); ++i) {
            const s = instanceManager.instanceAt(i)
            if (!s || s.status !== "running")
                continue
            instanceControl.stop(s.uuid)
        }
    }

    // ── Canvas base (solid palette window color; chrome distinguishes
    // itself by hairlines instead of translucent overlays) ────────────────
    Rectangle {
        id: canvasLayer
        z: 0
        anchors.fill: parent
        color: Theme.bgColor
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
        color: "transparent"

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
        color: "transparent"

        property string activeKey: "welcome"
        property string searchFilter: ""

        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: Theme.hairlineColor
        }

        // search box (Fluent field: visible stroke + bottom emphasis line).
        // Two-layer trick: outer = stroke color, inner (inset 1px) = the
        // field fill. A transparent inner let the focused accent fill the
        // whole box (blue-background bug) and made the 1px ring too faint
        // at high DPI.
        Rectangle {
            id: searchBox
            x: 10; y: 12
            width: parent.width - 20
            height: 32
            radius: Theme.radiusMd
            color: searchInput.activeFocus
                   ? Theme.accentColor : Theme.borderColor

            Rectangle {
                anchors.fill: parent
                anchors.margins: 1
                radius: Theme.radiusMd - 1
                color: Theme.surfaceColor

                Rectangle {
                    // Fluent field: heavier bottom edge inside the stroke
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: Qt.alpha(Theme.textColor, 0.25)
                }

                // 🔍 placeholder replaced by a vector search icon + text —
                // the emoji glyph rendered as tofu on emoji-font-less hosts.
                Row {
                    visible: searchInput.text.length === 0
                    x: 10
                    y: (parent.height - height) / 2
                    spacing: 6
                    AppIcon {
                        name: "search"
                        size: 13
                        color: Theme.text3Color
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        // P2-5: honest copy — the box only filters nav labels
                        // (pages + instance names), it does not search content.
                        text: qsTr("筛选导航…")
                        color: Theme.text3Color
                        font.pixelSize: 12
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                TextInput {
                    id: searchInput
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    // room for the ✕ clear button while a filter is active
                    anchors.rightMargin: searchInput.text.length > 0 ? 30 : 10
                    verticalAlignment: TextInput.AlignVCenter
                    color: Theme.textColor
                    font.pixelSize: 12
                    clip: true
                    onTextChanged: navPane.searchFilter = text.toLowerCase()
                }
                // P2-5: clear button — visible only with a non-empty
                // filter; clearing restores the full navigation list.
                Item {
                    id: searchClearBtn
                    visible: searchInput.text.length > 0
                    anchors.right: parent.right
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    width: 20
                    height: 20
                    Text {
                        anchors.centerIn: parent
                        text: "✕"
                        color: _clearMa.containsMouse ? Theme.textColor
                                                      : Theme.text3Color
                        font.pixelSize: 11
                    }
                    MouseArea {
                        id: _clearMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            searchInput.clear()
                            searchInput.forceActiveFocus()
                        }
                    }
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
                    iconName: "home"; label: qsTr("主页")
                    match: navPane.searchFilter
                }
                NavItem {
                    itemKey: "instance"
                    iconName: "cat"; label: qsTr("实例详情")
                    // S3: roster count read goes through rosterModel (the
                    // pet::IInstanceApi read path); the page itself and all
                    // create/delete flows still use instanceManager.
                    badgeText: rosterModel.count > 0
                               ? rosterModel.count : ""
                    match: navPane.searchFilter
                }
                NavItem {
                    itemKey: "monitor"
                    iconName: "chart"; label: qsTr("资源监控")
                    match: navPane.searchFilter
                }
                NavItem {
                    itemKey: "voicepack"
                    iconName: "mic"; label: qsTr("语音包")
                    badgeText: voicePacks.packCount > 0
                               ? voicePacks.packCount : ""
                    match: navPane.searchFilter
                }
                NavItem {
                    itemKey: "models"
                    iconName: "cube"; label: qsTr("模型库")
                    badgeText: modelLibrary.modelCount > 0
                               ? modelLibrary.modelCount : ""
                    match: navPane.searchFilter
                }
                NavItem {
                    itemKey: "assets"
                    iconName: "folder"; label: qsTr("资源管理")
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
                        iconName: "puzzle"; label: title
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
            iconName: "gear"; label: qsTr("设置")
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
                            GradientStop { position: 0; color: Qt.lighter(Theme.accentColor, 1.3) }
                            GradientStop { position: 1; color: Theme.accentColor }
                        }
                    }
                    Rectangle {
                        anchors.fill: parent
                        radius: 15
                        visible: assetManager.logoUrl.length > 0
                        color: Theme.offBgColor
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
        danger: true
        title: qsTr("删除实例")
        // Roster READ (legal per the boundary discipline): resolve the
        // instance label from pendingUuid so the warning names the exact
        // instance. At open time the row always still exists (the actual
        // delete happens in onPositiveClicked / two-phase stop); the
        // nameless fallback only guards a vanished row.
        message: {
            let name = ""
            for (let i = 0; i < instanceManager.rowCount(); ++i) {
                const s = instanceManager.instanceAt(i)
                if (s && s.uuid === deleteConfirmDialog.pendingUuid) {
                    name = s.label
                    break
                }
            }
            return name.length > 0
                ? qsTr("删除实例「%1」？该实例的配置将被移除，此操作不可撤销。")
                      .arg(name)
                : qsTr("删除该实例？该实例的配置将被移除，此操作不可撤销。")
        }
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
        // P2-16: batch stop from the tray (start-all stays a WelcomePage
        // header button — tray is quick-glance territory).
        Action {
            text: qsTr("停止全部")
            onTriggered: root.stopAllInstances()
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
    // It is deliberately NOT transient to Main (BubbleStreamWindow sets
    // transientParent: null) so bubbles keep showing while the panel is
    // minimized or hidden in the tray.
    BubbleStreamWindow { }

    Component.onCompleted: {
        console.log("COLD_START_MS=" + (Date.now() - coldStartT0Ms))
        // Theme colors are system-palette driven now; the persisted panel
        // theme string (panelConfig.theme) is no longer consumed by QML —
        // kept for SDK/persistence compatibility only.
    }

    // ── Inline components ───────────────────────────────────────────────
    // Panel logo: the custom logo image when one is applied, else the
    // vector paw mark (was the 🐾 emoji — tofu without a color-emoji font,
    // and it could not follow the theme). Shared by the titlebar (top-left)
    // and the nav profile footer (bottom-left); re-evaluates on
    // AssetManager::logoUrlChanged. accentTextColor keeps the paw readable
    // on the accent-filled avatar disc in both schemes.
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
        AppIcon {
            anchors.centerIn: parent
            visible: assetManager.logoUrl.length === 0
            name: "paw"
            size: logoGlyph.size * 0.72
            color: Theme.accentTextColor
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
            color: Theme.bgColor
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
               ? (isClose ? Theme.errorColor
                          : Qt.alpha(Theme.textColor, 0.07))
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
        property string iconName: ""
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
            color: item.active ? Qt.alpha(Theme.accentColor, 0.12)
                : (_hover.hovered ? Qt.alpha(Theme.textColor, 0.05)
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
                // vector nav icon (was emoji text) — tint follows the
                // selection like Fluent nav panes do
                AppIcon {
                    name: item.iconName
                    size: 16
                    color: item.active ? Theme.accentColor : Theme.text2Color
                    anchors.verticalCenter: parent.verticalCenter
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
                    color: Theme.accentTextColor
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
            color: _hover.hovered ? Qt.alpha(Theme.textColor, 0.05)
                                  : "transparent"
            Row {
                x: 12; y: (parent.height - height) / 2
                spacing: 14
                AppIcon {
                    name: "paw"
                    size: 16
                    color: Theme.text2Color
                    anchors.verticalCenter: parent.verticalCenter
                }
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
