package com.desktoppet.core;

import com.desktoppet.model.BehaviorConfig;
import com.desktoppet.model.ModelSettingsConfig;
import com.desktoppet.model.PetConfig;
import com.desktoppet.model.SystemConfig;
import com.desktoppet.model.WindowConfig;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

class ConfigManagerTest {

    @Test
    void load_firstRun_createsDefaultConfig(@TempDir Path tempDir) {
        Path configPath = tempDir.resolve("desktop-pet/config.json");
        ConfigManager manager = new ConfigManager(configPath);

        PetConfig loaded = manager.load();

        assertEquals(PetConfig.defaults(), loaded);
        assertTrue(Files.exists(configPath));
    }

    @Test
    void load_existingConfig_parsedCorrectly(@TempDir Path tempDir) throws IOException {
        Path configPath = tempDir.resolve("desktop-pet/config.json");
        Files.createDirectories(configPath.getParent());
        Files.writeString(configPath, """
            {
              "window": { "position_x": 900, "position_y": 420, "opacity": 0.75 },
              "model": { "current_model_name": "Haru", "scale": 1.25 },
              "behavior": { "drag_mode": "physics", "idle_interval_seconds": 7 },
              "system": { "auto_start": true }
            }
            """);
        ConfigManager manager = new ConfigManager(configPath);

        PetConfig loaded = manager.load();

        assertEquals(900, loaded.window().positionX());
        assertEquals(420, loaded.window().positionY());
        assertEquals(0.75, loaded.window().opacity());
        assertEquals("Haru", loaded.model().currentModelName());
        assertEquals(1.25, loaded.model().scale());
        assertEquals("physics", loaded.behavior().dragMode());
        assertEquals(7, loaded.behavior().idleIntervalSeconds());
        assertTrue(loaded.system().autoStart());
    }

    @Test
    void load_corruptJson_returnsDefaults(@TempDir Path tempDir) throws IOException {
        Path configPath = tempDir.resolve("desktop-pet/config.json");
        Files.createDirectories(configPath.getParent());
        Files.writeString(configPath, "not json");
        ConfigManager manager = new ConfigManager(configPath);

        PetConfig loaded = manager.load();

        assertEquals(PetConfig.defaults(), loaded);
    }

    @Test
    void load_partialConfig_missingFieldsUseDefaults(@TempDir Path tempDir) throws IOException {
        Path configPath = tempDir.resolve("desktop-pet/config.json");
        Files.createDirectories(configPath.getParent());
        Files.writeString(configPath, """
            {
              "window": { "position_x": 100 }
            }
            """);
        ConfigManager manager = new ConfigManager(configPath);

        PetConfig loaded = manager.load();
        PetConfig defaults = PetConfig.defaults();

        assertEquals(100, loaded.window().positionX());
        assertEquals(defaults.window().positionY(), loaded.window().positionY());
        assertEquals(defaults.window().opacity(), loaded.window().opacity());
        assertEquals(defaults.model(), loaded.model());
        assertEquals(defaults.behavior(), loaded.behavior());
        assertEquals(defaults.system(), loaded.system());
    }

    @Test
    void save_createsDirectories(@TempDir Path tempDir) {
        Path configPath = tempDir.resolve("a/b/c/config.json");
        ConfigManager manager = new ConfigManager(configPath);

        manager.save(PetConfig.defaults());

        assertTrue(Files.exists(configPath.getParent()));
        assertTrue(Files.exists(configPath));
    }

    @Test
    void save_thenLoad_roundTrip(@TempDir Path tempDir) {
        Path configPath = tempDir.resolve("desktop-pet/config.json");
        ConfigManager manager = new ConfigManager(configPath);
        PetConfig input = new PetConfig(
            new WindowConfig(321, 654, 500, 600, 0.66),
            new ModelSettingsConfig("Shizuku", 1.33),
            new BehaviorConfig("physics", 17, 60),
            new SystemConfig(true)
        );

        manager.save(input);
        PetConfig loaded = manager.load();

        assertEquals(input, loaded);
    }

    @Test
    void defaults_correctValues() {
        ConfigManager manager = new ConfigManager(Path.of("/tmp/unused-config.json"));

        PetConfig defaults = manager.getDefaults();

        assertNotNull(defaults);
        assertEquals(1200, defaults.window().positionX());
        assertEquals(600, defaults.window().positionY());
        assertEquals(400, defaults.window().width());
        assertEquals(500, defaults.window().height());
        assertEquals(1.0, defaults.window().opacity());
        assertEquals("Hiyori", defaults.model().currentModelName());
        assertEquals(1.0, defaults.model().scale());
        assertEquals("direct", defaults.behavior().dragMode());
        assertEquals(10, defaults.behavior().idleIntervalSeconds());
        assertFalse(defaults.system().autoStart());
    }
}
