package com.desktoppet.network;

import com.desktoppet.model.Envelope;
import com.desktoppet.model.SubtitleStyle;
import com.google.gson.Gson;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

import java.time.Instant;
import java.util.Optional;
import java.util.UUID;

public class Protocol {

    private static final Gson GSON = new Gson();

    // Protocol action/event/error constants (must mirror renderer/src/network/Protocol.hpp)
    public static final String ACTION_GET_STATS = "get_stats";
    public static final String EVENT_STATS_STATE = "stats_state";
    public static final int ERROR_STATS_COLLECTION_FAILED = 9001;

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

    /**
     * Builds a "show_subtitle" command. Payload keys are snake_case (marginV keeps
     * ASS-native camelCase as it mirrors the ASS directive name). Colors are RRGGBBTT
     * uint32 values — the renderer converts to ASS AABBGGRR internally.
     */
    public static Envelope showSubtitle(String text, SubtitleStyle style, long durationMs) {
        JsonObject payload = new JsonObject();
        payload.addProperty("text", text);
        payload.addProperty("duration", durationMs);
        payload.addProperty("font_name", style.fontName());
        payload.addProperty("font_size", style.fontSize());
        payload.addProperty("primary_color", style.primaryColor());
        payload.addProperty("outline_color", style.outlineColor());
        payload.addProperty("outline_width", style.outlineWidth());
        payload.addProperty("shadow_color", style.shadowColor());
        payload.addProperty("shadow_depth", style.shadowDepth());
        payload.addProperty("alignment", style.alignment());
        payload.addProperty("marginV", style.marginV());
        payload.addProperty("edge_blur", style.edgeBlur());
        payload.addProperty("font_weight", style.fontWeight());
        payload.addProperty("letter_spacing", style.letterSpacing());
        payload.addProperty("bg_box_enabled", style.bgBoxEnabled());
        payload.addProperty("bg_box_color", style.bgBoxColor());
        payload.addProperty("bg_box_padding_x", style.bgBoxPaddingX());
        payload.addProperty("bg_box_padding_y", style.bgBoxPaddingY());
        return createCommand("show_subtitle", payload);
    }

    public static Envelope hideSubtitle() {
        return createCommand("hide_subtitle", new JsonObject());
    }

    public static Envelope setSubtitleStyle(SubtitleStyle style) {
        JsonObject payload = new JsonObject();
        payload.addProperty("font_name", style.fontName());
        payload.addProperty("font_size", style.fontSize());
        payload.addProperty("primary_color", style.primaryColor());
        payload.addProperty("outline_color", style.outlineColor());
        payload.addProperty("outline_width", style.outlineWidth());
        payload.addProperty("shadow_color", style.shadowColor());
        payload.addProperty("shadow_depth", style.shadowDepth());
        payload.addProperty("alignment", style.alignment());
        payload.addProperty("marginV", style.marginV());
        payload.addProperty("edge_blur", style.edgeBlur());
        payload.addProperty("font_weight", style.fontWeight());
        payload.addProperty("letter_spacing", style.letterSpacing());
        payload.addProperty("bg_box_enabled", style.bgBoxEnabled());
        payload.addProperty("bg_box_color", style.bgBoxColor());
        payload.addProperty("bg_box_padding_x", style.bgBoxPaddingX());
        payload.addProperty("bg_box_padding_y", style.bgBoxPaddingY());
        return createCommand("set_subtitle_style", payload);
    }

    public static Envelope setSubtitleAdjustMode(boolean enabled) {
        JsonObject payload = new JsonObject();
        payload.addProperty("enabled", enabled);
        return createCommand("set_subtitle_adjust_mode", payload);
    }

    public static Envelope setSubtitleLayout(double offsetX, double offsetY,
            int areaWidth, int areaHeight, double fontSize) {
        JsonObject payload = new JsonObject();
        payload.addProperty("offset_x", offsetX);
        payload.addProperty("offset_y", offsetY);
        payload.addProperty("area_width", areaWidth);
        payload.addProperty("area_height", areaHeight);
        payload.addProperty("font_size", fontSize);
        return createCommand("set_subtitle_layout", payload);
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
