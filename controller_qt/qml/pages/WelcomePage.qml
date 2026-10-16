import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import DesktopPet

// Home / dashboard (design doc §1). All bindings preserved: envChecker,
// instanceManager roles, createInstance -> selectInstance -> switchPage flow,
// monitorModel aggregation.
Rectangle {
    id: root
    color: "transparent"

    readonly property color _mutedColor: Theme.text2Color
    readonly property color _faintColor: Theme.text3Color

    readonly property var _thumbGradients: [
        ["#cfd2f5", "#a7abe8"], ["#f8d3d8", "#eaa8b4"],
        ["#d9f0d9", "#a8d0a8"], ["#f5ecd0", "#e0c98a"],
        ["#d8e8f5", "#a8c4e0"], ["#ece0f5", "#c4a8e0"]
    ]

    function _runningCount() {
        let n = 0
        for (let i = 0; i < instanceManager.rowCount(); ++i) {
            const s = instanceManager.instanceAt(i)
            if (s && s.status === "running") ++n
        }
        return n
    }
    function _connectedCount() {
        let n = 0
        for (let i = 0; i < instanceManager.rowCount(); ++i) {
            const s = instanceManager.instanceAt(i)
            if (s && s.connected) ++n
        }
        return n
    }
    function _aggRendererCpu() {
        let v = 0
        for (let i = 0; i < instanceManager.rowCount(); ++i) {
            const s = instanceManager.instanceAt(i)
            const m = s ? s.monitorModel() : null
            if (m) v += m.latestRendererCpuPercent()
        }
        return v < 0 ? -1 : v
    }
    function _aggRendererRss() {
        let v = 0, any = false
        for (let i = 0; i < instanceManager.rowCount(); ++i) {
            const s = instanceManager.instanceAt(i)
            const m = s ? s.monitorModel() : null
            if (m) {
                const b = m.latestRendererRssBytes()
                if (b >= 0) { v += b; any = true }
            }
        }
        return any ? v : -1
    }
    function _ctrlCpu() {
        const s = instanceManager.instanceAt(0)
        const m = s ? s.monitorModel() : null
        return m ? m.latestControllerCpuPercent() : -1
    }
    function _ctrlRss() {
        const s = instanceManager.instanceAt(0)
        const m = s ? s.monitorModel() : null
        return m ? m.latestControllerRssBytes() : -1
    }
    function _ctrlCpuSeries() {
        const s = instanceManager.instanceAt(0)
        const m = s ? s.monitorModel() : null
        return m ? m.controllerCpuSeries() : []
    }
    function _rendererCpuSeries() {
        const s = instanceManager.instanceAt(0)
        const m = s ? s.monitorModel() : null
        return m ? m.rendererCpuSeries() : []
    }
    function _fmtBytes(b) {
        if (b < 0) return "—"
        if (b <= 0) return "0 B"
        const units = ["B", "KB", "MB", "GB", "TB"]
        let d = Math.floor(Math.log(b) / Math.log(1024))
        if (d > units.length - 1) d = units.length - 1
        return (b / Math.pow(1024, d)).toFixed(1) + " " + units[d]
    }
    function _fmtPct(p) { return p < 0 ? "—" : p.toFixed(1) + "%" }

    property real _tick: 0
    Connections {
        target: instanceManager
        ignoreUnknownSignals: true
        function onDataChanged() { root._tick++ }
        function onRowsInserted() { root._tick++ }
        function onRowsRemoved() { root._tick++ }
    }
    Timer { interval: 2000; running: true; repeat: true; onTriggered: root._tick++ }

    function _createInstance() { createDialog.open() }

    function _finishCreate(name, avatar) {
        // S5: creation through the rosterModel write bridge (shared
        // IInstanceControlApi; int error code, 0 = Ok).
        const err = rosterModel.createInstance(name, avatar)
        if (err === 0) {
            const row = instanceManager.rowCount() - 1
            Window.window.selectInstance(
                row, instanceManager.instanceAt(row).instanceId)
            Window.window.switchPage("instance")
        }
    }

    // ── Empty state ───────────────────────────────────────────────────
    Column {
        anchors.centerIn: parent
        visible: instanceManager.rowCount() === 0
        spacing: 14

        Rectangle {
            width: 72; height: 72; radius: 36
            color: assetManager.logoUrl.length > 0
                   ? Theme.offBgColor
                   : Theme.accentColor
            border.width: assetManager.logoUrl.length > 0 ? 1 : 0
            border.color: Theme.borderColor
            anchors.horizontalCenter: parent.horizontalCenter
            Image {
                anchors.fill: parent
                anchors.margins: 8
                source: assetManager.logoUrl
                fillMode: Image.PreserveAspectFit
                visible: assetManager.logoUrl.length > 0
                         && status === Image.Ready
            }
            // vector paw mark (was the 🐾 emoji — tofu on emoji-font-less
            // hosts); accentTextColor keeps it on the accent disc
            AppIcon {
                anchors.centerIn: parent
                visible: assetManager.logoUrl.length === 0
                name: "paw"
                size: 40
                color: Theme.accentTextColor
            }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Desktop Pet Controller")
            color: Theme.textColor
            font.pixelSize: 20
            font.weight: Font.DemiBold
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("创建你的第一个宠物实例，开始使用")
            color: _mutedColor
            font.pixelSize: 13
        }
        AppButton {
            anchors.horizontalCenter: parent.horizontalCenter
            style: "primary"
            text: qsTr("＋ 创建第一个实例")
            implicitWidth: 220; implicitHeight: 40
            onClicked: root._createInstance()
        }
    }

    Flickable {
        anchors.fill: parent
        visible: instanceManager.rowCount() > 0
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentHeight: contentCol.implicitHeight + 48
        ScrollBar.vertical: AppScrollBar {}

        Column {
            id: contentCol
            width: root.width - 2 * Theme.spacePage
            x: Theme.spacePage
            spacing: Theme.spaceGroup
            topPadding: Theme.spacePage

            // ── Header ─────────────────────────────────────────────────
            Item {
                width: parent.width
                height: 40
                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("主页")
                    color: Theme.textColor
                    font.pixelSize: Theme.fontTitle
                    font.weight: Font.DemiBold
                }
                Row {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8
                    AppButton {
                        style: "subtle"
                        text: qsTr("全部启动")
                        anchors.verticalCenter: parent.verticalCenter
                        // P2-16 batch ops — contract functions live in
                        // Main.qml (roster read + per-uuid instanceControl
                        // writes); guard keeps the page usable standalone.
                        onClicked: {
                            const w = Window.window
                            if (w && w.startAllInstances) w.startAllInstances()
                        }
                    }
                    AppButton {
                        style: "subtle"
                        text: qsTr("全部停止")
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: {
                            const w = Window.window
                            if (w && w.stopAllInstances) w.stopAllInstances()
                        }
                    }
                    AppButton {
                        style: "subtle"
                        iconName: "folderopen"
                        text: qsTr("打开语音包目录")
                        anchors.verticalCenter: parent.verticalCenter
                        // Same C++ entry as VoicePackPage's header button
                        // (mkpath + QDesktopServices + fromLocalFile).
                        // String-concatenating "file:///" + absolute path
                        // yields four slashes on Linux → invalid URL →
                        // silent no-op.
                        onClicked: voicePacks.openVoicePackDir()
                    }
                    AppButton {
                        style: "primary"
                        text: qsTr("＋ 创建实例")
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: root._createInstance()
                    }
                }
            }
            Text {
                text: qsTr("管理所有桌面宠物实例 · 上次会话已自动恢复")
                color: _mutedColor
                font.pixelSize: 13
            }

            // ── Metrics strip with sparklines ──────────────────────────
            Row {
                width: parent.width
                spacing: Theme.spaceGroup

                MetricCard {
                    width: (parent.width - 3 * Theme.spaceGroup) / 4
                    titleText: qsTr("运行中实例")
                    valueText: root._tick >= 0
                        ? (root._runningCount() + " / " + instanceManager.rowCount())
                        : ""
                    sparkVisible: false
                }
                MetricCard {
                    width: (parent.width - 3 * Theme.spaceGroup) / 4
                    titleText: qsTr("控制器 CPU / 内存")
                    valueText: root._tick >= 0
                        ? (root._fmtPct(root._ctrlCpu()) + " · " + root._fmtBytes(root._ctrlRss()))
                        : ""
                    sparkSeries: root._ctrlCpuSeries()
                    sparkColor: Theme.chartColors[0]
                }
                MetricCard {
                    width: (parent.width - 3 * Theme.spaceGroup) / 4
                    titleText: qsTr("渲染器 CPU / 内存")
                    valueText: root._tick >= 0
                        ? (root._fmtPct(root._aggRendererCpu()) + " · " + root._fmtBytes(root._aggRendererRss()))
                        : ""
                    sparkSeries: root._rendererCpuSeries()
                    sparkColor: Theme.chartColors[3]
                }
                MetricCard {
                    width: (parent.width - 3 * Theme.spaceGroup) / 4
                    titleText: qsTr("WS 连接")
                    valueText: root._tick >= 0
                        ? (root._connectedCount() + " ✓") : ""
                    valueColor: root._connectedCount() > 0
                        ? Theme.successColor : Theme.text2Color
                    deltaText: root._connectedCount() > 0
                        ? qsTr("127.0.0.1:9001 正常") : qsTr("无连接")
                    sparkVisible: false
                }
            }

            // ── Instance card grid ─────────────────────────────────────
            Item {
                width: parent.width
                height: 32
                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("我的宠物")
                    color: Theme.textColor
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                }
                AppButton {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    style: "primary"
                    text: qsTr("＋ 创建实例")
                    onClicked: root._createInstance()
                }
            }
            GridLayout {
                width: parent.width
                columns: width > 860 ? 3 : 2
                columnSpacing: Theme.spaceGroup
                rowSpacing: Theme.spaceGroup

                Repeater {
                    model: instanceManager
                    delegate: Card {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 150
                        padding: 16

                        // TapHandler (not a MouseArea): handlers are not
                        // positioner children, so this cannot trip the
                        // "Column child with anchors" collapse again, and it
                        // does not compete with the inner AppButtons for
                        // grabs the way a full-card MouseArea layer did.
                        TapHandler {
                            cursorShape: Qt.PointingHandCursor
                            onTapped: Window.window.selectInstance(
                                index, model.uuid)
                        }

                        readonly property var _inst: instanceManager.instanceAt(index)

                        // custom icon for the card avatar — same resolution
                        // idiom as the detail-page header avatarButton
                        // (assetManager context property, keyed by uuid).
                        // Empty = no custom icon → the row-index gradient
                        // disc + glyph fallback stays.
                        readonly property string _iconUrl: model.uuid.length > 0
                            ? assetManager.instanceIconUrl(model.uuid) : ""

                            Row {
                                width: parent.width
                                spacing: 14
                                opacity: model.status === "stopped" ? 0.75 : 1.0

                                // avatar slot (BubbleCard rule): custom icon
                                // image when set — transparent backgrounds
                                // show the card surface, no backing — else
                                // the row-index gradient disc + glyph.
                                // Exactly one of the two Row children is
                                // visible; Row skips the hidden one, so
                                // geometry is identical in both states.
                                Rectangle {
                                    width: 64; height: 64; radius: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: _iconUrl.length === 0
                                    gradient: Gradient {
                                        GradientStop {
                                            position: 0
                                            color: root._thumbGradients[
                                                index % root._thumbGradients.length][0]
                                        }
                                        GradientStop {
                                            position: 1
                                            color: root._thumbGradients[
                                                index % root._thumbGradients.length][1]
                                        }
                                    }
                                    Text {
                                        anchors.centerIn: parent
                                        // avatar DATA (emoji string from the
                                        // create dialog) renders through the
                                        // AppIcon emoji→vector map; unknown
                                        // strings fall back to plain text
                                        AppIcon {
                                            anchors.centerIn: parent
                                            glyph: model.avatar
                                            size: 30
                                            color: Theme.textColor
                                        }
                                    }
                                }
                                Image {
                                    width: 64; height: 64
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: _iconUrl.length > 0
                                    source: _iconUrl
                                    fillMode: Image.PreserveAspectFit
                                    asynchronous: true
                                }

                                Column {
                                    width: parent.width - 78
                                    spacing: 5
                                    anchors.verticalCenter: parent.verticalCenter

                                    Row {
                                        id: nameRow
                                        width: parent.width
                                        spacing: 8
                                        Text {
                                            // P2-8: long names elide instead
                                            // of pushing the StatusPill out
                                            // of the card. The plugin-scope
                                            // badge (when visible) reserves
                                            // its own slot the same way —
                                            // host cards keep today's math.
                                            text: label
                                            color: Theme.textColor
                                            font.pixelSize: 14
                                            font.weight: Font.DemiBold
                                            elide: Text.ElideRight
                                            width: Math.min(
                                                implicitWidth,
                                                nameRow.width - namePill.width
                                                    - nameRow.spacing
                                                    - (scopeBadge.visible
                                                       ? scopeBadge.width
                                                         + nameRow.spacing
                                                       : 0))
                                        }
                                        StatusPill {
                                            id: namePill
                                            status: model.status
                                        }
                                        // v1.13 "打标记不隐藏": plugin-summoned
                                        // instances keep their card (fully
                                        // functional) plus this marker; the
                                        // tooltip reveals the owning plugin id.
                                        PluginScopeBadge {
                                            id: scopeBadge
                                            pluginId: model.pluginScope
                                            showText: true
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                    }
                                    Text {
                                        width: parent.width
                                        text: {
                                            const parts = []
                                            parts.push(model.modelName.length > 0
                                                ? model.modelName : qsTr("未加载模型"))
                                            const inst = instanceManager.instanceAt(index)
                                            if (inst && inst.targetFps > 0)
                                                parts.push(inst.targetFps + " FPS")
                                            else if (inst)
                                                parts.push(qsTr("自适应 FPS"))
                                            if (inst && inst.muted)
                                                parts.push(qsTr("静音"))
                                            else if (inst)
                                                parts.push(qsTr("音量 ") +
                                                    Math.round(inst.volume * 100) + "%")
                                            return parts.join(" · ")
                                        }
                                        color: root._faintColor
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                    }
                                    Row {
                                        spacing: 8
                                        AppButton {
                                            style: "subtle"
                                            // P1-1: a failed start is
                                            // visible on the card itself —
                                            // the button becomes the retry
                                            // entry (still calls start).
                                            // ▶/⏸ glyph prefixes → one
                                            // vector icon family (⏸ was
                                            // tofu without an emoji font).
                                            iconName: model.status === "error"
                                                      ? ""
                                                      : (model.status === "running"
                                                         ? "stop" : "play")
                                            text: model.status === "running"
                                                  ? qsTr("停止")
                                                  : (model.status === "error"
                                                     ? qsTr("启动失败，点击重试")
                                                     : qsTr("启动"))
                                            iconSize: 12
                                            // P2-12: debounce — no lifecycle
                                            // spam while a transition is in
                                            // flight.
                                            enabled: model.status !== "pending"
                                                     && model.status !== "connecting"
                                            implicitHeight: 26
                                            fontSize: 12
                                            onClicked: {
                                                if (!_inst) return
                                                // S7: lifecycle writes
                                                // through the shared API
                                                // bridge (reads keep the
                                                // live _inst object).
                                                if (model.status === "running")
                                                    instanceControl.stop(_inst.uuid)
                                                else
                                                    instanceControl.start(_inst.uuid)
                                            }
                                        }
                                        AppButton {
                                            style: "subtle"
                                            iconName: "restart"
                                            text: qsTr("重启")
                                            iconSize: 12
                                            implicitHeight: 26
                                            fontSize: 12
                                            // P2-12: running guard +
                                            // transition debounce.
                                            enabled: model.status === "running"
                                                     && model.status !== "pending"
                                                     && model.status !== "connecting"
                                            onClicked: if (_inst)
                                                instanceControl.restart(_inst.uuid)
                                        }
                                        AppButton {
                                            style: "subtle"
                                            text: qsTr("管理 →")
                                            implicitHeight: 26
                                            fontSize: 12
                                            onClicked: Window.window.selectInstance(
                                                index, model.uuid)
                                        }
                                    }
                                }
                            }
                        }
                    }
            }

            // ── Env check summary (collapsible) ────────────────────────
            Card {
                id: envCard
                width: parent.width
                padding: 14
                property bool envExpanded: !envChecker.allReady

                Item {
                    width: parent.width
                    height: 40

                    Row {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 10
                        Rectangle {
                            width: 20; height: 20; radius: 5
                            anchors.verticalCenter: parent.verticalCenter
                            color: envChecker.allReady
                                ? Theme.successColor : Theme.errorColor
                            Text {
                                anchors.centerIn: parent
                                text: envChecker.allReady ? "✓" : "!"
                                // on a success/error fill: window color keeps
                                // contrast in both schemes (dark-green fill +
                                // near-white window text in light; light-green
                                // fill + near-black window text in dark)
                                color: Theme.bgColor
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                            }
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: envChecker.allReady
                                ? qsTr("环境检查全部通过")
                                : qsTr("部分环境检查未通过")
                            color: envChecker.allReady
                                ? Theme.successColor : Theme.errorColor
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                        }
                        Text {
                            visible: envChecker.allReady
                            anchors.verticalCenter: parent.verticalCenter
                            text: "· OpenGL " + (envChecker.openglRendererReady ? "✓" : "✗")
                                + "  Vulkan " + (envChecker.vulkanRendererReady ? "✓" : "✗")
                                + "  " + qsTr("端口 9001 ") + (envChecker.portBindable ? "✓" : "✗")
                            color: root._faintColor
                            font.pixelSize: 12
                        }
                    }

                    Row {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8
                        AppButton {
                            style: "subtle"
                            text: qsTr("重新检测")
                            implicitHeight: 26; fontSize: 12
                            anchors.verticalCenter: parent.verticalCenter
                            onClicked: envChecker.runChecks()
                        }
                        AppButton {
                            style: "subtle"
                            text: envCard.envExpanded
                                  ? qsTr("收起 ▴") : qsTr("展开详情 ▾")
                            implicitHeight: 26; fontSize: 12
                            anchors.verticalCenter: parent.verticalCenter
                            onClicked: envCard.envExpanded = !envCard.envExpanded
                        }
                    }
                }

                Column {
                    width: parent.width
                    visible: envCard.envExpanded
                    spacing: 10

                    CheckRow {
                        width: parent.width
                        ready: envChecker.openglRendererReady
                        label: qsTr("OpenGL 渲染器")
                        detail: envChecker.openglRendererReady
                                ? qsTr("找到: ") + envChecker.openglRendererPath
                                : qsTr("未找到")
                    }
                    CheckRow {
                        width: parent.width
                        ready: envChecker.vulkanRendererReady
                        label: qsTr("Vulkan 渲染器")
                        detail: envChecker.vulkanRendererReady
                                ? qsTr("找到: ") + envChecker.vulkanRendererPath
                                : qsTr("未找到")
                    }
                    CheckRow {
                        width: parent.width
                        ready: envChecker.qtRuntimeReady
                        label: qsTr("Qt 运行时")
                        detail: qsTr("Qt ") + envChecker.qtVersion
                    }
                    CheckRow {
                        width: parent.width
                        ready: envChecker.resourcesReady
                        label: qsTr("资源")
                        detail: envChecker.resourcesReady
                                ? qsTr("找到: ") + envChecker.modelsDirectory
                                : qsTr("未找到")
                    }
                    CheckRow {
                        width: parent.width
                        ready: envChecker.modelsAvailable
                        label: qsTr("模型")
                        detail: envChecker.modelsAvailable
                                ? qsTr("%1 个可用: %2")
                                  .arg(envChecker.availableModels.length)
                                  .arg(envChecker.availableModels.join(", "))
                                : qsTr("无模型")
                    }
                    CheckRow {
                        width: parent.width
                        ready: envChecker.portBindable
                        label: qsTr("WS 端口 %1").arg(envChecker.port)
                        detail: envChecker.portBindable
                                ? qsTr("可绑定（空闲）")
                                : qsTr("被占用")
                    }
                }
            }
        }
    }

    CreateInstanceDialog {
        id: createDialog
        onAccepted: (name, avatar) => root._finishCreate(name, avatar)
    }

    component MetricCard : Card {
        id: card
        property string titleText: ""
        property string valueText: ""
        property string deltaText: ""
        property color valueColor: Theme.textColor
        property var sparkSeries: []
        property color sparkColor: Theme.accentColor
        property bool sparkVisible: true
        padding: 16

        Column {
            width: parent.width
            spacing: 4
            Text {
                text: card.titleText
                color: Theme.text3Color
                font.pixelSize: 11
                font.capitalization: Font.AllUppercase
                font.weight: Font.DemiBold
            }
            Text {
                width: parent.width
                text: card.valueText
                color: card.valueColor
                font.pixelSize: 20
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Sparkline {
                visible: card.sparkVisible
                width: parent.width
                series: card.sparkSeries
                lineColor: card.sparkColor
            }
            Text {
                visible: card.deltaText.length > 0
                text: card.deltaText
                color: card.valueColor === Theme.successColor
                       ? Theme.successColor : Theme.text3Color
                font.pixelSize: 11
            }
        }
    }

    component CheckRow : Item {
        id: rowItem
        property bool ready: false
        property string label: ""
        property string detail: ""

        height: Math.max(dot.height, rowCol.implicitHeight)

        Rectangle {
            id: dot
            width: 10; height: 10; radius: 5
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.topMargin: 4
            color: rowItem.ready ? Theme.successColor : Theme.errorColor
        }

        Column {
            id: rowCol
            anchors.left: dot.right
            anchors.leftMargin: 12
            anchors.right: parent.right

            Text {
                text: rowItem.label
                color: Theme.textColor
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            Text {
                text: rowItem.detail
                color: root._mutedColor
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                width: rowCol.width
            }
        }
    }

    Component.onCompleted: envChecker.runChecks()
}
