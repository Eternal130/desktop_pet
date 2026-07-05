package com.desktoppet.core;

import com.desktoppet.model.InstanceConfig;
import com.desktoppet.model.PanelConfig;
import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

public class PanelStateManager {

    private static final Logger log = LoggerFactory.getLogger(PanelStateManager.class);

    private final Path configPath;
    private final Path legacyPath;
    private final Gson gson;
    private final InstanceConfigManager instanceConfigManager;

    public PanelStateManager() {
        this(
            Path.of(System.getProperty("user.home"), ".config", "desktop-pet", "panel.json"),
            Path.of(System.getProperty("user.home"), ".config", "desktop-pet", "panel-state.json"),
            new InstanceConfigManager()
        );
    }

    public PanelStateManager(Path configPath, Path legacyPath, InstanceConfigManager instanceConfigManager) {
        this.configPath = configPath;
        this.legacyPath = legacyPath;
        this.instanceConfigManager = instanceConfigManager;
        this.gson = new GsonBuilder().setPrettyPrinting().create();
    }

    public PanelConfig load() {
        if (Files.exists(configPath)) {
            return loadNewFormat();
        }

        if (Files.exists(legacyPath)) {
            log.info("Migrating legacy panel-state.json to new format");
            return migrateFromLegacy();
        }

        return PanelConfig.defaults();
    }

    public void save(PanelConfig config) {
        try {
            if (configPath.getParent() != null) {
                Files.createDirectories(configPath.getParent());
            }
            JsonObject root = toJson(config);
            Files.writeString(configPath, gson.toJson(root));
        } catch (IOException e) {
            log.error("Failed to save panel config to {}", configPath, e);
        }
    }

    public String getConfigPath() {
        return configPath.toString();
    }

    private PanelConfig loadNewFormat() {
        try {
            String content = Files.readString(configPath);
            JsonObject root = JsonParser.parseString(content).getAsJsonObject();
            return fromJson(root);
        } catch (Exception e) {
            log.warn("Failed to load panel config from {}, using defaults", configPath, e);
            return PanelConfig.defaults();
        }
    }

    private PanelConfig migrateFromLegacy() {
        try {
            String content = Files.readString(legacyPath);
            JsonObject root = JsonParser.parseString(content).getAsJsonObject();

            JsonObject panelObj = getObject(root, "panel");
            PanelConfig defaults = PanelConfig.defaults();
            double panelX = getDouble(panelObj, "x", defaults.panelX());
            double panelY = getDouble(panelObj, "y", defaults.panelY());
            double panelW = getDouble(panelObj, "width", defaults.panelWidth());
            double panelH = getDouble(panelObj, "height", defaults.panelHeight());
            String theme = getString(panelObj, "theme", defaults.theme());

            List<String> instanceIds = new ArrayList<>();
            if (root.has("instances") && root.get("instances").isJsonArray()) {
                for (var elem : root.getAsJsonArray("instances")) {
                    if (!elem.isJsonObject()) continue;
                    JsonObject obj = elem.getAsJsonObject();

                    String id = UUID.randomUUID().toString();
                    InstanceConfig instConfig = new InstanceConfig(
                        id,
                        getString(obj, "label", "新实例"),
                        getString(obj, "renderer_path", ""),
                        "opengl",
                        getString(obj, "model", ""),
                        1.0,
                        getInt(obj, "pos_x", 1200),
                        getInt(obj, "pos_y", 600),
                        getInt(obj, "window_width", 400),
                        getInt(obj, "window_height", 500),
                        getDouble(obj, "opacity", 1.0),
                        getString(obj, "drag_mode", "direct"),
                        getInt(obj, "idle_interval", 10),
                        getInt(obj, "target_fps", 0),
                        getBoolean(obj, "auto_start", false),
                        getString(obj, "current_expression", "F01"),
                        null,
                        1.0,
                        false,
                        0.0, 0.0, 1.0
                    );
                    instanceConfigManager.save(instConfig);
                    instanceIds.add(id);
                    log.info("Migrated instance '{}' → {}", instConfig.label(), id);
                }
            }

            PanelConfig panelConfig = new PanelConfig(panelX, panelY, panelW, panelH, theme, 13, 1.0, instanceIds,
                    false, false, "exit", false);
            save(panelConfig);

            Path backupPath = legacyPath.resolveSibling("panel-state.json.bak");
            Files.move(legacyPath, backupPath);
            log.info("Legacy panel-state.json backed up to {}", backupPath);

            return panelConfig;
        } catch (Exception e) {
            log.warn("Failed to migrate legacy panel state, using defaults", e);
            return PanelConfig.defaults();
        }
    }

    private JsonObject toJson(PanelConfig config) {
        JsonObject root = new JsonObject();

        JsonObject panel = new JsonObject();
        panel.addProperty("x", config.panelX());
        panel.addProperty("y", config.panelY());
        panel.addProperty("width", config.panelWidth());
        panel.addProperty("height", config.panelHeight());
        panel.addProperty("theme", config.theme());
        panel.addProperty("font_size", config.fontSize());
        panel.addProperty("panel_opacity", config.panelOpacity());
        panel.addProperty("auto_launch_system", config.autoLaunchSystem());
        panel.addProperty("start_minimized", config.startMinimized());
        panel.addProperty("close_action", config.closeAction());
        panel.addProperty("confirm_on_exit", config.confirmOnExit());
        root.add("panel", panel);

        JsonArray instances = new JsonArray();
        for (String id : config.instanceIds()) {
            instances.add(id);
        }
        root.add("instances", instances);

        return root;
    }

    private PanelConfig fromJson(JsonObject root) {
        PanelConfig defaults = PanelConfig.defaults();
        JsonObject panelObj = getObject(root, "panel");

        double panelX = getDouble(panelObj, "x", defaults.panelX());
        double panelY = getDouble(panelObj, "y", defaults.panelY());
        double panelW = getDouble(panelObj, "width", defaults.panelWidth());
        double panelH = getDouble(panelObj, "height", defaults.panelHeight());
        String theme = getString(panelObj, "theme", defaults.theme());
        int fontSize = getInt(panelObj, "font_size", defaults.fontSize());
        double panelOpacity = getDouble(panelObj, "panel_opacity", defaults.panelOpacity());
        boolean autoLaunchSystem = getBoolean(panelObj, "auto_launch_system", defaults.autoLaunchSystem());
        boolean startMinimized = getBoolean(panelObj, "start_minimized", defaults.startMinimized());
        String closeAction = getString(panelObj, "close_action", defaults.closeAction());
        boolean confirmOnExit = getBoolean(panelObj, "confirm_on_exit", defaults.confirmOnExit());

        List<String> instanceIds = new ArrayList<>();
        if (root.has("instances") && root.get("instances").isJsonArray()) {
            for (var elem : root.getAsJsonArray("instances")) {
                if (elem.isJsonPrimitive()) {
                    instanceIds.add(elem.getAsString());
                }
            }
        }

        return new PanelConfig(panelX, panelY, panelW, panelH, theme, fontSize, panelOpacity, instanceIds,
                autoLaunchSystem, startMinimized, closeAction, confirmOnExit);
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
