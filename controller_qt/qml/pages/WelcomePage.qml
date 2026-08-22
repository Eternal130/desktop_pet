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
        const uuid = instanceManager.createInstance(name, avatar)
        if (uuid !== "") {
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
            color: Theme.accentColor
            anchors.horizontalCenter: parent.horizontalCenter
            Text { anchors.centerIn: parent; text: "🐾"; font.pixelSize: 34 }
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
            topPadding: 28

            // ── Header ─────────────────────────────────────────────────
            Item {
                width: parent.width
                height: 40
                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("主页")
                    color: Theme.textColor
                    font.pixelSize: 24
                    font.weight: Font.DemiBold
                }
                Row {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8
                    AppButton {
                        style: "subtle"
                        text: qsTr("⤓ 导入配置")
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: Qt.openUrlExternally(
                            "file:///" + voicePacks.voicePackDir())
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

                            Row {
                                width: parent.width
                                spacing: 14
                                opacity: model.status === "stopped" ? 0.75 : 1.0

                                Rectangle {
                                    width: 64; height: 64; radius: 8
                                    anchors.verticalCenter: parent.verticalCenter
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
                                        text: model.avatar
                                        font.pixelSize: 28
                                    }
                                }

                                Column {
                                    width: parent.width - 78
                                    spacing: 5
                                    anchors.verticalCenter: parent.verticalCenter

                                    Row {
                                        spacing: 8
                                        Text {
                                            text: label
                                            color: Theme.textColor
                                            font.pixelSize: 14
                                            font.weight: Font.DemiBold
                                        }
                                        StatusPill { status: model.status }
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
                                            text: model.status === "running"
                                                  ? qsTr("⏸ 停止") : qsTr("▶ 启动")
                                            implicitHeight: 26
                                            fontSize: 12
                                            onClicked: {
                                                if (!_inst) return
                                                if (model.status === "running") _inst.stop()
                                                else _inst.start()
                                            }
                                        }
                                        AppButton {
                                            style: "subtle"
                                            text: qsTr("↻ 重启")
                                            implicitHeight: 26
                                            fontSize: 12
                                            enabled: model.status === "running"
                                            onClicked: if (_inst) _inst.restart()
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
                                color: "#fff"; font.pixelSize: 12
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
