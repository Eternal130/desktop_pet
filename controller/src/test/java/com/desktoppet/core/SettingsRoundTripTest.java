package com.desktoppet.core;

import com.desktoppet.model.PanelConfig;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

import static org.junit.jupiter.api.Assertions.*;

class SettingsRoundTripTest {

    @Test
    void allDefaultValues_surviveRoundTrip(@TempDir Path tempDir) {
        PanelStateManager manager = createManager(tempDir);
        PanelConfig defaults = PanelConfig.defaults();

        manager.save(defaults);
        PanelConfig loaded = manager.load();

        assertEquals(defaults, loaded);
    }

    @Test
    void allNonDefaultValues_surviveRoundTrip(@TempDir Path tempDir) {
        PanelStateManager manager = createManager(tempDir);
        PanelConfig config = new PanelConfig(100, 200, 1400, 800, "樱花浅粉", 15, 0.85,
                List.of("id-1"), true, true, "hide_to_tray", true);

        manager.save(config);
        PanelConfig loaded = manager.load();

        assertEquals(100, loaded.panelX());
        assertEquals(200, loaded.panelY());
        assertEquals(1400, loaded.panelWidth());
        assertEquals(800, loaded.panelHeight());
        assertEquals("樱花浅粉", loaded.theme());
        assertEquals(15, loaded.fontSize());
        assertEquals(0.85, loaded.panelOpacity());
        assertEquals(List.of("id-1"), loaded.instanceIds());
        assertTrue(loaded.autoLaunchSystem());
        assertTrue(loaded.startMinimized());
        assertEquals("hide_to_tray", loaded.closeAction());
        assertTrue(loaded.confirmOnExit());
        assertEquals(config, loaded);
    }

    @Test
    void multipleInstances_surviveRoundTrip(@TempDir Path tempDir) {
        PanelStateManager manager = createManager(tempDir);
        PanelConfig config = new PanelConfig(-1, -1, 1200, 760, "深紫梦幻", 13, 1.0,
                List.of("id-1", "id-2", "id-3"), false, false, "exit", false);

        manager.save(config);
        PanelConfig loaded = manager.load();

        assertEquals(List.of("id-1", "id-2", "id-3"), loaded.instanceIds());
        assertEquals(config, loaded);
    }

    @Test
    void emptyInstanceIds_surviveRoundTrip(@TempDir Path tempDir) {
        PanelStateManager manager = createManager(tempDir);
        PanelConfig config = new PanelConfig(50, 100, 1300, 750, "深紫梦幻", 13, 0.9,
                List.of(), false, false, "exit", false);

        manager.save(config);
        PanelConfig loaded = manager.load();

        assertTrue(loaded.instanceIds().isEmpty());
        assertEquals(config, loaded);
    }

    @Test
    void closeActionHideToTray_survivesRoundTrip(@TempDir Path tempDir) {
        PanelStateManager manager = createManager(tempDir);
        PanelConfig config = new PanelConfig(-1, -1, 1200, 760, "深紫梦幻", 13, 1.0,
                List.of(), false, false, "hide_to_tray", true);

        manager.save(config);
        PanelConfig loaded = manager.load();

        assertEquals("hide_to_tray", loaded.closeAction());
        assertTrue(loaded.confirmOnExit());
    }

    @Test
    void backwardCompatibility_jsonWithOnlyOldFields_loadsCorrectly(@TempDir Path tempDir) throws IOException {
        Path configPath = tempDir.resolve("panel.json");
        InstanceConfigManager instanceManager = new InstanceConfigManager(tempDir.resolve("instances"));
        PanelStateManager manager = new PanelStateManager(configPath, tempDir.resolve("panel-state.json"), instanceManager);

        Files.writeString(configPath, """
            {
              "panel": {
                "x": 50,
                "y": 100,
                "width": 1300,
                "height": 750,
                "theme": "樱花浅粉",
                "font_size": 14,
                "panel_opacity": 0.9
              },
              "instances": ["id-1"]
            }
            """);

        PanelConfig loaded = manager.load();

        assertEquals(50, loaded.panelX());
        assertEquals(100, loaded.panelY());
        assertEquals(1300, loaded.panelWidth());
        assertEquals(750, loaded.panelHeight());
        assertEquals("樱花浅粉", loaded.theme());
        assertEquals(14, loaded.fontSize());
        assertEquals(0.9, loaded.panelOpacity());
        assertEquals(List.of("id-1"), loaded.instanceIds());

        assertFalse(loaded.autoLaunchSystem());
        assertFalse(loaded.startMinimized());
        assertEquals("exit", loaded.closeAction());
        assertFalse(loaded.confirmOnExit());
    }

    private PanelStateManager createManager(Path tempDir) {
        Path configPath = tempDir.resolve("panel.json");
        Path legacyPath = tempDir.resolve("panel-state.json");
        InstanceConfigManager instanceManager = new InstanceConfigManager(tempDir.resolve("instances"));
        return new PanelStateManager(configPath, legacyPath, instanceManager);
    }
}
