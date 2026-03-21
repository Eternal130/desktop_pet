package com.desktoppet.core;

import com.desktoppet.model.VoicePackGroup;
import com.desktoppet.model.VoicePackInfo;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.nio.file.Files;
import java.nio.file.Path;

import static org.junit.jupiter.api.Assertions.*;

class MetaMkoParserTest {

    // Relative to controller/ (Maven working directory during test)
    private static final Path FIXTURE_DIR = Path.of("src/test/resources/voice-pack-fixture");

    @Test
    void parseValidMko_returnsVoicePackInfo() {
        VoicePackInfo info = MetaMkoParser.parse(FIXTURE_DIR);
        assertNotNull(info, "Expected non-null VoicePackInfo for valid meta.mko");
        assertFalse(info.displayName().isEmpty(), "displayName should not be empty");
        assertFalse(info.groups().isEmpty(), "groups should not be empty");
    }

    @Test
    void parseValidMko_hasExpectedGroups() {
        VoicePackInfo info = MetaMkoParser.parse(FIXTURE_DIR);
        assertNotNull(info);
        assertTrue(info.groups().size() > 0, "Should have at least one group");
        assertTrue(info.groups().containsKey("tap_head"),
            "Expected 'tap_head' group but got: " + info.groups().keySet());
    }

    @Test
    void parseValidMko_tapHeadHasActions() {
        VoicePackInfo info = MetaMkoParser.parse(FIXTURE_DIR);
        assertNotNull(info);
        VoicePackGroup tapHead = info.groups().get("tap_head");
        assertNotNull(tapHead, "tap_head group should exist");
        assertFalse(tapHead.actions().isEmpty(), "tap_head should have at least one action");
    }

    @Test
    void parseCorruptBytes_returnsNull(@TempDir Path tempDir) throws Exception {
        Path fakeDir = tempDir.resolve("fake-pack");
        Files.createDirectory(fakeDir);
        Files.write(fakeDir.resolve("meta.mko"), new byte[]{0x01, 0x02, 0x03, 0x04, 0x05});
        assertNull(MetaMkoParser.parse(fakeDir), "Corrupt bytes should yield null");
    }

    @Test
    void parseMissingFile_returnsNull(@TempDir Path tempDir) {
        assertNull(MetaMkoParser.parse(tempDir), "Missing meta.mko should yield null");
    }
}
