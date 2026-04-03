package com.desktoppet.core;

import com.desktoppet.model.BehaviorConfig;
import com.desktoppet.model.ModelSettingsConfig;
import com.desktoppet.model.PetConfig;
import com.desktoppet.model.SystemConfig;
import com.desktoppet.model.WindowConfig;
import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class ConfigManager {

    private static final Logger log = LoggerFactory.getLogger(ConfigManager.class);

    private final Path configPath;
    private final Gson gson;

    public ConfigManager() {
        this(Path.of(System.getProperty("user.home"), ".config", "desktop-pet", "config.json"));
    }

    public ConfigManager(Path configPath) {
        this.configPath = configPath;
        this.gson = new GsonBuilder().setPrettyPrinting().create();
    }

    public PetConfig load() {
        PetConfig defaults = getDefaults();

        if (!Files.exists(configPath)) {
            save(defaults);
            return defaults;
        }

        try {
            String content = Files.readString(configPath);
            JsonObject root = JsonParser.parseString(content).getAsJsonObject();
            return mergeWithDefaults(root, defaults);
        } catch (Exception e) {
            log.warn("Failed to load config from {}, using defaults", configPath, e);
            return defaults;
        }
    }

    public void save(PetConfig config) {
        try {
            if (configPath.getParent() != null) {
                Files.createDirectories(configPath.getParent());
            }
            JsonObject root = toJson(config);
            Files.writeString(configPath, gson.toJson(root));
        } catch (IOException e) {
            log.error("Failed to save config to {}", configPath, e);
        }
    }

    public String getConfigPath() {
        return configPath.toString();
    }

    public PetConfig getDefaults() {
        return PetConfig.defaults();
    }

    private PetConfig mergeWithDefaults(JsonObject root, PetConfig defaults) {
        JsonObject windowObj = getObject(root, "window");
        JsonObject modelObj = getObject(root, "model");
        JsonObject behaviorObj = getObject(root, "behavior");
        JsonObject systemObj = getObject(root, "system");

        WindowConfig window = new WindowConfig(
            getInt(windowObj, "position_x", defaults.window().positionX()),
            getInt(windowObj, "position_y", defaults.window().positionY()),
            getInt(windowObj, "window_width", defaults.window().width()),
            getInt(windowObj, "window_height", defaults.window().height()),
            getDouble(windowObj, "opacity", defaults.window().opacity()),
            getDouble(windowObj, "layout_offset_x", defaults.window().layoutOffsetX()),
            getDouble(windowObj, "layout_offset_y", defaults.window().layoutOffsetY()),
            getDouble(windowObj, "layout_scale", defaults.window().layoutScale())
        );

        ModelSettingsConfig model = new ModelSettingsConfig(
            getString(modelObj, "current_model_name", defaults.model().currentModelName()),
            getDouble(modelObj, "scale", defaults.model().scale())
        );

        BehaviorConfig behavior = new BehaviorConfig(
            getString(behaviorObj, "drag_mode", defaults.behavior().dragMode()),
            getInt(behaviorObj, "idle_interval_seconds", defaults.behavior().idleIntervalSeconds()),
            getInt(behaviorObj, "target_fps", defaults.behavior().targetFps())
        );

        SystemConfig system = new SystemConfig(
            getBoolean(systemObj, "auto_start", defaults.system().autoStart()),
            getString(systemObj, "default_graphics_backend", defaults.system().defaultGraphicsBackend())
        );

        return new PetConfig(window, model, behavior, system);
    }

    private JsonObject toJson(PetConfig config) {
        JsonObject root = new JsonObject();

        JsonObject window = new JsonObject();
        window.addProperty("position_x", config.window().positionX());
        window.addProperty("position_y", config.window().positionY());
        window.addProperty("window_width", config.window().width());
        window.addProperty("window_height", config.window().height());
        window.addProperty("opacity", config.window().opacity());
        window.addProperty("layout_offset_x", config.window().layoutOffsetX());
        window.addProperty("layout_offset_y", config.window().layoutOffsetY());
        window.addProperty("layout_scale", config.window().layoutScale());
        root.add("window", window);

        JsonObject model = new JsonObject();
        model.addProperty("current_model_name", config.model().currentModelName());
        model.addProperty("scale", config.model().scale());
        root.add("model", model);

        JsonObject behavior = new JsonObject();
        behavior.addProperty("drag_mode", config.behavior().dragMode());
        behavior.addProperty("idle_interval_seconds", config.behavior().idleIntervalSeconds());
        behavior.addProperty("target_fps", config.behavior().targetFps());
        root.add("behavior", behavior);

        JsonObject system = new JsonObject();
        system.addProperty("auto_start", config.system().autoStart());
        system.addProperty("default_graphics_backend", config.system().defaultGraphicsBackend());
        root.add("system", system);

        return root;
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
