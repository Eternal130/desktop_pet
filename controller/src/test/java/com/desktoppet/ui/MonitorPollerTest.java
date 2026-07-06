package com.desktoppet.ui;

import ch.qos.logback.classic.Level;
import ch.qos.logback.classic.Logger;
import ch.qos.logback.classic.spi.ILoggingEvent;
import ch.qos.logback.core.read.ListAppender;
import com.desktoppet.model.ControllerStats;
import com.desktoppet.model.RendererStats;
import com.desktoppet.network.MessageDispatcher;
import com.desktoppet.network.Protocol;
import com.google.gson.JsonObject;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.slf4j.LoggerFactory;

import java.util.ArrayList;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Unit tests for the resource-monitor polling/event layer (plan T4). Covers
 * MonitorDataModel's merge/clear/stale/history-cap semantics and the
 * MainWindowController wiring that registers the stats_state handler on every
 * per-instance dispatcher. Tests run without the JavaFX toolkit by overriding
 * the controller's {@code fxRunner} to execute synchronously, so the full
 * WS-handler → model-mutation path is exercised end-to-end.
 */
class MonitorPollerTest {

    private Logger dispatcherLogger;
    private ListAppender<ILoggingEvent> appender;

    @BeforeEach
    void attachAppender() {
        dispatcherLogger = (Logger) LoggerFactory.getLogger(MessageDispatcher.class);
        appender = new ListAppender<>();
        appender.start();
        dispatcherLogger.addAppender(appender);
    }

    @AfterEach
    void detachAppender() {
        dispatcherLogger.detachAppender(appender);
        appender.stop();
    }

    @Test
    void testStatsStateUpdatesModel() {
        MainWindowController controller = newMonitorController();
        MessageDispatcher dispatcher = new MessageDispatcher();
        controller.registerStatsStateHandler(dispatcher);

        dispatcher.dispatch(Protocol.createEvent(Protocol.EVENT_STATS_STATE, sampleStatsPayload(50.0)));

        MonitorDataModel.MonitorSnapshot snap = controller.monitorModel.getLatestSnapshot();
        assertNotNull(snap, "snapshot must be created after stats_state");
        assertNotNull(snap.renderer(), "renderer stats must be populated");
        assertEquals(50.0, snap.renderer().cpuPercent());
        assertEquals("NVIDIA RTX", snap.renderer().gpuName());
        assertFalse(controller.monitorModel.isStale(), "fresh stats_state must clear stale");
    }

    @Test
    void testStatsStatePreservesControllerSideAcrossRendererMerges() {
        MainWindowController controller = newMonitorController();
        MonitorDataModel model = controller.monitorModel;

        model.mergeController(new ControllerStats(10.0, 100L, 50L, 500L, 1L));
        RendererStats rs = sampleRendererStats(33.0);
        model.mergeRenderer(rs);

        MonitorDataModel.MonitorSnapshot snap = model.getLatestSnapshot();
        assertNotNull(snap.controller());
        assertEquals(10.0, snap.controller().cpuPercent());
        assertEquals(33.0, snap.renderer().cpuPercent());
    }

    @Test
    void testInstanceSwitchClearsHistory() {
        MainWindowController controller = newMonitorController();
        MonitorDataModel model = controller.monitorModel;

        model.mergeController(new ControllerStats(10, 100, 50, 500, 1L));
        model.mergeRenderer(sampleRendererStats(50.0));
        model.mergeController(new ControllerStats(20, 200, 60, 600, 2L));
        assertFalse(model.getHistory().isEmpty());
        assertNotNull(model.getLatestSnapshot());

        model.clearHistory();

        assertTrue(model.getHistory().isEmpty());
        assertNull(model.getLatestSnapshot());
        assertFalse(model.isStale());
    }

    @Test
    void testInstanceSwitchHookClearsViaController() {
        MainWindowController controller = newMonitorController();
        controller.monitorModel.mergeRenderer(sampleRendererStats(50.0));
        controller.monitorModel.mergeController(new ControllerStats(1, 1, 1, 1, 1L));
        assertFalse(controller.monitorModel.getHistory().isEmpty());

        controller.onMonitoredInstanceChanged(null);

        assertTrue(controller.monitorModel.getHistory().isEmpty());
        assertEquals(-1, controller.currentMonitoredInstanceId);
    }

    @Test
    void testAllDispatchersRegisterStatsStateHandler() {
        MainWindowController controller = newMonitorController();

        List<MessageDispatcher> dispatchers = new ArrayList<>();
        for (int i = 0; i < 3; i++) {
            MessageDispatcher d = new MessageDispatcher();
            controller.registerStatsStateHandler(d);
            dispatchers.add(d);
        }

        for (MessageDispatcher d : dispatchers) {
            d.dispatch(Protocol.createEvent(Protocol.EVENT_STATS_STATE, sampleStatsPayload(50.0)));
        }

        boolean unhandledWarning = appender.list.stream()
                .anyMatch(e -> e.getLevel() == Level.WARN
                        && e.getFormattedMessage().contains("No handler registered for event action: stats_state"));
        assertFalse(unhandledWarning, "every dispatcher must have stats_state handler registered");

        assertNotNull(controller.monitorModel.getLatestSnapshot(),
                "handler must have run and updated the shared model");
        assertNotNull(controller.monitorModel.getLatestSnapshot().renderer());
    }

    @Test
    void testUnregisteredStatsStateLogsWarning() {
        MessageDispatcher dispatcher = new MessageDispatcher();

        dispatcher.dispatch(Protocol.createEvent(Protocol.EVENT_STATS_STATE, sampleStatsPayload(50.0)));

        boolean warned = appender.list.stream()
                .anyMatch(e -> e.getLevel() == Level.WARN
                        && e.getFormattedMessage().contains("No handler registered for event action: stats_state"));
        assertTrue(warned, "sanity: unregistered dispatch must warn (proves the assertion in testAll* is meaningful)");
    }

    @Test
    void testStaleDetectionAfter10s() {
        long threshold = 10_000L;
        long lastUpdate = 1_000_000L;

        assertFalse(MonitorDataModel.isStaleAt(lastUpdate + 9_000L, lastUpdate, threshold),
                "9s after update should not be stale");
        assertFalse(MonitorDataModel.isStaleAt(lastUpdate + 10_000L, lastUpdate, threshold),
                "exactly at threshold should not be stale (strict >)");
        assertTrue(MonitorDataModel.isStaleAt(lastUpdate + 10_001L, lastUpdate, threshold),
                "10s+1ms after update should be stale");
        assertTrue(MonitorDataModel.isStaleAt(5_000_000L, 0L, threshold),
                "never-received state should count as stale");

        MainWindowController controller = newMonitorController();
        MonitorDataModel model = controller.monitorModel;
        controller.lastStatsStateReceivedMs = lastUpdate;

        assertFalse(model.isStale());
        if (MonitorDataModel.isStaleAt(
                lastUpdate + 11_000L, controller.lastStatsStateReceivedMs, threshold)) {
            model.markStale();
        }
        assertTrue(model.isStale(), "model must be marked stale after 11s with no stats_state");

        model.mergeRenderer(sampleRendererStats(1.0));
        assertFalse(model.isStale(), "fresh stats_state must clear the stale flag");
    }

    @Test
    void testHistoryCappedAtSixty() {
        MonitorDataModel model = new MonitorDataModel();
        for (int i = 0; i < 70; i++) {
            model.mergeController(new ControllerStats(i, i, i, i, i));
        }
        assertEquals(MonitorDataModel.HISTORY_CAP, model.getHistory().size());
        assertEquals(69, model.getHistory().get(model.getHistory().size() - 1).controller().cpuPercent(),
                "history must keep the most recent 60 entries, dropping oldest");
    }

    private MainWindowController newMonitorController() {
        MainWindowController controller = new MainWindowController();
        controller.initMonitorForTest();
        controller.fxRunner = Runnable::run;
        return controller;
    }

    private RendererStats sampleRendererStats(double cpu) {
        return new RendererStats(
                cpu, 100_000_000L, 75.0, "NVIDIA RTX", 512_000_000L, 1_000_000_000L, System.currentTimeMillis());
    }

    private JsonObject sampleStatsPayload(double cpu) {
        JsonObject obj = new JsonObject();
        obj.addProperty("cpu_percent", cpu);
        obj.addProperty("rss_bytes", 100_000_000L);
        obj.addProperty("gpu_percent", 75.0);
        obj.addProperty("gpu_name", "NVIDIA RTX");
        obj.addProperty("vram_used_bytes", 512_000_000L);
        obj.addProperty("vram_total_bytes", 1_000_000_000L);
        obj.addProperty("timestamp_ms", System.currentTimeMillis());
        return obj;
    }
}
