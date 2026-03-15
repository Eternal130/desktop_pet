package com.desktoppet.core;

import com.desktoppet.model.Envelope;
import com.desktoppet.model.HitAction;
import com.desktoppet.model.ModelConfig;
import com.desktoppet.network.Protocol;
import com.google.gson.JsonNull;
import com.google.gson.JsonObject;
import org.junit.jupiter.api.Test;
import org.mockito.ArgumentCaptor;

import java.util.List;
import java.util.Map;
import java.util.function.Consumer;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.verifyNoInteractions;

class InteractionHandlerTest {

    @Test
    void hitHead_withModelConfig_sendsPlayMotion() {
        Consumer<String> messageSender = mock(Consumer.class);
        InteractionHandler handler = new InteractionHandler(messageSender);
        handler.setModelConfig(new ModelConfig(
            "Hiyori",
            Map.of("Head", new HitAction("TapHead", 2)),
            List.of(),
            ""
        ));

        handler.handleHitEvent(hitEventWithArea("head"));

        Envelope command = captureSentCommand(messageSender);
        assertEquals("command", command.type());
        assertEquals("play_motion", command.action());
        assertEquals("TapHead", command.payload().get("group").getAsString());
        assertEquals(0, command.payload().get("index").getAsInt());
        assertEquals(2, command.payload().get("priority").getAsInt());
    }

    @Test
    void hitBody_defaultMapping_sendsPlayMotion() {
        Consumer<String> messageSender = mock(Consumer.class);
        InteractionHandler handler = new InteractionHandler(messageSender);

        handler.handleHitEvent(hitEventWithArea("body"));

        Envelope command = captureSentCommand(messageSender);
        assertEquals("play_motion", command.action());
        assertEquals("TapBody", command.payload().get("group").getAsString());
        assertEquals(0, command.payload().get("index").getAsInt());
        assertEquals(2, command.payload().get("priority").getAsInt());
    }

    @Test
    void hitHead_defaultMapping_sendsPlayMotion() {
        Consumer<String> messageSender = mock(Consumer.class);
        InteractionHandler handler = new InteractionHandler(messageSender);

        handler.handleHitEvent(hitEventWithArea("head"));

        Envelope command = captureSentCommand(messageSender);
        assertEquals("play_motion", command.action());
        assertEquals("TapHead", command.payload().get("group").getAsString());
        assertEquals(0, command.payload().get("index").getAsInt());
        assertEquals(2, command.payload().get("priority").getAsInt());
    }

    @Test
    void unknownAreaId_noMessageSent() {
        Consumer<String> messageSender = mock(Consumer.class);
        InteractionHandler handler = new InteractionHandler(messageSender);

        handler.handleHitEvent(hitEventWithArea("unknown_area"));

        verifyNoInteractions(messageSender);
    }

    @Test
    void hitEventWithoutAreaId_noMessageSent() {
        Consumer<String> messageSender = mock(Consumer.class);
        InteractionHandler handler = new InteractionHandler(messageSender);

        JsonObject emptyAreaPayload = new JsonObject();
        emptyAreaPayload.addProperty("area_id", "");
        JsonObject nullAreaPayload = new JsonObject();
        nullAreaPayload.add("area_id", JsonNull.INSTANCE);

        assertDoesNotThrow(() -> handler.handleHitEvent(hitEvent(emptyAreaPayload)));
        assertDoesNotThrow(() -> handler.handleHitEvent(hitEvent(nullAreaPayload)));

        verifyNoInteractions(messageSender);
    }

    private Envelope captureSentCommand(Consumer<String> messageSender) {
        ArgumentCaptor<String> captor = ArgumentCaptor.forClass(String.class);
        verify(messageSender).accept(captor.capture());
        return Protocol.deserialize(captor.getValue())
            .filter(envelope -> "command".equals(envelope.type()))
            .orElseThrow(() -> new AssertionError("Expected serialized command envelope"));
    }

    private Envelope hitEventWithArea(String areaId) {
        JsonObject payload = new JsonObject();
        payload.addProperty("area_id", areaId);
        return hitEvent(payload);
    }

    private Envelope hitEvent(JsonObject payload) {
        Envelope event = new Envelope("event", "hit", "evt-1", payload, 1710000000000L, null, null, null);
        assertTrue(event.payload().isJsonObject());
        return event;
    }
}
