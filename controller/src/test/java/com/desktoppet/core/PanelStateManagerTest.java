package com.desktoppet.core;

import com.desktoppet.model.InstanceConfig;
import com.desktoppet.model.PanelConfig;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

class PanelStateManagerTest {

    @Test
    void load_noFiles_returnsDefaults(@TempDir Path tempDir) {
        Path configPath = tempDir.resolve("panel.json");
        Path legacyPath = tempDir.resolve("panel-state.json");
        InstanceConfigManager instanceManager = new InstanceConfigManager(tempDir.resolve("instances"));
        PanelStateManager manager = new PanelStateManager(configPath, legacyPath, instanceManager);

        PanelConfig loaded = manager.load();

        assertEquals(PanelConfig.defaults(), loaded);
    }

    @Test
    void save_thenLoad_roundTrip(@TempDir Path tempDir) {
        Path configPath = tempDir.resolve("panel.json");
        Path legacyPath = tempDir.resolve("panel-state.json");
        InstanceConfigManager instanceManager = new InstanceConfigManager(tempDir.resolve("instances"));
        PanelStateManager manager = new PanelStateManager(configPath, legacyPath, instanceManager);
        PanelConfig input = new PanelConfig(100, 200, 1400, 800, "深紫梦幻", 13, 1.0, List.of("id-1", "id-2"));

        manager.save(input);
        PanelConfig loaded = manager.load();

        assertEquals(input, loaded);
    }

    @Test
    void migrateFromLegacy_convertsInstances(@TempDir Path tempDir) throws IOException {
        Path configPath = tempDir.resolve("panel.json");
        Path legacyPath = tempDir.resolve("panel-state.json");
        Path instancesDir = tempDir.resolve("instances");
        InstanceConfigManager instanceManager = new InstanceConfigManager(instancesDir);

        Files.createDirectories(legacyPath.getParent());
        Files.writeString(legacyPath, """
            {
              "panel": {
                "x": 50,
                "y": 100,
                "width": 1300,
                "height": 750,
                "theme": "深紫梦幻"
              },
              "instances": [
                {
                  "label": "Instance One",
                  "renderer_path": "/path/to/renderer1",
                  "model": "Model1",
                  "opacity": 0.9,
                  "drag_mode": "physics",
                  "idle_interval": 12,
                  "pos_x": 1100,
                  "pos_y": 550,
                  "window_width": 450,
                  "window_height": 520,
                  "auto_start": true,
                  "current_expression": "F02",
                  "target_fps": 30
                },
                {
                  "label": "Instance Two",
                  "renderer_path": "/path/to/renderer2",
                  "model": "Model2",
                  "opacity": 0.8,
                  "drag_mode": "direct",
                  "idle_interval": 15,
                  "pos_x": 1300,
                  "pos_y": 600,
                  "window_width": 400,
                  "window_height": 500,
                  "auto_start": false,
                  "current_expression": "F01",
                  "target_fps": 0
                }
              ]
            }
            """);

        PanelStateManager manager = new PanelStateManager(configPath, legacyPath, instanceManager);
        PanelConfig loaded = manager.load();

        assertEquals(50, loaded.panelX());
        assertEquals(100, loaded.panelY());
        assertEquals(1300, loaded.panelWidth());
        assertEquals(750, loaded.panelHeight());
        assertEquals("深紫梦幻", loaded.theme());
        assertEquals(2, loaded.instanceIds().size());

        String id1 = loaded.instanceIds().get(0);
        String id2 = loaded.instanceIds().get(1);

        InstanceConfig inst1 = instanceManager.load(id1);
        InstanceConfig inst2 = instanceManager.load(id2);

        assertEquals("Instance One", inst1.label());
        assertEquals("/path/to/renderer1", inst1.rendererPath());
        assertEquals("Model1", inst1.modelName());
        assertEquals(0.9, inst1.opacity());
        assertEquals("physics", inst1.dragMode());
        assertEquals(12, inst1.idleInterval());
        assertEquals(1100, inst1.windowX());
        assertEquals(550, inst1.windowY());
        assertEquals(450, inst1.windowWidth());
        assertEquals(520, inst1.windowHeight());
        assertTrue(inst1.autoStart());
        assertEquals("F02", inst1.currentExpression());
        assertEquals(30, inst1.targetFps());

        assertEquals("Instance Two", inst2.label());
        assertEquals("/path/to/renderer2", inst2.rendererPath());
        assertEquals("Model2", inst2.modelName());
        assertEquals(0.8, inst2.opacity());
        assertEquals("direct", inst2.dragMode());
        assertEquals(15, inst2.idleInterval());
        assertEquals(1300, inst2.windowX());
        assertEquals(600, inst2.windowY());
        assertEquals(400, inst2.windowWidth());
        assertEquals(500, inst2.windowHeight());
        assertFalse(inst2.autoStart());
        assertEquals("F01", inst2.currentExpression());
        assertEquals(0, inst2.targetFps());

        assertTrue(Files.exists(configPath));
        assertFalse(Files.exists(legacyPath));
        assertTrue(Files.exists(legacyPath.resolveSibling("panel-state.json.bak")));
    }
}
