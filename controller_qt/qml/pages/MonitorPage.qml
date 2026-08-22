import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCharts
import FluentUI
import DesktopPet

// Monitor page — Fluent UI redesign (design doc §③).
//
// Changes vs the previous version:
//   - Instance selector row at the top (FluToggleButton group bound to
//     InstanceManager; selection rebinds monitorModel).
//   - 6 chart cards in a 3×2 grid (wide) / 2-col (narrow), each with
//     current value + peak + trend arrow computed from the 60-sample ring.
//   - Stale banner restyled as a Fluent InfoBar (isStaleNow unchanged).
//   - Crash-recovery status card: restartAttempts is not exposed to QML
//     (core-side gap), so the card renders session status/connected —
//     honest presentation instead of fabricated numbers.
//
// Data pipeline unchanged: MonitorDataModel ring buffer + snapshotAppended.
Rectangle {
    id: root
    color: "transparent"

    property var instance: null
    property var monitorModel: null
    property int selectedRow: 0

    Component.onCompleted: {
        root._selectInstance(0)
    }

    function _selectInstance(row) {
        if (!instanceManager.instanceAt) return
        const inst = instanceManager.instanceAt(row)
        if (!inst) return
        selectedRow = row
        instance = inst
        monitorModel = inst.monitorModel()
    }

    readonly property color _mutedColor: Theme.mutedTextColor
    readonly property color _faintColor: Qt.rgba(
        Theme.textColor.r, Theme.textColor.g, Theme.textColor.b, 0.35)
    readonly property color _warnColor: Theme.warningColor

    function formatBytes(b) {
        if (b < 0) return "—"
        if (b <= 0) return "0 B"
        const units = ["B", "KB", "MB", "GB", "TB"]
        let digit = Math.floor(Math.log(b) / Math.log(1024))
        if (digit > units.length - 1) digit = units.length - 1
        return (b / Math.pow(1024, digit)).toFixed(1) + " " + units[digit]
    }
    function formatPercent(p) {
        if (p < 0) return "—"
        return p.toFixed(1) + "%"
    }
    // Peak / trend from the ring buffer series (pure QML-side reduction).
    function seriesPeak(series) {
        let m = -Infinity
        for (let i = 0; i < series.length; ++i)
            if (series[i] > m) m = series[i]
        return m === -Infinity ? 0 : m
    }
    function seriesTrend(series) {
        const n = series.length
        if (n < 4) return 0
        const recent = series[n-1] - series[n-4]
        return recent
    }
    function trendText(t, fmt) {
        if (!isFinite(t) || t === 0) return "→ 0"
        const v = fmt === "bytes" ? formatBytes(Math.abs(t)) : Math.abs(t).toFixed(1)
        return t > 0 ? "↗ +" + v : "↘ −" + v
    }

    Text {
        anchors.centerIn: parent
        visible: instance === null
        text: qsTr("No instance yet — create one from the Home page")
        color: _mutedColor
        font: FluTextStyle.Body
    }

    Flickable {
        anchors.fill: parent
        visible: instance !== null
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: width
        contentHeight: contentCol.implicitHeight + 48
        ScrollBar.vertical: FluScrollBar {}

        Column {
            id: contentCol
            width: root.width - 2 * Theme.spacePage
            x: Theme.spacePage
            spacing: Theme.spaceGroup
            topPadding: 28

            // ── Header + instance selector ────────────────────────────────
            RowLayout {
                width: parent.width
                spacing: 12
                FluText {
                    text: qsTr("资源监控")
                    font: FluTextStyle.Title
                }

                Row {
                    spacing: 4
                    visible: instanceManager.rowCount() > 1
                    Repeater {
                        model: instanceManager
                        delegate: FluToggleButton {
                            text: label
                            checked: root.selectedRow === index
                            onClicked: root._selectInstance(index)
                        }
                    }
                }

                Item { Layout.fillWidth: true }
                StatusPill {
                    status: instance ? instance.status : "stopped"
                    Layout.alignment: Qt.AlignVCenter
                }
            }
            FluText {
                text: qsTr("每 2 秒轮询 · 环形缓冲保留 60 个样本（约 2 分钟历史）")
                color: _mutedColor
                font: FluTextStyle.Caption
            }

            // ── Stale InfoBar ─────────────────────────────────────────────
            Rectangle {
                width: parent.width
                height: 32
                visible: instance !== null && monitorModel
                         && monitorModel.isStaleNow()
                radius: Theme.radiusMd
                color: Qt.rgba(_warnColor.r, _warnColor.g, _warnColor.b, 0.14)
                border.color: Qt.rgba(_warnColor.r, _warnColor.g,
                                      _warnColor.b, 0.5)
                border.width: 1
                Text {
                    anchors.centerIn: parent
                    text: qsTr("⚠ 渲染器数据陈旧 — 超过 10 秒未收到 stats_state，请检查渲染进程")
                    color: _warnColor
                    font: FluTextStyle.Caption
                }
            }

            // ── Charts grid 3×2 ───────────────────────────────────────────
            GridLayout {
                width: parent.width
                columns: root.width > 860 ? 3 : 2
                columnSpacing: Theme.spaceGroup
                rowSpacing: Theme.spaceGroup

                ChartCard {
                    Layout.fillWidth: true
                    titleText: qsTr("控制器 CPU")
                    valueText: monitorModel ? formatPercent(monitorModel.latestControllerCpuPercent()) : "—"
                    lineColor: Theme.chartColors[1]
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
                    lineColor: Theme.chartColors[5]
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
                    lineColor: Theme.chartColors[4]
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
                    lineColor: Theme.chartColors[0]
                    series: monitorModel ? monitorModel.rendererVramSeries() : []
                    valueFormat: "bytes"
                }
            }

            // ── Crash-recovery status card ────────────────────────────────
            SectionCard {
                width: parent.width
                title: qsTr("崩溃恢复状态 · RestartController")

                Row {
                    spacing: 12
                    FluText {
                        anchors.verticalCenter: parent.verticalCenter
                        text: instance && instance.connected
                              ? qsTr("连接正常 · 崩溃恢复未触发")
                              : qsTr("渲染器未连接")
                        color: instance && instance.connected
                               ? Theme.successColor : Theme.mutedTextColor
                        font: FluTextStyle.Body
                    }
                }
                FluText {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    color: root._faintColor
                    font: FluTextStyle.Caption
                    text: qsTr("重启尝试计数（0–5 · 指数退避 2s→30s）尚未暴露到 QML，" +
                               "此处展示会话连接状态。")
                }
            }
        }
    }

    Connections {
        target: root.monitorModel
        ignoreUnknownSignals: true
        function onSnapshotAppended() { /* bindings auto-refresh */ }
        function onHistoryCleared()  { /* bindings auto-refresh */ }
    }

    Connections {
        target: root
        function onInstanceChanged() {
            if (instance) root.monitorModel = instance.monitorModel()
            else root.monitorModel = null
        }
    }

    // ── Inline ChartCard component ─────────────────────────────────────────
    component ChartCard : FluFrame {
        id: card
        property string titleText: ""
        property string valueText: ""
        property string subText: ""
        property color lineColor: Theme.accentColor
        property var series: []
        property string valueFormat: "percent"
        // FluFrame (Rectangle-based) has NO implicit size — inside GridLayout
        // the row would collapse to 0 and stack all cards. Give it an
        // implicit height (layouts use implicitHeight for cell sizing).
        implicitHeight: 170
        height: 170
        padding: 14

        Text {
            id: title
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.topMargin: 12
            anchors.leftMargin: 14
            text: card.titleText
            color: _mutedColor
            font.capitalization: Font.AllUppercase
            font.family: FluTextStyle.BodyStrong.family
            font.pixelSize: FluTextStyle.BodyStrong.pixelSize
            font.weight: FluTextStyle.BodyStrong.weight
        }
        Text {
            id: value
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.topMargin: 10
            anchors.rightMargin: 14
            text: card.valueText
            color: Theme.textColor
            font: FluTextStyle.Subtitle
        }
        Text {
            id: sub
            anchors.top: value.bottom
            anchors.topMargin: 2
            anchors.right: parent.right
            anchors.rightMargin: 14
            visible: text.length > 0
            text: card.subText
            color: _faintColor
            font: FluTextStyle.Caption
        }

        ChartView {
            id: chartView
            anchors.top: title.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: legendRow.top
            anchors.topMargin: 4
            anchors.bottomMargin: 2
            antialiasing: true
            legend.visible: false
            backgroundColor: "transparent"
            margins.top: 0; margins.bottom: 0
            margins.left: 0; margins.right: 0

            ValueAxis {
                id: axisX
                visible: false
                min: 0
                max: Math.max(1, card.series.length)
            }
            ValueAxis {
                id: axisY
                visible: false
                min: 0
                max: computeYMax(card.series, card.valueFormat)
            }

            LineSeries {
                id: line
                color: card.lineColor
                width: 2
                style: Qt.SolidLine
                axisX: axisX
                axisY: axisY
                useOpenGL: false
                pointsVisible: false
            }
        }

        // Peak + trend footer row.
        Row {
            id: legendRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            anchors.bottomMargin: 10
            spacing: 12

            Text {
                text: qsTr("峰值 ") + (card.valueFormat === "percent"
                    ? formatPercent(seriesPeak(card.series))
                    : formatBytes(seriesPeak(card.series)))
                color: _faintColor
                font.pixelSize: 11
            }
            Text {
                text: trendText(seriesTrend(card.series), card.valueFormat)
                color: _faintColor
                font.pixelSize: 11
            }
        }

        onSeriesChanged: rebuildPoints()
        Component.onCompleted: rebuildPoints()
        Layout.preferredHeight: 170

        function rebuildPoints() {
            line.removePoints(0, line.count)
            const n = card.series.length
            if (n === 0) return
            for (let i = 0; i < n; ++i)
                line.append(i, card.series[i])
            axisX.min = 0
            axisX.max = Math.max(1, n - 1)
        }
    }

    function computeYMax(series, fmt) {
        if (series.length === 0) return 1.0
        let m = 0
        for (let i = 0; i < series.length; ++i)
            if (series[i] > m) m = series[i]
        if (m <= 0) return 1.0
        if (fmt === "percent") return Math.max(10, Math.ceil(m / 10) * 10)
        const step = Math.pow(1024, Math.floor(Math.log(m) / Math.log(1024)))
        return Math.ceil(m / step) * step
    }
}
