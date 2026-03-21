package com.desktoppet.core;

import com.desktoppet.model.MountConfig;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;
import java.nio.file.*;
import java.util.Map;
import static org.junit.jupiter.api.Assertions.*;

class MountConfigManagerTest {

    @Test
    void saveAndLoad_roundTrip(@TempDir Path tempDir) {
        Path configPath = tempDir.resolve("mount.json");
        MountConfigManager mgr = new MountConfigManager(configPath);
        mgr.saveForModel(new MountConfig("giwa-idol2023", "锦瑟-锦瑟-中文-voice"));
        MountConfig loaded = mgr.loadForModel("giwa-idol2023");
        assertEquals("giwa-idol2023", loaded.modelName());
        assertEquals("锦瑟-锦瑟-中文-voice", loaded.voicePackName());
    }

    @Test
    void loadNonExistentModel_returnsNullVoicePack(@TempDir Path tempDir) {
        Path configPath = tempDir.resolve("mount.json");
        MountConfigManager mgr = new MountConfigManager(configPath);
        MountConfig loaded = mgr.loadForModel("unknown-model");
        assertEquals("unknown-model", loaded.modelName());
        assertNull(loaded.voicePackName());
    }

    @Test
    void multiModelIsolation(@TempDir Path tempDir) {
        Path configPath = tempDir.resolve("mount.json");
        MountConfigManager mgr = new MountConfigManager(configPath);
        mgr.saveForModel(new MountConfig("model-a", "voice-pack-1"));
        mgr.saveForModel(new MountConfig("model-b", "voice-pack-2"));
        assertEquals("voice-pack-1", mgr.loadForModel("model-a").voicePackName());
        assertEquals("voice-pack-2", mgr.loadForModel("model-b").voicePackName());
    }

    @Test
    void saveNull_persistsNullVoicePack(@TempDir Path tempDir) {
        Path configPath = tempDir.resolve("mount.json");
        MountConfigManager mgr = new MountConfigManager(configPath);
        mgr.saveForModel(new MountConfig("giwa-idol2023", "锦瑟-锦瑟-中文-voice"));
        mgr.saveForModel(new MountConfig("giwa-idol2023", null));
        assertNull(mgr.loadForModel("giwa-idol2023").voicePackName());
    }
}
