package com.desktoppet.ui;

import com.desktoppet.model.ControllerStats;
import com.desktoppet.model.RendererStats;
import javafx.fxml.FXMLLoader;
import javafx.scene.Node;
import javafx.scene.Parent;
import javafx.scene.Scene;
import javafx.scene.chart.LineChart;
import javafx.scene.chart.XYChart;
import javafx.scene.control.Button;
import javafx.scene.control.Label;
import javafx.stage.Stage;
import org.junit.jupiter.api.Test;
import org.testfx.framework.junit5.ApplicationTest;

import java.lang.reflect.Field;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * TestFX + Monocle headless UI tests for the resource-monitor page
 * (resource-monitor plan, T5). Covers the three acceptance scenarios:
 * navigation toggle, snapshot-driven chart updates, and graceful null
 * rendering for unavailable GPU metrics.
 */
public class MonitorPageTest extends ApplicationTest {

    private MainWindowController mainController;
    private MonitorPageController monitorPageController;
    private Node monitorPageNode;

    @Override
    public void start(Stage stage) throws Exception {
        FXMLLoader loader = new FXMLLoader(getClass().getResource("/fxml/main-window.fxml"));
        Parent root = loader.load();
        mainController = loader.getController();
        monitorPageController = (MonitorPageController) getField(mainController, "monitorPageController");
        monitorPageNode = (Node) getField(mainController, "monitorPageNode");
        stage.setScene(new Scene(root, 1200, 760));
        stage.show();
    }

    @Test
    void testOpenMonitorPage() {
        Button monitorBtn = lookup("#monitorButton").queryAs(Button.class);
        assertNotNull(monitorBtn, "📊 monitor button must exist in title bar");
        assertEquals("📊", monitorBtn.getText());

        clickOn("#monitorButton");

        assertTrue(monitorPageNode.isVisible(), "monitor page must be visible after clicking 📊");
        assertTrue(monitorPageNode.isManaged(), "monitor page must be managed after clicking 📊");
    }

    @Test
    void testStatsInjectionUpdatesChart() {
        interact(() -> monitorPageNode.setVisible(true));

        LineChart<Number, Number> chart = lookup("#ctrlCpuChart").queryAs(LineChart.class);
        int before = totalPoints(chart);

        MonitorDataModel.MonitorSnapshot snapshot = new MonitorDataModel.MonitorSnapshot(
                new ControllerStats(42.0, 200_000_000L, 100_000_000L, 500_000_000L, System.currentTimeMillis()),
                null,
                System.currentTimeMillis()
        );
        interact(() -> monitorPageController.refresh(snapshot));

        int after = totalPoints(chart);
        assertTrue(after > before,
                "controller CPU chart must gain a point after refresh (before=" + before + ", after=" + after + ")");

        Label cpuValue = lookup("#ctrlCpuValue").queryAs(Label.class);
        assertTrue(cpuValue.getText().contains("42"),
                "CPU label must reflect injected value: " + cpuValue.getText());
    }

    @Test
    void testNullGpuRenderedAsNa() {
        interact(() -> monitorPageNode.setVisible(true));

        RendererStats rs = new RendererStats(
                10.0, 50_000_000L, null, null, null, null, System.currentTimeMillis());
        MonitorDataModel.MonitorSnapshot snapshot = new MonitorDataModel.MonitorSnapshot(
                null, rs, System.currentTimeMillis());

        interact(() -> monitorPageController.refresh(snapshot));

        Label gpuValue = lookup("#rendGpuValue").queryAs(Label.class);
        Label vramValue = lookup("#rendVramValue").queryAs(Label.class);
        assertTrue(gpuValue.getText().contains("—"),
                "GPU label must render placeholder when gpuPercent is null: " + gpuValue.getText());
        assertTrue(vramValue.getText().contains("—"),
                "VRAM label must render placeholder when vramUsedBytes is null: " + vramValue.getText());
    }

    private static int totalPoints(LineChart<Number, Number> chart) {
        int sum = 0;
        for (XYChart.Series<Number, Number> s : chart.getData()) {
            sum += s.getData().size();
        }
        return sum;
    }

    private static Object getField(Object target, String name) throws Exception {
        Field f = target.getClass().getDeclaredField(name);
        f.setAccessible(true);
        return f.get(target);
    }
}
