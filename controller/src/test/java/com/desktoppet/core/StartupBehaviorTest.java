package com.desktoppet.core;

import com.desktoppet.model.PanelConfig;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.nio.file.Path;
import java.util.List;

import static org.junit.jupiter.api.Assertions.*;

class StartupBehaviorTest {

    @Test
    void startMinimizedTrue_survivesRoundTrip(@TempDir Path tempDir) {
        PanelStateManager manager = createManager(tempDir);
        PanelConfig config = withNewFields(true, false, "exit", false);

        manager.save(config);
        PanelConfig loaded = manager.load();

        assertTrue(loaded.startMinimized());
        assertEquals(config, loaded);
    }

    @Test
    void startMinimizedFalse_survivesRoundTrip(@TempDir Path tempDir) {
        PanelStateManager manager = createManager(tempDir);
        PanelConfig config = withNewFields(false, false, "exit", false);

        manager.save(config);
        PanelConfig loaded = manager.load();

        assertFalse(loaded.startMinimized());
    }

    @Test
    void autoLaunchSystemTrue_survivesRoundTrip(@TempDir Path tempDir) {
        PanelStateManager manager = createManager(tempDir);
        PanelConfig config = withNewFields(false, true, "exit", false);

        manager.save(config);
        PanelConfig loaded = manager.load();

        assertTrue(loaded.autoLaunchSystem());
        assertEquals(config, loaded);
    }

    @Test
    void autoLaunchSystemFalse_survivesRoundTrip(@TempDir Path tempDir) {
        PanelStateManager manager = createManager(tempDir);
        PanelConfig config = withNewFields(false, false, "exit", false);

        manager.save(config);
        PanelConfig loaded = manager.load();

        assertFalse(loaded.autoLaunchSystem());
    }

    @Test
    void allNewFields_nonDefault_surviveRoundTrip(@TempDir Path tempDir) {
        PanelStateManager manager = createManager(tempDir);
        PanelConfig config = new PanelConfig(100, 200, 1400, 800, "樱花浅粉", 15, 0.85,
                List.of("id-1"), true, true, "hide_to_tray", true);

        manager.save(config);
        PanelConfig loaded = manager.load();

        assertTrue(loaded.autoLaunchSystem());
        assertTrue(loaded.startMinimized());
        assertEquals("hide_to_tray", loaded.closeAction());
        assertTrue(loaded.confirmOnExit());
        assertEquals(config, loaded);
    }

    @Test
    void defaults_allElevenFieldsCorrect() {
        PanelConfig d = PanelConfig.defaults();

        assertEquals(-1, d.panelX());
        assertEquals(-1, d.panelY());
        assertEquals(1200, d.panelWidth());
        assertEquals(760, d.panelHeight());
        assertEquals("深紫梦幻", d.theme());
        assertEquals(13, d.fontSize());
        assertEquals(1.0, d.panelOpacity());
        assertEquals(List.of(), d.instanceIds());
        assertFalse(d.autoLaunchSystem());
        assertFalse(d.startMinimized());
        assertEquals("exit", d.closeAction());
        assertFalse(d.confirmOnExit());
    }

    private PanelStateManager createManager(Path tempDir) {
        Path configPath = tempDir.resolve("panel.json");
        Path legacyPath = tempDir.resolve("panel-state.json");
        InstanceConfigManager instanceManager = new InstanceConfigManager(tempDir.resolve("instances"));
        return new PanelStateManager(configPath, legacyPath, instanceManager);
    }

    private PanelConfig withNewFields(boolean startMinimized, boolean autoLaunchSystem,
                                       String closeAction, boolean confirmOnExit) {
        return new PanelConfig(-1, -1, 1200, 760, "深紫梦幻", 13, 1.0,
                List.of(), autoLaunchSystem, startMinimized, closeAction, confirmOnExit);
    }
}
