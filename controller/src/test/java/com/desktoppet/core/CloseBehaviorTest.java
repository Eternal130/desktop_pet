package com.desktoppet.core;

import com.desktoppet.model.PanelConfig;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

import static org.junit.jupiter.api.Assertions.*;

class CloseBehaviorTest {

    @Test
    void closeActionExit_survivesRoundTrip(@TempDir Path tempDir) {
        PanelStateManager manager = createManager(tempDir);
        PanelConfig config = new PanelConfig(-1, -1, 1200, 760, "深紫梦幻", 13, 1.0,
                List.of(), false, false, "exit", false);

        manager.save(config);
        PanelConfig loaded = manager.load();

        assertEquals("exit", loaded.closeAction());
    }

    @Test
    void closeActionHideToTray_survivesRoundTrip(@TempDir Path tempDir) {
        PanelStateManager manager = createManager(tempDir);
        PanelConfig config = new PanelConfig(-1, -1, 1200, 760, "深紫梦幻", 13, 1.0,
                List.of(), false, false, "hide_to_tray", false);

        manager.save(config);
        PanelConfig loaded = manager.load();

        assertEquals("hide_to_tray", loaded.closeAction());
    }

    @Test
    void confirmOnExitTrue_survivesRoundTrip(@TempDir Path tempDir) {
        PanelStateManager manager = createManager(tempDir);
        PanelConfig config = new PanelConfig(-1, -1, 1200, 760, "深紫梦幻", 13, 1.0,
                List.of(), false, false, "exit", true);

        manager.save(config);
        PanelConfig loaded = manager.load();

        assertTrue(loaded.confirmOnExit());
    }

    @Test
    void closeActionExitAndConfirmOnExit_combinedRoundTrip(@TempDir Path tempDir) {
        PanelStateManager manager = createManager(tempDir);
        PanelConfig config = new PanelConfig(100, 200, 1400, 800, "樱花浅粉", 15, 0.85,
                List.of("id-1"), false, false, "exit", true);

        manager.save(config);
        PanelConfig loaded = manager.load();

        assertEquals("exit", loaded.closeAction());
        assertTrue(loaded.confirmOnExit());
        assertEquals(config, loaded);
    }

    @Test
    void defaults_correctCloseBehaviorFields() {
        PanelConfig defaults = PanelConfig.defaults();

        assertEquals("exit", defaults.closeAction());
        assertFalse(defaults.confirmOnExit());
    }

    @Test
    void legacyJson_withoutCloseKeys_loadsWithDefaults(@TempDir Path tempDir) throws IOException {
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
                "theme": "深紫梦幻",
                "font_size": 14,
                "panel_opacity": 0.9
              },
              "instances": []
            }
            """);

        PanelConfig loaded = manager.load();

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
