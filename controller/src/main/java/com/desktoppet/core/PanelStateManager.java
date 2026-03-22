package com.desktoppet.core;

import com.desktoppet.model.InstanceState;
import com.desktoppet.model.PanelState;
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

public class PanelStateManager {

    private static final Logger log = LoggerFactory.getLogger(PanelStateManager.class);

    private final Path statePath;
    private final Gson gson;

    public PanelStateManager() {
        this(Path.of(System.getProperty("user.home"), ".config", "desktop-pet", "panel-state.json"));
    }

    public PanelStateManager(Path statePath) {
        this.statePath = statePath;
        this.gson = new GsonBuilder().setPrettyPrinting().create();
    }

    public PanelState load() {
        PanelState defaults = PanelState.defaults();

        if (!Files.exists(statePath)) {
            return defaults;
        }

        try {
            String content = Files.readString(statePath);
            JsonObject root = JsonParser.parseString(content).getAsJsonObject();
            return mergeWithDefaults(root, defaults);
        } catch (Exception e) {
            log.warn("Failed to load panel state from {}, using defaults", statePath, e);
            return defaults;
        }
    }

    public void save(PanelState state) {
        try {
            if (statePath.getParent() != null) {
                Files.createDirectories(statePath.getParent());
            }
            JsonObject root = toJson(state);
            Files.writeString(statePath, gson.toJson(root));
            log.info("Panel state saved to {}", statePath);
        } catch (IOException e) {
            log.error("Failed to save panel state to {}", statePath, e);
        }
    }

    public String getStatePath() {
        return statePath.toString();
    }

    private PanelState mergeWithDefaults(JsonObject root, PanelState defaults) {
        JsonObject panelObj = getObject(root, "panel");
        double panelX = getDouble(panelObj, "x", defaults.panelX());
        double panelY = getDouble(panelObj, "y", defaults.panelY());
        double panelWidth = getDouble(panelObj, "width", defaults.panelWidth());
        double panelHeight = getDouble(panelObj, "height", defaults.panelHeight());
        String theme = getString(panelObj, "theme", defaults.theme());

        List<InstanceState> instanceStates = new ArrayList<>();
        if (root.has("instances") && root.get("instances").isJsonArray()) {
            JsonArray arr = root.getAsJsonArray("instances");
            InstanceState instDefaults = InstanceState.defaults();
            for (var elem : arr) {
                if (!elem.isJsonObject()) continue;
                JsonObject obj = elem.getAsJsonObject();
                instanceStates.add(new InstanceState(
                    getString(obj, "label", instDefaults.label()),
                    getString(obj, "model", instDefaults.model()),
                    getString(obj, "renderer_path", instDefaults.rendererPath()),
                    getDouble(obj, "opacity", instDefaults.opacity()),
                    getString(obj, "drag_mode", instDefaults.dragMode()),
                    getInt(obj, "idle_interval", instDefaults.idleInterval()),
                    getInt(obj, "pos_x", instDefaults.posX()),
                    getInt(obj, "pos_y", instDefaults.posY()),
                    getInt(obj, "window_width", instDefaults.windowWidth()),
                    getInt(obj, "window_height", instDefaults.windowHeight()),
                    getBoolean(obj, "auto_start", instDefaults.autoStart()),
                    getString(obj, "current_expression", instDefaults.currentExpression()),
                    getInt(obj, "target_fps", instDefaults.targetFps())
                ));
            }
        }

        return new PanelState(panelX, panelY, panelWidth, panelHeight, theme, instanceStates);
    }

    private JsonObject toJson(PanelState state) {
        JsonObject root = new JsonObject();

        JsonObject panel = new JsonObject();
        panel.addProperty("x", state.panelX());
        panel.addProperty("y", state.panelY());
        panel.addProperty("width", state.panelWidth());
        panel.addProperty("height", state.panelHeight());
        panel.addProperty("theme", state.theme());
        root.add("panel", panel);

        JsonArray instances = new JsonArray();
        for (InstanceState inst : state.instances()) {
            JsonObject obj = new JsonObject();
            obj.addProperty("label", inst.label());
            obj.addProperty("model", inst.model());
            obj.addProperty("renderer_path", inst.rendererPath());
            obj.addProperty("opacity", inst.opacity());
            obj.addProperty("drag_mode", inst.dragMode());
            obj.addProperty("idle_interval", inst.idleInterval());
            obj.addProperty("pos_x", inst.posX());
            obj.addProperty("pos_y", inst.posY());
            obj.addProperty("window_width", inst.windowWidth());
            obj.addProperty("window_height", inst.windowHeight());
            obj.addProperty("auto_start", inst.autoStart());
            obj.addProperty("current_expression", inst.currentExpression());
            obj.addProperty("target_fps", inst.targetFps());
            instances.add(obj);
        }
        root.add("instances", instances);

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
