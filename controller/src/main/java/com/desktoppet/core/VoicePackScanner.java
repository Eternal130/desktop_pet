package com.desktoppet.core;

import java.io.IOException;
import java.nio.file.*;
import java.util.*;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/** Scans a Resources directory and identifies voice pack directories (those containing meta.mko). */
public final class VoicePackScanner {

    private static final Logger log = LoggerFactory.getLogger(VoicePackScanner.class);
    private static final String VOICE_PACK_INDEX = "meta.mko";

    private VoicePackScanner() {} // utility class

    /**
     * Scans {@code resourcesDir} and returns the names of subdirectories that contain {@code meta.mko}.
     * Returns an empty list if {@code resourcesDir} does not exist or is not a directory.
     */
    public static List<String> scanAvailableVoicePacks(Path resourcesDir) {
        if (resourcesDir == null || !Files.isDirectory(resourcesDir)) {
            return List.of();
        }
        List<String> result = new ArrayList<>();
        try (DirectoryStream<Path> stream = Files.newDirectoryStream(resourcesDir)) {
            for (Path entry : stream) {
                String name = entry.getFileName().toString();
                // skip hidden directories (starting with '.')
                if (name.startsWith(".")) continue;
                // must be a directory
                if (!Files.isDirectory(entry)) continue;
                // must contain meta.mko
                if (Files.exists(entry.resolve(VOICE_PACK_INDEX))) {
                    result.add(name);
                }
            }
        } catch (IOException e) {
            log.warn("Failed to scan voice packs in {}: {}", resourcesDir, e.getMessage());
        }
        Collections.sort(result);
        return result;
    }
}
