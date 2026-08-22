import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtQuick.Layouts
import FluentUI
import DesktopPet

// Welcome / home page — Fluent UI redesign (dashboard layout).
//
// Three zones (design doc §①):
//   1. Metrics strip  — running instance count, aggregated controller/
//      renderer CPU+RSS, WS connection health.
//   2. Instance grid  — one card per instance (status pill, model/fps/
//      volume summary, quick start/stop/restart, "manage" route).
//   3. Env summary    — collapsed green row when all checks pass; auto-
//      expands to the full 6-item list when any check fails.
//
// Data sources unchanged: envChecker context property, InstanceManager
// model roles (label/modelName/status/connected/uuid), instance.*
// session properties, createInstance → selectInstance → switchPage flow.
Rectangle {
    id: root
    color: "transparent"

    readonly property color _mutedColor: Theme.mutedTextColor
    readonly property color _faintColor: Qt.rgba(
        Theme.textColor.r, Theme.textColor.g, Theme.textColor.b, 0.35)

    // Aggregation: iterate live sessions for metrics strip values.
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
    function _fmtBytes(b) {
        if (b < 0) return "—"
        if (b <= 0) return "0 B"
        const units = ["B", "KB", "MB", "GB", "TB"]
        let d = Math.floor(Math.log(b) / Math.log(1024))
        if (d > units.length - 1) d = units.length - 1
        return (b / Math.pow(1024, d)).toFixed(1) + " " + units[d]
    }
    function _fmtPct(p) { return p < 0 ? "—" : p.toFixed(1) + "%" }

    // Re-evaluate metric bindings when instances mutate.
    property real _tick: 0
    Connections {
        target: instanceManager
        ignoreUnknownSignals: true
        function onDataChanged() { root._tick++ }
        function onRowsInserted() { root._tick++ }
        function onRowsRemoved() { root._tick++ }
    }
    Timer { interval: 2000; running: true; repeat: true; onTriggered: root._tick++ }

    // ── Empty state (no instances at all) ──────────────────────────────────
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
        FluText {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Desktop Pet Controller")
            font: FluTextStyle.Title
        }
        FluText {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Create your first pet instance to get started")
            color: _mutedColor
            font: FluTextStyle.Caption
        }
        FluFilledButton {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Create First Instance")
            implicitWidth: 220; implicitHeight: 40
            onClicked: root._createInstance()
        }
    }

    function _createInstance() {
        const label = qsTr("Pet %1").arg(instanceManager.rowCount() + 1)
        const uuid = instanceManager.createInstance(label)
        if (uuid !== "") {
            const row = instanceManager.rowCount() - 1
            Window.window.selectInstance(
                row, instanceManager.instanceAt(row).instanceId)
            Window.window.switchPage("instance")
        }
    }

    Flickable {
        anchors.fill: parent
        visible: instanceManager.rowCount() > 0
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentHeight: contentCol.implicitHeight + 48
        ScrollBar.vertical: FluScrollBar {}

        Column {
            id: contentCol
            width: root.width - 2 * Theme.spacePage
            x: Theme.spacePage
            spacing: Theme.spaceGroup
            topPadding: 28

            // ── Header ─────────────────────────────────────────────────────
            RowLayout {
                width: parent.width
                spacing: 12
                FluText {
                    text: qsTr("主页")
                    font: FluTextStyle.Title
                    Layout.fillWidth: true
                }
                FluFilledButton {
                    text: qsTr("＋ 创建实例")
                    onClicked: root._createInstance()
                }
            }
            FluText {
                text: qsTr("管理所有桌面宠物实例 · 上次会话已自动恢复")
                color: _mutedColor
                font: FluTextStyle.Caption
            }

            // ── Metrics strip ──────────────────────────────────────────────
            RowLayout {
                width: parent.width
                spacing: Theme.spaceGroup

                MetricCard {
                    Layout.fillWidth: true
                    titleText: qsTr("运行中实例")
                    valueText: root._tick >= 0
                        ? (root._runningCount() + " / " + instanceManager.rowCount())
                        : ""
                }
                MetricCard {
                    Layout.fillWidth: true
                    titleText: qsTr("控制器 CPU / 内存")
                    valueText: root._tick >= 0
                        ? (root._fmtPct(root._ctrlCpu()) + " · " + root._fmtBytes(root._ctrlRss()))
                        : ""
                }
                MetricCard {
                    Layout.fillWidth: true
                    titleText: qsTr("渲染器 CPU / 内存")
                    valueText: root._tick >= 0
                        ? (root._fmtPct(root._aggRendererCpu()) + " · " + root._fmtBytes(root._aggRendererRss()))
                        : ""
                }
                MetricCard {
                    Layout.fillWidth: true
                    titleText: qsTr("WS 连接")
                    valueText: root._tick >= 0 ? (root._connectedCount() + " ✓") : ""
                    valueColor: root._connectedCount() > 0
                        ? Theme.successColor : Theme.mutedTextColor
                }
            }

            // ── Instance grid ──────────────────────────────────────────────
            FluText {
                text: qsTr("我的宠物")
                font: FluTextStyle.BodyStrong
            }
            GridLayout {
                width: parent.width
                columns: width > 860 ? 3 : 2
                columnSpacing: Theme.spaceGroup
                rowSpacing: Theme.spaceGroup

                Repeater {
                    model: instanceManager
                    delegate: FluFrame {
                        Layout.fillWidth: true
                        // FluFrame has no implicit size — without an explicit
                        // preferred height the GridLayout row collapses to 0
                        // and cards stack/overlap (verified via screenshot QA).
                        Layout.preferredHeight: 140
                        padding: Theme.spaceCard

                        readonly property var _inst: instanceManager.instanceAt(index)

                        Row {
                            width: parent.width
                            spacing: 14

                            Rectangle {
                                width: 56; height: 56; radius: 10
                                anchors.verticalCenter: parent.verticalCenter
                                color: Qt.rgba(Theme.accentColor.r,
                                               Theme.accentColor.g,
                                               Theme.accentColor.b, 0.14)
                                Text {
                                    anchors.centerIn: parent
                                    text: "🐾"; font.pixelSize: 26
                                }
                            }

                            Column {
                                width: parent.width - 70
                                spacing: 6

                                RowLayout {
                                    width: parent.width
                                    spacing: 8
                                    FluText {
                                        text: label
                                        font: FluTextStyle.BodyStrong
                                        Layout.fillWidth: true
                                    }
                                    StatusPill {
                                        status: model.status
                                        Layout.alignment: Qt.AlignVCenter
                                    }
                                }
                                FluText {
                                    width: parent.width
                                    text: model.modelName.length > 0
                                        ? model.modelName
                                        : qsTr("未加载模型")
                                    color: root._faintColor
                                    font: FluTextStyle.Caption
                                }
                                Row {
                                    spacing: 8
                                    FluButton {
                                        text: model.status === "running"
                                              ? qsTr("⏸ 停止") : qsTr("▶ 启动")
                                        padding: 6
                                        font.pixelSize: 12
                                        onClicked: {
                                            if (!_inst) return
                                            if (model.status === "running")
                                                _inst.stop()
                                            else
                                                _inst.start()
                                        }
                                    }
                                    FluButton {
                                        text: qsTr("↻ 重启")
                                        padding: 6
                                        font.pixelSize: 12
                                        enabled: model.status === "running"
                                        onClicked: if (_inst) _inst.restart()
                                    }
                                    FluTextButton {
                                        text: qsTr("管理 →")
                                        font.pixelSize: 12
                                        onClicked:
                                            Window.window.selectInstance(
                                                index, model.uuid)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ── Environment summary (collapsible) ──────────────────────────
            FluFrame {
                width: parent.width
                padding: Theme.spaceCard

                Column {
                    width: parent.width
                    spacing: envExpanded ? 14 : 0

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
                            FluText {
                                anchors.verticalCenter: parent.verticalCenter
                                text: envChecker.allReady
                                    ? qsTr("环境检查全部通过")
                                    : qsTr("部分环境检查未通过")
                                font: FluTextStyle.BodyStrong
                                color: envChecker.allReady
                                    ? Theme.successColor : Theme.errorColor
                            }
                        }

                        Row {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 8
                            FluTextButton {
                                text: qsTr("重新检测")
                                font.pixelSize: 12
                                onClicked: envChecker.runChecks()
                            }
                            FluTextButton {
                                text: envExpanded ? qsTr("收起 ▴") : qsTr("展开详情 ▾")
                                font.pixelSize: 12
                                onClicked: envExpanded = !envExpanded
                            }
                        }
                    }

                    // Full check list — only when expanded. Auto-expand on
                    // failure (binding, not one-shot: a later re-check that
                    // fails re-opens the list).
                    Column {
                        visible: envExpanded
                        width: parent.width
                        spacing: 10

                        CheckRow {
                            width: parent.width
                            ready: envChecker.openglRendererReady
                            label: qsTr("OpenGL Renderer")
                            detail: envChecker.openglRendererReady
                                    ? qsTr("Found: ") + envChecker.openglRendererPath
                                    : qsTr("Not found")
                        }
                        CheckRow {
                            width: parent.width
                            ready: envChecker.vulkanRendererReady
                            label: qsTr("Vulkan Renderer")
                            detail: envChecker.vulkanRendererReady
                                    ? qsTr("Found: ") + envChecker.vulkanRendererPath
                                    : qsTr("Not found")
                        }
                        CheckRow {
                            width: parent.width
                            ready: envChecker.qtRuntimeReady
                            label: qsTr("Qt Runtime")
                            detail: qsTr("Qt ") + envChecker.qtVersion
                        }
                        CheckRow {
                            width: parent.width
                            ready: envChecker.resourcesReady
                            label: qsTr("Resources")
                            detail: envChecker.resourcesReady
                                    ? qsTr("Found: ") + envChecker.modelsDirectory
                                    : qsTr("Not found")
                        }
                        CheckRow {
                            width: parent.width
                            ready: envChecker.modelsAvailable
                            label: qsTr("Models")
                            detail: envChecker.modelsAvailable
                                    ? qsTr("%1 available: %2")
                                      .arg(envChecker.availableModels.length)
                                      .arg(envChecker.availableModels.join(", "))
                                    : qsTr("No models found")
                        }
                        CheckRow {
                            width: parent.width
                            ready: envChecker.portBindable
                            label: qsTr("WS Port %1").arg(envChecker.port)
                            detail: envChecker.portBindable
                                    ? qsTr("Bindable (free)")
                                    : qsTr("In use — another process is listening")
                        }
                    }
                }

                property bool envExpanded: !envChecker.allReady
            }
        }
    }

    // ── Inline components ──────────────────────────────────────────────────
    component MetricCard : FluFrame {
        id: card
        property string titleText: ""
        property string valueText: ""
        property color valueColor: Theme.textColor
        padding: 16
        Column {
            width: parent.width
            spacing: 4
            Text {
                text: card.titleText
                color: Theme.mutedTextColor
                font.pixelSize: 11
                font.capitalization: Font.AllUppercase
                font.weight: Font.DemiBold
            }
            Text {
                width: parent.width
                text: card.valueText
                color: card.valueColor
                font.pixelSize: 19
                font.weight: Font.DemiBold
            }
        }
    }

    component CheckRow : Rectangle {
        id: rowItem
        property bool ready: false
        property string label: ""
        property string detail: ""

        color: "transparent"
        height: Math.max(dot.height, rowCol.implicitHeight)

        Rectangle {
            id: dot
            width: 10; height: 10; radius: 5
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.topMargin: 4
            color: rowItem.ready ? root._okColor : root._errColor
        }

        Column {
            id: rowCol
            anchors.left: dot.right
            anchors.leftMargin: 12
            anchors.right: parent.right

            FluText { text: rowItem.label; font: FluTextStyle.BodyStrong }
            FluText {
                text: rowItem.detail
                color: root._mutedColor
                font: FluTextStyle.Caption
                wrapMode: Text.WordWrap
                width: rowCol.width
            }
        }
    }

    readonly property color _okColor: Theme.successColor
    readonly property color _errColor: Theme.errorColor

    Component.onCompleted: envChecker.runChecks()
}
