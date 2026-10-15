import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QWindowKit
import DesktopPet

// Native Fluent-style shell (design doc fluent-ui-redesign.html), no third-
// party QML library. Frameless window via QWindowKit (stdware/qwindowkit,
// linked from CMake + registered in main.cpp): the WindowAgent owns titlebar
// drag, double-click maximize, the titlebar right-click system menu, edge
// resize and (on Windows) Snap Layout + Mica, replacing the previous hand-
// rolled DragHandler + 8 ResizeEdge strips. Custom titlebar + nav pane with
// groups / badges / instance section / search filter remain right here.
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
    title: qsTr("Desktop Pet Controller")

    // NO manual FramelessWindowHint here: QWindowKit applies the right
    // per-platform strategy when the platform window is created (Qt-context
    // platforms — incl. Linux — get the frameless hint from QWK itself;
    // Windows gets native frame stripping + hit-test rewrite WITHOUT the
    // hint, which is what unlocks Snap Layout flyouts and the system menu).
    //
    // Show is deferred to Component.onCompleted: agent.setup() must run
    // before the platform window exists so the frame never flashes decorated.
    //
    // Material coupling: when Mica activates (Windows 11 22H2+, QWK reports
    // support via the attribute call's return) both the window base and the
    // canvas layer go transparent so the system material shows through; on
    // everything else (incl. all of Linux) micaActive stays false and the
    // SystemPalette-driven Theme.bgColor chain below is untouched.
    color: root.micaActive ? "transparent" : Theme.bgColor
    readonly property bool isWindows: Qt.platform.os === "windows"
    property bool micaActive: false

    // Material application runs in FOUR phases, all funnelling through this
    // one function (its log lines are the Windows-side diagnosis surface):
    //   pre-show     — QWK stores the attributes and the color binding above
    //                  flips to transparent BEFORE the platform window
    //                  exists, so the surface is alpha-capable from frame 1.
    //                  (Return value is meaningless here — QWK always
    //                  returns true when merely storing.)
    //   post-show    — the ONLY timing QWK's own examples exercise: the
    //                  calls now run windowAttributeChanged against the live
    //                  HWND (DwmSetWindowAttribute DWMWA_SYSTEMBACKDROP_TYPE
    //                  + DwmExtendFrameIntoClientArea on a mapped window).
    //                  A false return here genuinely means "unsupported"
    //                  (e.g. Win10) and micaActive drops back to the palette
    //                  background — self-correcting the pre-show store.
    //   theme-change — 面板内深浅切换后重设 dark-mode（mica 激活则保持）。
    //   mica-change  — 设置页 Mica 开关切换后整体重跑：开→尝试上材质；
    //                  关→QWK "mica"=false 主动撤材质（DWMSBT_AUTO +
    //                  restoreMargins）并回落实色外壳。
    //
    // PRODUCT DECISION (2026-10-15): the Mica material is OPT-IN via
    // themeScheme.micaEnabled (default false). Windows 11 build 26200
    // reported ret=true in both phases yet never rendered the backdrop
    // (Qt Quick + newer Win11 DWM composition, unresolved upstream), and
    // micaActive=true strips the window base color for nothing — a white
    // shell that collapses dark-mode contrast. The QWK return value is
    // therefore NOT trusted as visual evidence: micaActive = enabled && ret,
    // so a disabled (or silently failing) material always keeps the solid
    // Theme.bgColor shell.
    function applyMaterial(phase) {
        if (!root.isWindows) {
            console.warn("MATERIAL_APPLIED=none phase=" + phase
                         + " platform=" + Qt.platform.os
                         + " enabled=" + themeScheme.micaEnabled
                         + " win=" + themeScheme.osVersion())
            return
        }
        // dark-mode: true tells DWM the window is dark — dark titlebar
        // frame, dark-tinted material and dark menu themes
        // (DWMWA_USE_IMMERSIVE_DARK_MODE + SetPreferredAppMode(PAM_AUTO)
        // inside QWK); false is the light counterpart. Follows Theme.dark
        // so the system chrome tracks the panel scheme.
        const darkRet = windowAgent.setWindowAttribute("dark-mode", Theme.dark)
        // "mica": true = DWMSBT_MAINWINDOW (22H2+) / DWMWA_MICA_EFFECT
        // (22000-22621) + extend frame margins; false actively REVERTS the
        // material (_DWMSBT_AUTO + restoreMargins) — always written, so the
        // runtime off-switch restores the solid shell and a stale attribute
        // from a previous phase cannot linger.
        const micaEnabled = themeScheme.micaEnabled
        const micaRet = windowAgent.setWindowAttribute("mica", micaEnabled)
        root.micaActive = micaEnabled && micaRet
        const state = !micaEnabled ? "mica-disabled"
                    : (micaRet ? "mica" : "mica-unsupported")
        console.warn("MATERIAL_APPLIED=" + state + " phase=" + phase
                     + " enabled=" + micaEnabled
                     + " ret=" + micaRet
                     + " dark-mode=" + Theme.dark + "/" + darkRet
                     + " win=" + themeScheme.osVersion())
    }

    // OS light/dark switch (also fires when themeScheme calls
    // setColorScheme): re-sync the material tint. Linux is a no-op inside
    // applyMaterial.
    Connections {
        target: Qt.styleHints
        function onColorSchemeChanged(scheme) {
            root.applyMaterial("scheme-change")
        }
    }

    // 面板内深浅切换（SettingsPage 外观卡 → themeScheme.setTheme）。
    Connections {
        target: themeScheme
        function onThemeChanged(theme) {
            root.applyMaterial("theme-change")
        }
    }

    // Mica 开关切换（SettingsPage 外观卡 → themeScheme.micaEnabled）：
    // 重跑整个材质流程——开→尝试上材质；关→QWK 撤材质 + 回落实色外壳。
    Connections {
        target: themeScheme
        function onMicaEnabledChanged(enabled) {
            root.applyMaterial("mica-change")
        }
    }

    WindowAgent {
        id: windowAgent
    }

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
        // 确认开关是唯一的询问入口: 开 → 无论退出还是最小化都先弹三钮确认
        // (最小化到托盘 / 退出 / 取消); 关 → 按关闭行为直接执行。
        if (panelConfig.confirmOnExit) {
            exitConfirmDialog.open()
            close.accepted = false
        } else if (panelConfig.closeAction === "minimize") {
            root.hide()
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
    // itself by hairlines instead of translucent overlays). Under Mica
    // (Windows) both layers go transparent so the system material shows
    // through; Linux keeps the opaque palette chain. ──────────────────────
    Rectangle {
        id: canvasLayer
        z: 0
        anchors.fill: parent
        color: root.micaActive ? "transparent" : Theme.bgColor
    }

    // ── Window resize edges: OWNED BY QWK NOW. The previous hand-rolled
    // ResizeEdge strips (8 MouseAreas + startSystemResize) and the titlebar
    // DragHandler/TapHandler are replaced by the WindowAgent's hit-test:
    // identical 8px borders on Linux, native borders + Snap Layout on
    // Windows. ───────────────────────────────────────────────────────────

    // ── Titlebar (Fluent spec: 32px band + hairline; QWK drag region) ────
    Rectangle {
        id: titleBar
        z: 10
        height: 32
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        color: "transparent"

        // The QWK drag region: every press/double-click/right-click inside
        // this item (minus the system buttons registered below in root's
        // Component.onCompleted) is intercepted before QML sees it — no
        // DragHandler/TapHandler here.

        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: Theme.hairlineColor
        }

        // Window icon area — registered as the WindowIcon system button:
        // native behavior (click = system menu, double-click = close on
        // Windows), excluded from the drag region.
        Rectangle {
            id: logoMark
            width: 16; height: 16; radius: 4
            x: 16; y: (parent.height - height) / 2
            opacity: root.active ? 1 : 0.55
            color: assetManager.logoUrl.length > 0
                   ? "transparent"
                   : Theme.accentColor
            PanelLogoGlyph {
                anchors.centerIn: parent
                size: 16
            }
        }
        Text {
            x: 42
            y: (parent.height - height) / 2
            opacity: root.active ? 1 : 0.55
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
            // Win11: caption content fades on an inactive window
            opacity: root.active ? 1 : 0.55

            TitleBarButton {
                id: minBtn
                kind: "min"
                onActivated: root.showMinimized()
            }
            TitleBarButton {
                id: maxBtn
                kind: root.visibility === Window.Maximized
                      ? "restore" : "max"
                onActivated: root.visibility === Window.Maximized
                           ? root.showNormal() : root.showMaximized()
            }
            TitleBarButton {
                id: closeBtn
                kind: "close"
                isClose: true
                onActivated: root.close()
            }
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

        // 1px hairline on the pane's right edge — the nav pane and the
        // content area share the same window background, so without a
        // divider the two zones merge into one white field in light scheme
        // (obs QA 2026-10-15). hairlineColor is an 8% text wash: a quiet
        // line on light AND dark surfaces alike.
        Rectangle {
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.right: parent.right
            width: 1
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

                // ── Nav information architecture (Fluent group rhythm) ──
                // 实例 (detail + per-instance switcher) / 资源 (assets
                // quartet) / 插件 (dynamic). Group headers keep the
                // FluPaneItemHeader cell rhythm: a compact cell with the
                // caption anchored low, so groups sit tight to their first
                // item. The former 页面 header was removed (2026-10-15 nav
                // budget cut): its group held only 主页, and a headerless
                // first item is the conventional IA for the primary
                // destination — 主页 now leads the list directly.
                NavItem {
                    itemKey: "welcome"
                    iconName: "home"; label: qsTr("主页")
                    match: navPane.searchFilter
                }

                NavGroupLabel { text: qsTr("实例") }

                NavItem {
                    itemKey: "instance"
                    iconName: "cat"; label: qsTr("实例详情")
                    // S3: roster count read goes through rosterModel (the
                    // pet::IInstanceApi read path); the page itself and all
                    // create/delete flows still use instanceManager.
                    badgeCount: rosterModel.count
                    match: navPane.searchFilter
                }
                // S3 dogfooding: the roster LIST is RosterApiModel — the
                // read path over pet::IInstanceApi (same shared
                // InstanceApiImpl the plugin SDK serves). Roles are
                // byte-identical to the old instanceManager binding, so the
                // delegate is unchanged. Click still routes through
                // selectInstance(row, uuid) → instanceManager.instanceAt
                // (write/selection paths deliberately stay on the manager
                // until S2); row indexes line up because both models share
                // the manager's roster order. The switcher lives INSIDE the
                // 实例 group now (was its own 宠物·实例切换 header); the
                // page-conditional visibility is unchanged.
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

                NavGroupLabel { text: qsTr("资源") }

                NavItem {
                    itemKey: "monitor"
                    iconName: "chart"; label: qsTr("资源监控")
                    match: navPane.searchFilter
                }
                NavItem {
                    itemKey: "voicepack"
                    iconName: "mic"; label: qsTr("语音包")
                    badgeCount: voicePacks.packCount
                    match: navPane.searchFilter
                }
                NavItem {
                    itemKey: "models"
                    iconName: "cube"; label: qsTr("模型库")
                    badgeCount: modelLibrary.modelCount
                    match: navPane.searchFilter
                }
                NavItem {
                    itemKey: "assets"
                    iconName: "folder"; label: qsTr("资源管理")
                    badgeCount: assetManager.assetCount()
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
        danger: true
        title: qsTr("关闭面板")
        message: qsTr("关闭面板时你想…（退出会停止所有运行中的实例）")
        positiveText: qsTr("退出")
        neutralText: qsTr("最小化到托盘")
        onNeutralClicked: root.hide()
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
        // Legacy "ask" close-action normalization (2026-10-15): the
        // 每次询问 option was folded into the 退出前确认 switch — a
        // persisted "ask" maps to 直接退出 + confirm ON, which is
        // behavior-equivalent (close previously asked). One-shot: the
        // panelConfig writes go through the whitelisted settings bridge
        // and the kv never holds "ask" again.
        if (panelConfig.closeAction === "ask") {
            panelConfig.closeAction = "exit"
            panelConfig.confirmOnExit = true
            console.warn("CLOSE_ACTION_MIGRATED ask -> exit + confirmOnExit=true")
        }
        // QWindowKit wiring — kept in ONE place, in this exact order:
        // QML's Component.onCompleted finalization order between the root
        // and its children is UNSPECIFIED, but the agent's context only
        // exists after setup(), so every registration is sequenced here
        // after setup() deterministically (per-item onCompleted hooks would
        // race the root on some Qt versions).
        //
        // setup() must also run BEFORE the platform window is created (the
        // show() below) — QWK applies the per-platform strategy at winId
        // time, so the frame never flashes decorated.
        windowAgent.setup(root)
        windowAgent.setTitleBar(titleBar)
        windowAgent.setSystemButton(WindowAgent.WindowIcon, logoMark)
        windowAgent.setSystemButton(WindowAgent.Minimize, minBtn)
        windowAgent.setSystemButton(WindowAgent.Maximize, maxBtn)
        windowAgent.setSystemButton(WindowAgent.Close, closeBtn)
        root.applyMaterial("pre-show")
        root.show()
        // Phase 2 runs AFTER show(): the platform window exists by now, so
        // QWK's windowAttributeChanged executes its DWM sequence directly —
        // the proven path from the QWK examples (their menus can only fire
        // on a visible window).
        root.applyMaterial("post-show")
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

    // Caption glyphs: the AppIcon SVG set ("windowmin/max/restore/close" —
    // Segoe Fluent Icons native geometry, see the path comments in AppIcon)
    // at 10px display size with the native 1px optical stroke (2.4 on the
    // 24-grid) — deliberately NOT the Segoe Fluent Icons font, which does
    // not exist on Linux (tofu) and is not shipped with the app. Win11
    // metrics: full-bleed 46×<bar> hover targets, red close hover
    // (Theme.errorColor + readable-on-red glyph).
    component TitleBarButton : Rectangle {
        id: btn
        property string kind: "min"   // min | max | restore | close
        property bool isClose: false
        signal activated()
        width: 46
        height: parent.height
        color: _hover.hovered
               ? (isClose ? Theme.errorColor
                          : Qt.alpha(Theme.textColor, 0.06))
               : "transparent"

        AppIcon {
            anchors.centerIn: parent
            name: "window" + btn.kind
            size: 10
            strokeWidth: 2.4
            color: btn.isClose && _hover.hovered
                   ? Theme.accentTextColor   // readable X on the red fill
                   : Theme.text2Color
        }
        HoverHandler { id: _hover }
        TapHandler { onTapped: btn.activated() }
    }

    // Fluent nav group header — the FluPaneItemHeader recipe from
    // FluNavigationView: the label cell keeps the caption pushed toward its
    // BOTTOM (tight to the group's first item). 2026-10-15 nav budget cut:
    // the cell went from a full nav-row-tall 32px to a compact 24px (text
    // bottom-anchored with the same 4px clearance below, so the 沉底 rhythm
    // survives and only the air ABOVE the caption shrinks) — three headers
    // plus the removed 页面 header save ~56px of standing nav height.
    // 12px semibold in the muted text tone (FluTextStyle caption weight,
    // our token colors).
    component NavGroupLabel : Item {
        id: grp
        property string text: ""
        height: visible ? 24 : 0
        width: parent.width
        Text {
            x: 12
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 4
            text: grp.text
            color: Theme.text3Color
            font.pixelSize: 12
            font.weight: Font.DemiBold
        }
    }

    component NavItem : Item {
        id: item
        property string itemKey: ""
        property string iconName: ""
        property string label: ""
        // Count badge (FluBadge geometry, accent-tinted variant — see the
        // pill below): int count, 0 auto-hides (FluBadge showZero=false),
        // caps at "99+" (FluBadge max=99). Read-only bindings only — the
        // roster/pack/model/asset counts come from the shared read APIs.
        property int badgeCount: 0
        property string match: ""

        readonly property bool active: navPane.activeKey === itemKey
        readonly property bool hidden: match.length > 0
            && label.toLowerCase().indexOf(match) === -1

        // FluNavigationView cellHeight as pinned by the prototype (34; Flu
        // upstream default is 38 — the prototype overrode to the denser
        // Fluent NavigationView spec value and that is the look we match).
        height: hidden ? 0 : 34
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
            // FluBadge pill geometry (width = text + 12, height 20, fully
            // rounded r10) with an ACCENT-TINTED fill instead of Flu's
            // alert-red solid: a translucent accent wash + accent-derived
            // label keeps four count badges calm in both schemes. 0 hides.
            Rectangle {
                visible: item.badgeCount > 0
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                width: badgeTextItem.implicitWidth + 12
                height: 20
                radius: 10
                color: Qt.alpha(Theme.accentColor, Theme.dark ? 0.30 : 0.16)
                Text {
                    id: badgeTextItem
                    anchors.centerIn: parent
                    text: item.badgeCount > 99 ? "99+" : item.badgeCount
                    color: Theme.dark ? Qt.lighter(Theme.accentColor, 1.35)
                                      : Qt.darker(Theme.accentColor, 1.2)
                    font.pixelSize: 12
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

        // same Flu cellHeight as NavItem — roster switcher rows keep the
        // identical density
        height: hidden ? 0 : 34
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
