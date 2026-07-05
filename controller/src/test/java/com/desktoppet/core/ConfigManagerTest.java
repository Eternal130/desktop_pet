package com.desktoppet.core;

import com.desktoppet.model.InstanceConfig;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

class ConfigManagerTest {

    @Test
    void save_thenLoad_roundTrip(@TempDir Path tempDir) {
        InstanceConfigManager manager = new InstanceConfigManager(tempDir);
        InstanceConfig input = new InstanceConfig(
            "test-id-123",
            "Test Instance",
            "/path/to/renderer",
            "opengl",
            "TestModel",
            1.5,
            100, 200, 800, 600,
            0.85,
            "physics",
            15,
            60,
            true,
            "F02",
            "voice-pack-1",
            1.0,
            false,
            0.0, 0.0, 1.0
        );

        manager.save(input);
        InstanceConfig loaded = manager.load("test-id-123");

        assertEquals(input, loaded);
    }

    @Test
    void load_nonExistent_returnsNull(@TempDir Path tempDir) {
        InstanceConfigManager manager = new InstanceConfigManager(tempDir);

        InstanceConfig loaded = manager.load("non-existent-id");

        assertNull(loaded);
    }

    @Test
    void load_corruptJson_returnsNull(@TempDir Path tempDir) throws IOException {
        InstanceConfigManager manager = new InstanceConfigManager(tempDir);
        Path configPath = manager.getConfigPath("corrupt-id");
        Files.createDirectories(configPath.getParent());
        Files.writeString(configPath, "not valid json {{{");

        InstanceConfig loaded = manager.load("corrupt-id");

        assertNull(loaded);
    }

    @Test
    void loadAll_mixedExistence(@TempDir Path tempDir) {
        InstanceConfigManager manager = new InstanceConfigManager(tempDir);
        InstanceConfig config1 = new InstanceConfig(
            "id-1", "Instance 1", "", "opengl", "", 1.0, 0, 0, 400, 500, 1.0, "direct", 10, 0, false, "F01", null, 1.0, false, 0.0, 0.0, 1.0
        );
        InstanceConfig config2 = new InstanceConfig(
            "id-2", "Instance 2", "", "opengl", "", 1.0, 0, 0, 400, 500, 1.0, "direct", 10, 0, false, "F01", null, 1.0, false, 0.0, 0.0, 1.0
        );
        manager.save(config1);
        manager.save(config2);

        var loaded = manager.loadAll(java.util.List.of("id-1", "id-2", "id-3"));

        assertEquals(2, loaded.size());
        assertTrue(loaded.stream().anyMatch(c -> c.id().equals("id-1")));
        assertTrue(loaded.stream().anyMatch(c -> c.id().equals("id-2")));
    }

    @Test
    void delete_removesFile(@TempDir Path tempDir) {
        InstanceConfigManager manager = new InstanceConfigManager(tempDir);
        InstanceConfig config = new InstanceConfig(
            "delete-test", "Test", "", "opengl", "", 1.0, 0, 0, 400, 500, 1.0, "direct", 10, 0, false, "F01", null, 1.0, false, 0.0, 0.0, 1.0
        );
        manager.save(config);
        Path configPath = manager.getConfigPath("delete-test");
        assertTrue(Files.exists(configPath));

        manager.delete("delete-test");

        assertFalse(Files.exists(configPath));
    }

    @Test
    void save_createsDirectories(@TempDir Path tempDir) {
        InstanceConfigManager manager = new InstanceConfigManager(tempDir.resolve("nested/deep/path"));
        InstanceConfig config = new InstanceConfig(
            "nested-test", "Test", "", "opengl", "", 1.0, 0, 0, 400, 500, 1.0, "direct", 10, 0, false, "F01", null, 1.0, false, 0.0, 0.0, 1.0
        );

        manager.save(config);

        Path configPath = manager.getConfigPath("nested-test");
        assertTrue(Files.exists(configPath));
    }

    @Test
    void muted_persistsThroughRoundTrip(@TempDir Path tempDir) {
        InstanceConfigManager manager = new InstanceConfigManager(tempDir);
        InstanceConfig input = new InstanceConfig(
            "muted-test-id",
            "Muted Instance",
            "/path/to/renderer",
            "opengl",
            "TestModel",
            1.0,
            100, 200, 800, 600,
            0.85,
            "physics",
            15,
            60,
            true,
            "F02",
            "voice-pack-1",
            1.0,
            true,
            0.0, 0.0, 1.0
        );

        manager.save(input);
        InstanceConfig loaded = manager.load("muted-test-id");

        assertTrue(loaded.muted());
    }
}
