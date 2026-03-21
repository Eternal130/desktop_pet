package com.desktoppet.core;

import com.desktoppet.model.MountConfig;
import com.google.gson.*;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import java.io.IOException;
import java.nio.file.*;
import java.util.*;

/**
 * Manages persistence of voice pack mount configuration.
 * Config file format: {"mounts": {"modelName": {"voice_pack": "packName"}}}
 */
public class MountConfigManager {

    private static final Logger log = LoggerFactory.getLogger(MountConfigManager.class);
    private static final Gson GSON = new GsonBuilder().setPrettyPrinting().create();

    private final Path configPath;

    /** Default constructor uses ~/.config/desktop-pet/mount.json */
    public MountConfigManager() {
        this(Path.of(System.getProperty("user.home"), ".config", "desktop-pet", "mount.json"));
    }

    /** Injectable constructor for testing */
    public MountConfigManager(Path configPath) {
        this.configPath = configPath;
    }

    /** Returns the mount config for the given model. voicePackName is null if not mounted. */
    public MountConfig loadForModel(String modelName) {
        JsonObject mounts = readMountsObject();
        if (mounts.has(modelName)) {
            JsonObject entry = mounts.getAsJsonObject(modelName);
            String voicePack = entry.has("voice_pack") && !entry.get("voice_pack").isJsonNull()
                ? entry.get("voice_pack").getAsString() : null;
            return new MountConfig(modelName, voicePack);
        }
        return new MountConfig(modelName, null);
    }

    /** Saves or updates the mount config for one model. */
    public void saveForModel(MountConfig config) {
        JsonObject root = readRoot();
        JsonObject mounts = root.has("mounts") ? root.getAsJsonObject("mounts") : new JsonObject();
        JsonObject entry = new JsonObject();
        if (config.voicePackName() != null) {
            entry.addProperty("voice_pack", config.voicePackName());
        } else {
            entry.add("voice_pack", JsonNull.INSTANCE);
        }
        mounts.add(config.modelName(), entry);
        root.add("mounts", mounts);
        writeRoot(root);
    }

    /** Returns all configured mounts as a map of modelName → MountConfig. */
    public Map<String, MountConfig> loadAll() {
        JsonObject mounts = readMountsObject();
        Map<String, MountConfig> result = new LinkedHashMap<>();
        for (Map.Entry<String, JsonElement> e : mounts.entrySet()) {
            String modelName = e.getKey();
            JsonObject entry = e.getValue().getAsJsonObject();
            String vp = entry.has("voice_pack") && !entry.get("voice_pack").isJsonNull()
                ? entry.get("voice_pack").getAsString() : null;
            result.put(modelName, new MountConfig(modelName, vp));
        }
        return result;
    }

    // --- private helpers ---

    private JsonObject readRoot() {
        if (!Files.exists(configPath)) {
            return emptyRoot();
        }
        try {
            String json = Files.readString(configPath);
            JsonElement el = JsonParser.parseString(json);
            return el.isJsonObject() ? el.getAsJsonObject() : emptyRoot();
        } catch (IOException | JsonParseException e) {
            log.warn("Failed to read mount config at {}: {}", configPath, e.getMessage());
            return emptyRoot();
        }
    }

    private JsonObject readMountsObject() {
        JsonObject root = readRoot();
        return root.has("mounts") ? root.getAsJsonObject("mounts") : new JsonObject();
    }

    private void writeRoot(JsonObject root) {
        try {
            Files.createDirectories(configPath.getParent());
            Files.writeString(configPath, GSON.toJson(root));
        } catch (IOException e) {
            log.error("Failed to write mount config at {}: {}", configPath, e.getMessage());
        }
    }

    private static JsonObject emptyRoot() {
        JsonObject root = new JsonObject();
        root.add("mounts", new JsonObject());
        return root;
    }
}
