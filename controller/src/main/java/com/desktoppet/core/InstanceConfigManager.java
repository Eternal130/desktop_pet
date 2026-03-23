package com.desktoppet.core;

import com.desktoppet.model.InstanceConfig;
import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

/**
 * Manages per-instance configuration files.
 * Each instance config is stored as ~/.config/desktop-pet/instances/{uuid}.json
 */
public class InstanceConfigManager {

    private static final Logger log = LoggerFactory.getLogger(InstanceConfigManager.class);

    private final Path instancesDir;
    private final Gson gson;

    public InstanceConfigManager() {
        this(Path.of(System.getProperty("user.home"), ".config", "desktop-pet", "instances"));
    }

    public InstanceConfigManager(Path instancesDir) {
        this.instancesDir = instancesDir;
        this.gson = new GsonBuilder().setPrettyPrinting().create();
    }

    public void save(InstanceConfig config) {
        Path path = getConfigPath(config.id());
        writeJson(path, toJson(config));
        log.debug("Saved instance config: {}", path);
    }

    public InstanceConfig load(String configId) {
        Path path = getConfigPath(configId);
        if (!Files.exists(path)) {
            log.warn("Instance config not found: {}", path);
            return null;
        }
        try {
            String content = Files.readString(path);
            JsonObject root = JsonParser.parseString(content).getAsJsonObject();
            return fromJson(configId, root);
        } catch (Exception e) {
            log.warn("Failed to load instance config {}: {}", configId, e.getMessage());
            return null;
        }
    }

    public List<InstanceConfig> loadAll(List<String> configIds) {
        List<InstanceConfig> result = new ArrayList<>();
        for (String id : configIds) {
            InstanceConfig config = load(id);
            if (config != null) {
                result.add(config);
            }
        }
        return result;
    }

    public void delete(String configId) {
        Path path = getConfigPath(configId);
        try {
            if (Files.deleteIfExists(path)) {
                log.info("Deleted instance config: {}", path);
            }
        } catch (IOException e) {
            log.warn("Failed to delete instance config {}: {}", configId, e.getMessage());
        }
    }

    public Path getConfigPath(String configId) {
        return instancesDir.resolve(configId + ".json");
    }

    private JsonObject toJson(InstanceConfig config) {
        JsonObject root = new JsonObject();
        root.addProperty("id", config.id());
        root.addProperty("label", config.label());
        root.addProperty("renderer_path", config.rendererPath());

        JsonObject model = new JsonObject();
        model.addProperty("name", config.modelName());
        model.addProperty("scale", config.modelScale());
        root.add("model", model);

        JsonObject window = new JsonObject();
        window.addProperty("x", config.windowX());
        window.addProperty("y", config.windowY());
        window.addProperty("width", config.windowWidth());
        window.addProperty("height", config.windowHeight());
        window.addProperty("opacity", config.opacity());
        window.addProperty("layout_offset_x", config.layoutOffsetX());
        window.addProperty("layout_offset_y", config.layoutOffsetY());
        window.addProperty("layout_scale", config.layoutScale());
        root.add("window", window);

        JsonObject behavior = new JsonObject();
        behavior.addProperty("drag_mode", config.dragMode());
        behavior.addProperty("idle_interval", config.idleInterval());
        behavior.addProperty("target_fps", config.targetFps());
        root.add("behavior", behavior);

        root.addProperty("auto_start", config.autoStart());
        root.addProperty("current_expression", config.currentExpression());

        if (config.voicePack() != null) {
            root.addProperty("voice_pack", config.voicePack());
        } else {
            root.add("voice_pack", com.google.gson.JsonNull.INSTANCE);
        }

        return root;
    }

    private InstanceConfig fromJson(String configId, JsonObject root) {
        InstanceConfig d = InstanceConfig.defaults();

        String id = getString(root, "id", configId);
        String label = getString(root, "label", d.label());
        String rendererPath = getString(root, "renderer_path", d.rendererPath());

        JsonObject modelObj = getObject(root, "model");
        String modelName = getString(modelObj, "name", d.modelName());
        double modelScale = getDouble(modelObj, "scale", d.modelScale());

        JsonObject windowObj = getObject(root, "window");
        int windowX = getInt(windowObj, "x", d.windowX());
        int windowY = getInt(windowObj, "y", d.windowY());
        int windowWidth = getInt(windowObj, "width", d.windowWidth());
        int windowHeight = getInt(windowObj, "height", d.windowHeight());
        double opacity = getDouble(windowObj, "opacity", d.opacity());

        JsonObject behaviorObj = getObject(root, "behavior");
        String dragMode = getString(behaviorObj, "drag_mode", d.dragMode());
        int idleInterval = getInt(behaviorObj, "idle_interval", d.idleInterval());
        int targetFps = getInt(behaviorObj, "target_fps", d.targetFps());

        boolean autoStart = getBoolean(root, "auto_start", d.autoStart());
        String currentExpression = getString(root, "current_expression", d.currentExpression());

        String voicePack = null;
        if (root.has("voice_pack") && !root.get("voice_pack").isJsonNull()) {
            voicePack = root.get("voice_pack").getAsString();
        }

        double layoutOffsetX = getDouble(windowObj, "layout_offset_x", d.layoutOffsetX());
        double layoutOffsetY = getDouble(windowObj, "layout_offset_y", d.layoutOffsetY());
        double layoutScale = getDouble(windowObj, "layout_scale", d.layoutScale());

        return new InstanceConfig(
            id, label, rendererPath,
            modelName, modelScale,
            windowX, windowY, windowWidth, windowHeight, opacity,
            dragMode, idleInterval, targetFps,
            autoStart, currentExpression, voicePack,
            getDouble(root, "volume", 1.0),
            layoutOffsetX, layoutOffsetY, layoutScale
        );
    }

    private void writeJson(Path path, JsonObject json) {
        try {
            Files.createDirectories(path.getParent());
            Files.writeString(path, gson.toJson(json));
        } catch (IOException e) {
            log.error("Failed to write config to {}: {}", path, e.getMessage());
        }
    }

    private JsonObject getObject(JsonObject root, String key) {
        if (root == null || !root.has(key) || !root.get(key).isJsonObject()) {
            return null;
        }
        return root.getAsJsonObject(key);
    }

    private int getInt(JsonObject obj, String key, int fallback) {
        try {
            return obj != null && obj.has(key) ? obj.get(key).getAsInt() : fallback;
        } catch (Exception ignored) {
            return fallback;
        }
    }

    private double getDouble(JsonObject obj, String key, double fallback) {
        try {
            return obj != null && obj.has(key) ? obj.get(key).getAsDouble() : fallback;
        } catch (Exception ignored) {
            return fallback;
        }
    }

    private String getString(JsonObject obj, String key, String fallback) {
        try {
            return obj != null && obj.has(key) ? obj.get(key).getAsString() : fallback;
        } catch (Exception ignored) {
            return fallback;
        }
    }

    private boolean getBoolean(JsonObject obj, String key, boolean fallback) {
        try {
            return obj != null && obj.has(key) ? obj.get(key).getAsBoolean() : fallback;
        } catch (Exception ignored) {
            return fallback;
        }
    }
}
