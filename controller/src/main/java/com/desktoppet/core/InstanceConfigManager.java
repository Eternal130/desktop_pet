package com.desktoppet.core;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;

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

    public void createConfig(int instanceId, String label, String rendererPath) {
        Path configPath = getConfigPath(instanceId);
        JsonObject root = new JsonObject();
        root.addProperty("instance_id", instanceId);
        root.addProperty("label", label);
        root.addProperty("renderer_path", rendererPath);
        writeJson(configPath, root);
        log.info("Created instance config: {}", configPath);
    }

    public JsonObject loadConfig(int instanceId) {
        Path configPath = getConfigPath(instanceId);
        if (!Files.exists(configPath)) {
            return new JsonObject();
        }
        try {
            String content = Files.readString(configPath);
            return JsonParser.parseString(content).getAsJsonObject();
        } catch (Exception e) {
            log.warn("Failed to load instance config {}: {}", instanceId, e.getMessage());
            return new JsonObject();
        }
    }

    public void saveConfig(int instanceId, JsonObject config) {
        writeJson(getConfigPath(instanceId), config);
    }

    public void deleteConfig(int instanceId) {
        Path configPath = getConfigPath(instanceId);
        try {
            if (Files.deleteIfExists(configPath)) {
                log.info("Deleted instance config: {}", configPath);
            }
        } catch (IOException e) {
            log.warn("Failed to delete instance config {}: {}", instanceId, e.getMessage());
        }
    }

    public Path getConfigPath(int instanceId) {
        return instancesDir.resolve(instanceId + ".json");
    }

    private void writeJson(Path path, JsonObject json) {
        try {
            Files.createDirectories(path.getParent());
            Files.writeString(path, gson.toJson(json));
        } catch (IOException e) {
            log.error("Failed to write config to {}: {}", path, e.getMessage());
        }
    }
}
