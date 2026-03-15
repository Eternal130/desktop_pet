package com.desktoppet.core;

import com.desktoppet.model.Envelope;
import com.desktoppet.model.HitAction;
import com.desktoppet.model.ModelConfig;
import com.desktoppet.network.Protocol;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.Locale;
import java.util.Map;
import java.util.function.Consumer;

public class InteractionHandler {

    private static final Logger log = LoggerFactory.getLogger(InteractionHandler.class);

    private static final Map<String, HitAction> DEFAULT_MAPPINGS = Map.of(
        "head", new HitAction("TapHead", 2),
        "body", new HitAction("TapBody", 2)
    );

    private final Consumer<String> messageSender;
    private ModelConfig modelConfig;

    public InteractionHandler(Consumer<String> messageSender) {
        this.messageSender = messageSender;
    }

    public void setModelConfig(ModelConfig config) {
        this.modelConfig = config;
    }

    public void handleHitEvent(Envelope hitEvent) {
        if (hitEvent == null || hitEvent.payload() == null) {
            return;
        }

        String areaId = extractAreaId(hitEvent.payload());
        if (areaId.isEmpty()) {
            return;
        }

        HitAction action = findHitAction(areaId);
        if (action == null) {
            log.debug("No hit action configured for area_id: {}", areaId);
            return;
        }

        JsonObject payload = new JsonObject();
        payload.addProperty("group", action.motionGroup());
        payload.addProperty("index", 0);
        payload.addProperty("priority", action.priority());

        Envelope command = Protocol.createCommand("play_motion", payload);
        messageSender.accept(Protocol.serialize(command));
        log.debug("play_motion sent for area_id={}, group={}", areaId, action.motionGroup());
    }

    private HitAction findHitAction(String areaId) {
        if (modelConfig != null && modelConfig.hitActions() != null) {
            HitAction action = modelConfig.hitActions().get(areaId);
            if (action != null) {
                return action;
            }

            String capitalized = capitalize(areaId);
            action = modelConfig.hitActions().get(capitalized);
            if (action != null) {
                return action;
            }
        }

        return DEFAULT_MAPPINGS.get(areaId.toLowerCase(Locale.ROOT));
    }

    private String extractAreaId(JsonObject payload) {
        if (!payload.has("area_id")) {
            return "";
        }

        JsonElement area = payload.get("area_id");
        if (area == null || area.isJsonNull()) {
            return "";
        }

        try {
            String value = area.getAsString();
            return value == null ? "" : value.trim();
        } catch (UnsupportedOperationException | ClassCastException | IllegalStateException ex) {
            return "";
        }
    }

    private String capitalize(String s) {
        if (s == null || s.isEmpty()) {
            return s;
        }
        return Character.toUpperCase(s.charAt(0)) + s.substring(1).toLowerCase(Locale.ROOT);
    }
}
