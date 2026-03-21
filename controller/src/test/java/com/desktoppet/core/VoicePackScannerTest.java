package com.desktoppet.core;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

import static org.junit.jupiter.api.Assertions.*;

class VoicePackScannerTest {

    @Test
    void scanWithVoicePackDir_returnsVoicePackName(@TempDir Path tempDir) throws IOException {
        Path voicePackDir = tempDir.resolve("VoicePack1");
        Files.createDirectories(voicePackDir);
        Files.createFile(voicePackDir.resolve("meta.mko"));

        List<String> result = VoicePackScanner.scanAvailableVoicePacks(tempDir);

        assertEquals(1, result.size());
        assertEquals("VoicePack1", result.get(0));
    }

    @Test
    void scanWithModelDir_excludesModelDir(@TempDir Path tempDir) throws IOException {
        Path modelDir = tempDir.resolve("ModelDir");
        Files.createDirectories(modelDir);
        Files.createFile(modelDir.resolve("name.model3.json"));

        List<String> result = VoicePackScanner.scanAvailableVoicePacks(tempDir);

        assertTrue(result.isEmpty());
    }

    @Test
    void scanEmptyResourcesDir_returnsEmptyList(@TempDir Path tempDir) {
        List<String> result = VoicePackScanner.scanAvailableVoicePacks(tempDir);

        assertTrue(result.isEmpty());
    }

    @Test
    void scanNonExistentDir_returnsEmptyList() {
        Path nonExistent = Path.of("/non/existent/path");

        List<String> result = VoicePackScanner.scanAvailableVoicePacks(nonExistent);

        assertTrue(result.isEmpty());
    }

    @Test
    void scanHiddenDir_skipsHiddenDir(@TempDir Path tempDir) throws IOException {
        Path hiddenDir = tempDir.resolve(".hidden");
        Files.createDirectories(hiddenDir);
        Files.createFile(hiddenDir.resolve("meta.mko"));

        List<String> result = VoicePackScanner.scanAvailableVoicePacks(tempDir);

        assertTrue(result.isEmpty());
    }
}
