import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DesktopPet

// Monitor page (design doc §3). Instance selector + 3x2 sparkline cards +
// InfoBar stale banner + crash-recovery card with REAL restartAttempts.
// Canvas sparklines replace QtCharts; same MonitorDataModel pipeline.
Rectangle {
    id: root
    color: "transparent"

    property var instance: null
    property var monitorModel: null
    property int selectedRow: 0

    Component.onCompleted: root._selectInstance(0)

    function _selectInstance(row) {
        if (!instanceManager.instanceAt) return
        const inst = instanceManager.instanceAt(row)
        if (!inst) return
        selectedRow = row
        instance = inst
        monitorModel = inst.monitorModel()
    }

    readonly property color _mutedColor: Theme.text2Color
    readonly property color _faintColor: Theme.text3Color
    readonly property color _warnColor: Theme.warningColor

    function formatBytes(b) {
        if (b < 0) return "—"
        if (b <= 0) return "0 B"
        const units = ["B", "KB", "MB", "GB", "TB"]
        let d = Math.floor(Math.log(b) / Math.log(1024))
        if (d > units.length - 1) d = units.length - 1
        return (b / Math.pow(1024, d)).toFixed(1) + " " + units[d]
    }
    function formatPercent(p) {
        if (p < 0) return "—"
        return p.toFixed(1) + "%"
    }
    function seriesPeak(s) {
        let m = -Infinity
        for (let i = 0; i < s.length; ++i)
            if (s[i] > m) m = s[i]
        return m === -Infinity ? 0 : m
    }
    function seriesTrend(s) {
        const n = s.length
        if (n < 4) return 0
        return s[n-1] - s[n-4]
    }
    function trendText(t, fmt) {
        if (!isFinite(t) || t === 0) return "→ 0"
        const v = fmt === "bytes" ? formatBytes(Math.abs(t))
                                  : Math.abs(t).toFixed(1)
        return t > 0 ? "↗ +" + v : "↘ −" + v
    }

    property real _tick: 0
    Timer { interval: 2000; running: true; repeat: true; onTriggered: root._tick++ }

    Text {
        anchors.centerIn: parent
        visible: instance === null
        text: qsTr("尚未创建实例 — 请在主页创建")
        color: _mutedColor
        font.pixelSize: 13
    }

    Flickable {
        anchors.fill: parent
        visible: instance !== null
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

            // ── Header + instance selector ────────────────────────────
            Item {
                width: parent.width
                height: 40
                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("资源监控")
                    color: Theme.textColor
                    font.pixelSize: 24
                    font.weight: Font.DemiBold
                }
                Row {
                    anchors.left: parent.left
                    anchors.leftMargin: 130
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4
                    visible: instanceManager.rowCount() > 1
                    Repeater {
                        model: instanceManager
                        delegate: Rectangle {
                            readonly property bool sel: root.selectedRow === index
                            width: instLabel.implicitWidth + 24
                            height: 28
                            radius: Theme.radiusMd
                            color: sel ? Theme.accentAlpha(0.14) : "transparent"
                            border.width: 1
                            border.color: sel ? Theme.accentColor : Theme.borderColor
                            Text {
                                id: instLabel
                                anchors.centerIn: parent
                                text: label
                                color: parent.sel ? Theme.accentColor : Theme.text2Color
                                font.pixelSize: 12
                                font.weight: parent.sel ? Font.DemiBold : Font.Normal
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root._selectInstance(index)
                            }
                        }
                    }
                }
                StatusPill {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    status: instance ? instance.status : "stopped"
                }
            }
            Text {
                text: qsTr("每 2 秒轮询 · 环形缓冲保留 60 个样本（约 2 分钟历史）")
                color: _mutedColor
                font.pixelSize: 13
            }

            // ── Stale InfoBar ─────────────────────────────────────────
            Rectangle {
                width: parent.width
                height: 34
                visible: instance !== null && monitorModel
                         && root._tick >= 0 && monitorModel.isStaleNow()
                radius: Theme.radiusMd
                color: Theme.warningBgColor
                border.color: Theme.withAlpha(Theme.warningColor, 0.3)
                border.width: 1
                Text {
                    anchors.centerIn: parent
                    text: qsTr("⚠ 渲染器数据陈旧 — 超过 10 秒未收到 stats_state" +
                               "，请检查渲染进程")
                    color: _warnColor
                    font.pixelSize: 12
                }
            }

            // ── 3x2 chart grid ─────────────────────────────────────────
            GridLayout {
                width: parent.width
                columns: root.width > 860 ? 3 : 2
                columnSpacing: Theme.spaceGroup
                rowSpacing: Theme.spaceGroup

                ChartCard {
                    Layout.fillWidth: true
                    titleText: qsTr("控制器 CPU")
                    valueText: monitorModel ? formatPercent(monitorModel.latestControllerCpuPercent()) : "—"
                    lineColor: Theme.chartColors[0]
                    series: monitorModel ? monitorModel.controllerCpuSeries() : []
                    valueFormat: "percent"
                }
                ChartCard {
                    Layout.fillWidth: true
                    titleText: qsTr("渲染器 CPU")
                    valueText: monitorModel ? formatPercent(monitorModel.latestRendererCpuPercent()) : "—"
                    lineColor: Theme.chartColors[3]
                    series: monitorModel ? monitorModel.rendererCpuSeries() : []
                    valueFormat: "percent"
                }
                ChartCard {
                    Layout.fillWidth: true
                    titleText: qsTr("渲染器 GPU")
                    valueText: monitorModel ? formatPercent(monitorModel.latestRendererGpuPercent()) : "—"
                    subText: monitorModel && monitorModel.latestRendererGpuName().length > 0
                        ? monitorModel.latestRendererGpuName() : ""
                    lineColor: Theme.chartColors[4]
                    series: monitorModel ? monitorModel.rendererGpuSeries() : []
                    valueFormat: "percent"
                }
                ChartCard {
                    Layout.fillWidth: true
                    titleText: qsTr("控制器内存 RSS")
                    valueText: monitorModel ? formatBytes(monitorModel.latestControllerRssBytes()) : "—"
                    lineColor: Theme.chartColors[2]
                    series: monitorModel ? monitorModel.controllerRssSeries() : []
                    valueFormat: "bytes"
                }
                ChartCard {
                    Layout.fillWidth: true
                    titleText: qsTr("渲染器内存 RSS")
                    valueText: monitorModel ? formatBytes(monitorModel.latestRendererRssBytes()) : "—"
                    lineColor: Theme.chartColors[1]
                    series: monitorModel ? monitorModel.rendererRssSeries() : []
                    valueFormat: "bytes"
                }
                ChartCard {
                    Layout.fillWidth: true
                    titleText: qsTr("渲染器 VRAM")
                    valueText: monitorModel ? formatBytes(monitorModel.latestRendererVramUsedBytes()) : "—"
                    subText: (monitorModel && monitorModel.latestRendererVramTotalBytes() >= 0)
                        ? qsTr("共 %1").arg(formatBytes(monitorModel.latestRendererVramTotalBytes()))
                        : ""
                    lineColor: Theme.chartColors[5]
                    series: monitorModel ? monitorModel.rendererVramSeries() : []
                    valueFormat: "bytes"
                }
            }

            // ── Crash-recovery card ────────────────────────────────────
            Card {
                width: parent.width
                title: qsTr("崩溃恢复状态 · RestartController")

                StatusPill {
                    status: instance && instance.connected ? "running" : "stopped"
                    label: instance && instance.connected
                        ? qsTr("稳定运行") : qsTr("渲染器未连接")
                }

                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    color: root._faintColor
                    font.pixelSize: 11
                    text: (instance
                           ? qsTr("重启尝试 %1/5 · 指数退避 2s→30s").arg(
                                 instance.restartAttempts())
                           : "") +
                          qsTr(" · layout_state 事件正常同步")
                }
            }
        }
    }

    Connections {
        target: root.monitorModel
        ignoreUnknownSignals: true
        function onSnapshotAppended() { root._tick++ }
        function onHistoryCleared()  { root._tick++ }
    }

    Connections {
        target: root
        function onInstanceChanged() {
            if (instance) root.monitorModel = instance.monitorModel()
            else root.monitorModel = null
        }
    }

    // ── Inline ChartCard ────────────────────────────────────────────────
    component ChartCard : Card {
        id: card
        property string titleText: ""
        property string valueText: ""
        property string subText: ""
        property color lineColor: Theme.accentColor
        property var series: []
        property string valueFormat: "percent"
        padding: 16
        implicitHeight: 160
        Layout.preferredHeight: 160

        Item {
            width: parent.width
            height: 20
            Text {
                anchors.left: parent.left
                text: card.titleText
                color: _mutedColor
                font.pixelSize: 11
                font.capitalization: Font.AllUppercase
                font.weight: Font.DemiBold
            }
            Text {
                anchors.right: parent.right
                visible: card.subText.length === 0
                text: card.valueText
                color: Theme.textColor
                font.pixelSize: 18
                font.weight: Font.DemiBold
            }
        }

        Sparkline {
            width: parent.width
            height: 56
            series: card.series
            lineColor: card.lineColor
        }

        Row {
            width: parent.width
            spacing: 12
            Text {
                text: qsTr("峰值 ") + (card.valueFormat === "percent"
                    ? formatPercent(seriesPeak(card.series))
                    : formatBytes(seriesPeak(card.series)))
                color: _faintColor
                font.pixelSize: 11
            }
            Rectangle { width: 10; height: 10; radius: 2; color: card.lineColor
                anchors.verticalCenter: parent.verticalCenter }
            Text {
                text: trendText(seriesTrend(card.series), card.valueFormat)
                color: _faintColor
                font.pixelSize: 11
            }
            Text {
                visible: card.subText.length > 0
                text: card.valueText + " · " + card.subText
                color: _faintColor
                font.pixelSize: 11
            }
        }
    }
}
