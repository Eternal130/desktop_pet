package com.desktoppet.network;

import com.desktoppet.model.Envelope;
import com.google.gson.Gson;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

import java.time.Instant;
import java.util.Optional;
import java.util.UUID;

public class Protocol {

    private static final Gson GSON = new Gson();

    private Protocol() {
    }

    public static String serialize(Envelope env) {
        JsonObject root = new JsonObject();
        root.addProperty("type", env.type());
        root.addProperty("action", env.action());
        root.addProperty("id", env.id());
        root.add("payload", env.payload() == null ? new JsonObject() : env.payload());
        root.addProperty("timestamp", env.timestamp());

        if ("response".equals(env.type())) {
            root.addProperty("success", env.success());
            root.addProperty("error_code", env.errorCode());
            root.addProperty("error_message", env.errorMessage());
        }

        return GSON.toJson(root);
    }

    public static Optional<Envelope> deserialize(String json) {
        if (json == null || json.isEmpty()) {
            return Optional.empty();
        }

        try {
            JsonElement parsed = JsonParser.parseString(json);
            if (!parsed.isJsonObject()) {
                return Optional.empty();
            }

            JsonObject root = parsed.getAsJsonObject();
            if (!root.has("type") || !root.has("action") || !root.has("id") || !root.has("payload") || !root.has("timestamp")) {
                return Optional.empty();
            }

            JsonElement payloadElement = root.get("payload");
            JsonObject payload = payloadElement != null && payloadElement.isJsonObject()
                ? payloadElement.getAsJsonObject()
                : new JsonObject();

            String type = root.get("type").getAsString();
            String action = root.get("action").getAsString();
            String id = root.get("id").getAsString();
            long timestamp = root.get("timestamp").getAsLong();

            Boolean success = null;
            Integer errorCode = null;
            String errorMessage = null;

            if ("response".equals(type)) {
                success = root.has("success") ? root.get("success").getAsBoolean() : true;
                errorCode = root.has("error_code") ? root.get("error_code").getAsInt() : 0;
                errorMessage = root.has("error_message") ? root.get("error_message").getAsString() : "";
            }

            return Optional.of(new Envelope(type, action, id, payload, timestamp, success, errorCode, errorMessage));
        } catch (Exception ignored) {
            return Optional.empty();
        }
    }

    public static Envelope createCommand(String action, JsonObject payload) {
        return new Envelope("command", action, generateId(), payloadOrEmpty(payload), currentTimestampMs(), null, null, null);
    }

    public static Envelope createEvent(String action, JsonObject payload) {
        return new Envelope("event", action, generateId(), payloadOrEmpty(payload), currentTimestampMs(), null, null, null);
    }

    public static Envelope createResponse(String originalId, String action, boolean success, int errorCode, String errorMessage) {
        return new Envelope("response", action, originalId, new JsonObject(), currentTimestampMs(), success, errorCode, errorMessage);
    }

    public static String generateId() {
        return UUID.randomUUID().toString();
    }

    private static long currentTimestampMs() {
        return Instant.now().toEpochMilli();
    }

    private static JsonObject payloadOrEmpty(JsonObject payload) {
        return payload == null ? new JsonObject() : payload;
    }
}
