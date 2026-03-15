package com.desktoppet.core;

import com.desktoppet.model.ModelInfo;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;

public final class ModelInfoParser {

    private ModelInfoParser() {
    }

    public static Optional<ModelInfo> parse(Path model3JsonPath) {
        if (model3JsonPath == null || !Files.exists(model3JsonPath)) {
            return Optional.empty();
        }

        try {
            String json = Files.readString(model3JsonPath);
            JsonElement parsed = JsonParser.parseString(json);
            if (!parsed.isJsonObject()) {
                return Optional.empty();
            }

            JsonObject root = parsed.getAsJsonObject();
            Map<String, Integer> motionGroups = extractMotionGroups(root);
            List<String> expressions = extractExpressions(root);
            List<String> hitAreas = extractHitAreas(root);

            return Optional.of(new ModelInfo(motionGroups, expressions, hitAreas));
        } catch (IOException | RuntimeException ignored) {
            return Optional.empty();
        }
    }

    private static Map<String, Integer> extractMotionGroups(JsonObject root) {
        Map<String, Integer> groups = new LinkedHashMap<>();
        JsonObject motions = getNestedObject(root, "FileReferences", "Motions");
        if (motions == null) {
            return groups;
        }

        for (Map.Entry<String, JsonElement> entry : motions.entrySet()) {
            JsonElement value = entry.getValue();
            int count = value != null && value.isJsonArray() ? value.getAsJsonArray().size() : 0;
            groups.put(entry.getKey(), count);
        }

        return groups;
    }

    private static List<String> extractExpressions(JsonObject root) {
        List<String> expressions = new ArrayList<>();
        JsonObject fileRefs = getObject(root, "FileReferences");
        if (fileRefs == null || !fileRefs.has("Expressions") || !fileRefs.get("Expressions").isJsonArray()) {
            return expressions;
        }

        for (JsonElement item : fileRefs.getAsJsonArray("Expressions")) {
            if (item != null && item.isJsonObject()) {
                JsonObject expression = item.getAsJsonObject();
                if (expression.has("Name") && !expression.get("Name").isJsonNull()) {
                    expressions.add(expression.get("Name").getAsString());
                }
            }
        }

        return expressions;
    }

    private static List<String> extractHitAreas(JsonObject root) {
        List<String> hitAreas = new ArrayList<>();
        if (!root.has("HitAreas") || !root.get("HitAreas").isJsonArray()) {
            return hitAreas;
        }

        for (JsonElement item : root.getAsJsonArray("HitAreas")) {
            if (item != null && item.isJsonObject()) {
                JsonObject hitArea = item.getAsJsonObject();
                if (hitArea.has("Name") && !hitArea.get("Name").isJsonNull()) {
                    hitAreas.add(hitArea.get("Name").getAsString());
                }
            }
        }

        return hitAreas;
    }

    private static JsonObject getNestedObject(JsonObject root, String first, String second) {
        JsonObject firstObj = getObject(root, first);
        return firstObj == null ? null : getObject(firstObj, second);
    }

    private static JsonObject getObject(JsonObject obj, String key) {
        if (obj == null || !obj.has(key) || !obj.get(key).isJsonObject()) {
            return null;
        }
        return obj.getAsJsonObject(key);
    }
}
