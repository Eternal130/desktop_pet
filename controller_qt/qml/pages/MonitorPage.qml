import QtQuick
import QtQuick.Controls
import QtCharts
import DesktopPet

// Monitor page — Phase 5 Wave 8 todo 18.
//
// 2×3 grid of live LineSeries charts fed by InstanceSession::monitorModel()
// (a MonitorDataModel with a 60-sample ring buffer). Each tick (2s QTimer in
// InstanceSession) appends a controller sample + sends get_stats; the renderer
// responds via stats_state → the model's mergeRenderer → snapshotAppended →
// this page refreshes the 6 series + the value labels + the stale banner.
//
// GPU/VRAM cells render "—" when null (Linux Stub returns null gpu_percent /
// vram_used_bytes). The stale banner appears when no renderer update has
// arrived within 10s (5 missed polls).
//
// Visual language mirrors InstanceDetailPage: Theme-bound palette, 32px outer
// margins, 10px-radius surface panels, muted text at 0.6 alpha.
Rectangle {
    id: root
    color: Theme.bgColor

    property var instance: null
    property var monitorModel: null

    readonly property color _mutedColor: Qt.rgba(
        Theme.textColor.r, Theme.textColor.g, Theme.textColor.b, 0.6)
    readonly property color _faintColor: Qt.rgba(
        Theme.textColor.r, Theme.textColor.g, Theme.textColor.b, 0.35)
    readonly property color _warnColor: "#f9e2af"   // Mocha "yellow"
    readonly property color _accent2:   "#89b4fa"   // Mocha "blue" (chart line)
    readonly property color _accent3:   "#a6e3a1"   // Mocha "green"
    readonly property color _accent4:   "#fab387"   // Mocha "peach"
    readonly property color _accent5:   "#f5c2e7"   // Mocha "pink"
    readonly property color _accent6:   "#94e2d5"   // Mocha "teal"
    readonly property color _accent7:   "#cba6f7"   // Mocha "mauve"

    // ── formatBytes / formatPercent helpers (ported from Java) ─────────────
    // bytes can be -1 (null sentinel) → returns "—".
    function formatBytes(b) {
        if (b < 0) return "—"
        if (b <= 0) return "0 B"
        const units = ["B", "KB", "MB", "GB", "TB"]
        let digit = Math.floor(Math.log(b) / Math.log(1024))
        if (digit > units.length - 1) digit = units.length - 1
        const v = b / Math.pow(1024, digit)
        return v.toFixed(1) + " " + units[digit]
    }
    function formatPercent(p) {
        if (p < 0) return "—"
        return p.toFixed(1) + "%"
    }

    // ── Empty state (pre-sidebar-selection) ────────────────────────────────
    Text {
        anchors.centerIn: parent
        visible: instance === null
        text: qsTr("Select an instance from the sidebar")
        color: _mutedColor
        font.pixelSize: 14
    }

    // ── Header ─────────────────────────────────────────────────────────────
    Text {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: 24
        anchors.leftMargin: 32
        visible: instance !== null
        text: qsTr("Resource Monitor")
        color: Theme.textColor
        font.pixelSize: 22
        font.weight: Font.DemiBold
    }
    Text {
        id: subhead
        anchors.top: header.bottom
        anchors.topMargin: 4
        anchors.left: header.left
        visible: instance !== null
        text: instance ? qsTr("Polling every 2s · %1 samples max").arg(60) : ""
        color: _mutedColor
        font.pixelSize: 12
    }

    // ── Stale banner (visible when no renderer update in 10s) ──────────────
    Rectangle {
        id: staleBanner
        anchors.top: subhead.bottom
        anchors.topMargin: 12
        anchors.left: header.left
        anchors.right: parent.right
        anchors.rightMargin: 32
        height: 28
        visible: instance !== null && monitorModel && monitorModel.isStaleNow()
        radius: 6
        color: Qt.rgba(_warnColor.r, _warnColor.g, _warnColor.b, 0.18)
        border.color: _warnColor
        border.width: 1
        Text {
            anchors.centerIn: parent
            text: qsTr("⚠ Renderer data stale (no stats_state in >10s)")
            color: _warnColor
            font.pixelSize: 12
            font.weight: Font.Medium
        }
    }

    // ── Charts grid (2 columns × 3 rows = 6 LineSeries) ────────────────────
    Flickable {
        anchors.top: staleBanner.visible ? staleBanner.bottom : subhead.bottom
        anchors.topMargin: 16
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: 32
        anchors.rightMargin: 32
        anchors.bottomMargin: 24
        visible: instance !== null
        contentWidth: width
        contentHeight: grid.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Grid {
            id: grid
            columns: 2
            spacing: 12
            width: parent.width

            // Left column: ctrlCpu, ctrlMem, rendCpu
            ChartCard {
                width: (grid.width - 12) / 2
                titleText: qsTr("Controller CPU")
                valueText: monitorModel ? formatPercent(monitorModel.latestControllerCpuPercent()) : "—"
                lineColor: _accent2
                series: monitorModel ? monitorModel.controllerCpuSeries() : []
                valueFormat: "percent"
            }
            ChartCard {
                width: (grid.width - 12) / 2
                titleText: qsTr("Renderer Memory (RSS)")
                valueText: monitorModel ? formatBytes(monitorModel.latestRendererRssBytes()) : "—"
                lineColor: _accent5
                series: monitorModel ? monitorModel.rendererRssSeries() : []
                valueFormat: "bytes"
            }
            ChartCard {
                width: (grid.width - 12) / 2
                titleText: qsTr("Controller Memory (RSS)")
                valueText: monitorModel ? formatBytes(monitorModel.latestControllerRssBytes()) : "—"
                lineColor: _accent3
                series: monitorModel ? monitorModel.controllerRssSeries() : []
                valueFormat: "bytes"
            }
            ChartCard {
                width: (grid.width - 12) / 2
                titleText: qsTr("Renderer GPU")
                valueText: monitorModel ? formatPercent(monitorModel.latestRendererGpuPercent()) : "—"
                subText: monitorModel && monitorModel.latestRendererGpuName().length > 0
                    ? monitorModel.latestRendererGpuName() : ""
                lineColor: _accent6
                series: monitorModel ? monitorModel.rendererGpuSeries() : []
                valueFormat: "percent"
            }
            ChartCard {
                width: (grid.width - 12) / 2
                titleText: qsTr("Renderer CPU")
                valueText: monitorModel ? formatPercent(monitorModel.latestRendererCpuPercent()) : "—"
                lineColor: _accent4
                series: monitorModel ? monitorModel.rendererCpuSeries() : []
                valueFormat: "percent"
            }
            ChartCard {
                width: (grid.width - 12) / 2
                titleText: qsTr("Renderer VRAM Used")
                valueText: monitorModel ? formatBytes(monitorModel.latestRendererVramUsedBytes()) : "—"
                subText: (monitorModel && monitorModel.latestRendererVramTotalBytes() >= 0)
                    ? qsTr("of %1").arg(formatBytes(monitorModel.latestRendererVramTotalBytes()))
                    : ""
                lineColor: _accent7
                series: monitorModel ? monitorModel.rendererVramSeries() : []
                valueFormat: "bytes"
            }
        }
    }

    // ── Refresh on every snapshot append + on instance swap ────────────────
    // The bound properties above re-evaluate automatically when snapshotAppended
    // fires (because they read monitorModel.X() which mutates). The 6 ChartCards
    // each bind `series` to a fresh QVariantList on every signal; the ChartView
    // inside replaces its LineSeries points from that list in onSeriesChanged.
    Connections {
        target: root.monitorModel
        ignoreUnknownSignals: true
        // Force re-evaluation by touching a property the ChartCards bind to.
        // (Qt's binding engine already re-evaluates on signal-of-dependency;
        // this empty handler is a no-op safety net in case a future refactor
        // breaks the auto-binding chain.)
        function onSnapshotAppended() { /* bindings auto-refresh */ }
        function onHistoryCleared()  { /* bindings auto-refresh */ }
    }

    // Re-bind monitorModel + clear chart series when the user switches
    // instances via the sidebar.
    Connections {
        target: root
        function onInstanceChanged() {
            if (instance) {
                root.monitorModel = instance.monitorModel()
            } else {
                root.monitorModel = null
            }
        }
    }
    Component.onCompleted: {
        if (instance) root.monitorModel = instance.monitorModel()
    }

    // ── Inline ChartCard component ─────────────────────────────────────────
    // Each card shows: title (top-left) + current value (top-right) + an
    // optional sub-line (e.g. GPU name / "of 8 GB") + a ChartView. The chart
    // is a sparkline-style LineSeries (axes hidden, no legend, no gridlines)
    // per the Java reference's configureChart().
    component ChartCard : Rectangle {
        id: card
        property string titleText: ""
        property string valueText: ""
        property string subText: ""
        property color lineColor: Theme.accentColor
        property var series: []
        property string valueFormat: "percent"
        height: 160
        radius: 10
        color: Theme.surfaceColor

        Text {
            id: title
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.topMargin: 12
            anchors.leftMargin: 14
            text: card.titleText
            color: _mutedColor
            font.pixelSize: 11
            font.weight: Font.Medium
            font.capitalization: Font.AllUppercase
        }
        Text {
            id: value
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.topMargin: 10
            anchors.rightMargin: 14
            text: card.valueText
            color: Theme.textColor
            font.pixelSize: 18
            font.weight: Font.DemiBold
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
            font.pixelSize: 10
        }

        ChartView {
            id: chartView
            anchors.top: title.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.topMargin: 4
            anchors.margins: 0
            antialiasing: true
            legend.visible: false
            backgroundColor: Theme.surfaceColor
            // Drop the built-in chart padding so the line fills the card.
            margins.top: 0
            margins.bottom: 0
            margins.left: 0
            margins.right: 0

            ValueAxis {
                id: axisX
                visible: false
                min: 0
                max: Math.max(1, card.series.length)
            }
            ValueAxis {
                id: axisY
                visible: false
                // Auto-range. The Java reference uses autoRanging=true too;
                // percent charts settle near 0..100, bytes charts scale to
                // the working set. min pinned at 0 so the line bottom-anchors.
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

        // Rebuild the LineSeries points whenever the bound series list changes.
        // Each chart card reads its slice of the model on snapshotAppended (the
        // binding re-evaluates because series is a function call on monitorModel).
        onSeriesChanged: rebuildPoints()
        Component.onCompleted: rebuildPoints()

        function rebuildPoints() {
            line.removePoints(0, line.count)
            const n = card.series.length
            if (n === 0) return
            for (let i = 0; i < n; ++i) {
                line.append(i, card.series[i])
            }
            axisX.min = 0
            axisX.max = Math.max(1, n - 1)
        }
    }

    // Auto-range helper: returns the Y-axis max for a given series. Percent
    // charts clamp to a fixed 0..100 scale (CPU/GPU); bytes charts auto-scale
    // to the max sample (rounded up to the next "nice" power of 1024).
    function computeYMax(series, fmt) {
        if (series.length === 0) return 1.0
        let m = 0
        for (let i = 0; i < series.length; ++i) {
            const v = series[i]
            if (v > m) m = v
        }
        if (m <= 0) return 1.0
        if (fmt === "percent") return Math.max(10, Math.ceil(m / 10) * 10)
        // bytes — round up to next power-of-1024 step
        const step = Math.pow(1024, Math.floor(Math.log(m) / Math.log(1024)))
        return Math.ceil(m / step) * step
    }
}
