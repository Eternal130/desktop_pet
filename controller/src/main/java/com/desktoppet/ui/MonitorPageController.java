package com.desktoppet.ui;

import com.desktoppet.model.ControllerStats;
import com.desktoppet.model.RendererStats;
import javafx.fxml.FXML;
import javafx.scene.chart.LineChart;
import javafx.scene.chart.NumberAxis;
import javafx.scene.chart.XYChart;
import javafx.scene.control.Label;

import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.LinkedHashMap;
import java.util.Locale;
import java.util.Map;

/**
 * Controller for {@code /fxml/monitor-page.fxml} (resource-monitor plan, T5).
 *
 * <p>The page is a passive view of {@link MonitorDataModel}. The
 * {@code monitor-executor} tick and the {@code stats_state} WS handler both
 * hop onto the FX thread inside {@code MainWindowController} and then call
 * {@link #refresh(MonitorDataModel.MonitorSnapshot)} so this controller never
 * touches the network, the executor, or any采集 API directly.
 *
 * <p>Threading: every public method ({@link #refresh}, {@link #markStale},
 * {@link #clear}) must be invoked on the JavaFX Application Thread, matching
 * the {@code Platform.runLater} contract set up by the caller.
 */
public class MonitorPageController {

    /** Maximum number of points retained on each trend chart. Matches {@code MonitorDataModel.HISTORY_CAP}. */
    static final int CHART_CAP = 60;
    /** Glyph shown when a metric is unavailable (Linux stub GPU, missing renderer sample, etc.). */
    static final String NA = "—";

    @FXML private Label lastUpdateLabel;
    @FXML private Label ctrlCpuValue;
    @FXML private Label ctrlMemValue;
    @FXML private Label ctrlHeapUsedValue;
    @FXML private Label ctrlHeapMaxValue;
    @FXML private Label rendCpuValue;
    @FXML private Label rendMemValue;
    @FXML private Label rendGpuValue;
    @FXML private Label rendVramValue;

    @FXML private LineChart<Number, Number> ctrlCpuChart;
    @FXML private LineChart<Number, Number> ctrlMemChart;
    @FXML private LineChart<Number, Number> ctrlHeapUsedChart;
    @FXML private LineChart<Number, Number> ctrlHeapMaxChart;
    @FXML private LineChart<Number, Number> rendCpuChart;
    @FXML private LineChart<Number, Number> rendMemChart;
    @FXML private LineChart<Number, Number> rendGpuChart;
    @FXML private LineChart<Number, Number> rendVramChart;

    private final Map<LineChart<Number, Number>, XYChart.Series<Number, Number>> seriesMap = new LinkedHashMap<>();
    private final SimpleDateFormat clockFmt = new SimpleDateFormat("HH:mm:ss", Locale.ROOT);

    private MainWindowController mainController;
    private long tick = 0;

    public void setMainWindowController(MainWindowController controller) {
        this.mainController = controller;
    }

    @FXML
    private void onBack() {
        if (mainController == null) {
            return;
        }
        mainController.showInstanceDetail();
        mainController.stopMonitorPolling();
    }

    @FXML
    private void initialize() {
        LineChart<Number, Number>[] charts = new LineChart[]{
                ctrlCpuChart, ctrlMemChart, ctrlHeapUsedChart, ctrlHeapMaxChart,
                rendCpuChart, rendMemChart, rendGpuChart, rendVramChart
        };
        for (LineChart<Number, Number> chart : charts) {
            configureChart(chart);
            XYChart.Series<Number, Number> series = new XYChart.Series<>();
            chart.getData().add(series);
            seriesMap.put(chart, series);
        }
    }

    private void configureChart(LineChart<Number, Number> chart) {
        chart.setAnimated(false);
        chart.setCreateSymbols(false);
        chart.setLegendVisible(false);
        chart.setHorizontalGridLinesVisible(false);
        chart.setVerticalGridLinesVisible(false);
        chart.setHorizontalZeroLineVisible(false);
        chart.setVerticalZeroLineVisible(false);
        hideAxis((NumberAxis) chart.getXAxis());
        hideAxis((NumberAxis) chart.getYAxis());
    }

    private void hideAxis(NumberAxis axis) {
        axis.setAutoRanging(true);
        axis.setTickLabelsVisible(false);
        axis.setTickMarkVisible(false);
        axis.setMinorTickVisible(false);
    }

    /**
     * Repaint the page from a single snapshot. Caller ({@code MainWindowController})
     * guarantees FX-thread invocation and a non-null snapshot. Each side
     * (controller / renderer) is rendered independently so partial data still
     * produces a meaningful view (e.g. JVM stats landed, renderer not yet
     * connected).
     */
    public void refresh(MonitorDataModel.MonitorSnapshot snapshot) {
        if (snapshot == null) {
            return;
        }
        tick++;
        long x = tick;

        ControllerStats cs = snapshot.controller();
        if (cs != null) {
            ctrlCpuValue.setText(formatPercent(cs.cpuPercent()));
            ctrlMemValue.setText(formatBytes(cs.rssBytes()));
            ctrlHeapUsedValue.setText(formatBytes(cs.heapUsedBytes()));
            ctrlHeapMaxValue.setText(formatBytes(cs.heapMaxBytes()));
            appendPoint(ctrlCpuChart, x, cs.cpuPercent());
            appendPoint(ctrlMemChart, x, cs.rssBytes());
            appendPoint(ctrlHeapUsedChart, x, cs.heapUsedBytes());
            appendPoint(ctrlHeapMaxChart, x, cs.heapMaxBytes());
        }

        RendererStats rs = snapshot.renderer();
        if (rs != null) {
            rendCpuValue.setText(formatPercent(rs.cpuPercent()));
            rendMemValue.setText(formatBytes(rs.rssBytes()));
            Double gpu = rs.gpuPercent();
            rendGpuValue.setText(gpu != null ? formatPercent(gpu) : NA);
            Long vram = rs.vramUsedBytes();
            rendVramValue.setText(vram != null ? formatBytes(vram) : NA);
            appendPoint(rendCpuChart, x, rs.cpuPercent());
            appendPoint(rendMemChart, x, rs.rssBytes());
            if (gpu != null) {
                appendPoint(rendGpuChart, x, gpu);
            }
            if (vram != null) {
                appendPoint(rendVramChart, x, vram);
            }
        }

        lastUpdateLabel.setText("最后更新: " + clockFmt.format(new Date(snapshot.capturedAtMs())));
        lastUpdateLabel.getStyleClass().removeAll("stale-label");
    }

    /** Flag the current data as stale (no fresh {@code stats_state} within threshold). FX-thread only. */
    public void markStale() {
        lastUpdateLabel.setText("⚠ 数据陈旧");
        if (!lastUpdateLabel.getStyleClass().contains("stale-label")) {
            lastUpdateLabel.getStyleClass().add("stale-label");
        }
    }

    /** Clear all rendered values back to the placeholder. Used on instance switch. FX-thread only. */
    public void clear() {
        tick = 0;
        for (Map.Entry<LineChart<Number, Number>, XYChart.Series<Number, Number>> entry : seriesMap.entrySet()) {
            entry.getValue().getData().clear();
        }
        for (Label l : new Label[]{ctrlCpuValue, ctrlMemValue, ctrlHeapUsedValue, ctrlHeapMaxValue,
                rendCpuValue, rendMemValue, rendGpuValue, rendVramValue}) {
            l.setText(NA);
        }
        lastUpdateLabel.setText("最后更新: —");
        lastUpdateLabel.getStyleClass().removeAll("stale-label");
    }

    private void appendPoint(LineChart<Number, Number> chart, long x, double y) {
        XYChart.Series<Number, Number> series = seriesMap.get(chart);
        if (series == null) {
            return;
        }
        series.getData().add(new XYChart.Data<>(x, y));
        while (series.getData().size() > CHART_CAP) {
            series.getData().removeFirst();
        }
    }

    static String formatPercent(double pct) {
        return String.format(Locale.ROOT, "%.1f%%", pct);
    }

    static String formatBytes(long bytes) {
        if (bytes <= 0) {
            return "0 B";
        }
        String[] units = {"B", "KB", "MB", "GB", "TB"};
        int digit = (int) (Math.log(bytes) / Math.log(1024));
        digit = Math.min(digit, units.length - 1);
        double value = bytes / Math.pow(1024.0, digit);
        return String.format(Locale.ROOT, "%.1f %s", value, units[digit]);
    }
}
