package com.desktoppet.core;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

public class HitAreaCacheManager {

    private static final Logger log = LoggerFactory.getLogger(HitAreaCacheManager.class);

    private final Path cachePath;
    private final Gson gson;
    private final Map<String, List<String>> cache;

    public HitAreaCacheManager() {
        this(Path.of(System.getProperty("user.home"), ".config", "desktop-pet", "hit_area_cache.json"));
    }

    HitAreaCacheManager(Path cachePath) {
        this.cachePath = cachePath;
        this.gson = new GsonBuilder().setPrettyPrinting().create();
        this.cache = load();
    }

    public List<String> getHitAreas(String modelName) {
        return cache.getOrDefault(modelName, List.of());
    }

    public void updateHitAreas(String modelName, List<String> hitAreas) {
        cache.put(modelName, List.copyOf(hitAreas));
        save();
    }

    private Map<String, List<String>> load() {
        if (!Files.exists(cachePath)) {
            return new LinkedHashMap<>();
        }

        try {
            String content = Files.readString(cachePath);
            JsonElement parsed = JsonParser.parseString(content);
            if (!parsed.isJsonObject()) {
                return new LinkedHashMap<>();
            }

            Map<String, List<String>> result = new LinkedHashMap<>();
            JsonObject root = parsed.getAsJsonObject();
            for (Map.Entry<String, JsonElement> entry : root.entrySet()) {
                if (entry.getValue() != null && entry.getValue().isJsonArray()) {
                    List<String> areas = new ArrayList<>();
                    for (JsonElement item : entry.getValue().getAsJsonArray()) {
                        if (item != null && item.isJsonPrimitive()) {
                            areas.add(item.getAsString());
                        }
                    }
                    result.put(entry.getKey(), Collections.unmodifiableList(areas));
                }
            }
            return result;
        } catch (IOException | RuntimeException e) {
            log.warn("Failed to load hit area cache from {}: {}", cachePath, e.getMessage());
            return new LinkedHashMap<>();
        }
    }

    private void save() {
        try {
            if (cachePath.getParent() != null) {
                Files.createDirectories(cachePath.getParent());
            }

            JsonObject root = new JsonObject();
            for (Map.Entry<String, List<String>> entry : cache.entrySet()) {
                JsonArray array = new JsonArray();
                for (String area : entry.getValue()) {
                    array.add(area);
                }
                root.add(entry.getKey(), array);
            }

            Files.writeString(cachePath, gson.toJson(root));
        } catch (IOException e) {
            log.error("Failed to save hit area cache to {}: {}", cachePath, e.getMessage());
        }
    }
}
