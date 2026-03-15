package com.desktoppet.network;

import com.desktoppet.model.Envelope;
import com.google.gson.Gson;
import com.google.gson.JsonObject;
import java.util.Optional;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.*;

class ProtocolTest {

    private static final Gson GSON = new Gson();

    @Test
    void serializeCommand_includedRequiredFields() {
        JsonObject payload = new JsonObject();
        payload.addProperty("foo", "bar");
        Envelope env = new Envelope("command", "play_motion", "cmd-1", payload, 1710000000000L, null, null, null);

        String json = Protocol.serialize(env);
        JsonObject root = GSON.fromJson(json, JsonObject.class);

        assertEquals("command", root.get("type").getAsString());
        assertEquals("play_motion", root.get("action").getAsString());
        assertEquals("cmd-1", root.get("id").getAsString());
        assertEquals(1710000000000L, root.get("timestamp").getAsLong());
        assertTrue(root.has("payload"));
        assertEquals("bar", root.getAsJsonObject("payload").get("foo").getAsString());
    }

    @Test
    void serializeEvent_producesValidJson() {
        JsonObject payload = new JsonObject();
        payload.addProperty("area_id", "Head");
        Envelope env = new Envelope("event", "hit", "evt-1", payload, 1710000000001L, null, null, null);

        String json = Protocol.serialize(env);
        JsonObject root = GSON.fromJson(json, JsonObject.class);

        assertEquals("event", root.get("type").getAsString());
        assertEquals("hit", root.get("action").getAsString());
        assertEquals("evt-1", root.get("id").getAsString());
        assertEquals("Head", root.getAsJsonObject("payload").get("area_id").getAsString());
        assertFalse(root.has("success"));
        assertFalse(root.has("error_code"));
        assertFalse(root.has("error_message"));
    }

    @Test
    void serializeResponse_fieldsAtTopLevel() {
        Envelope env = Protocol.createResponse("123", "load_model", true, 0, "");

        String json = Protocol.serialize(env);
        JsonObject root = GSON.fromJson(json, JsonObject.class);
        JsonObject payload = root.getAsJsonObject("payload");

        assertTrue(root.has("success"));
        assertTrue(root.has("error_code"));
        assertTrue(root.has("error_message"));
        assertNotNull(payload);
        assertFalse(payload.has("success"));
        assertFalse(payload.has("error_code"));
        assertFalse(payload.has("error_message"));
    }

    @Test
    void serializeResponse_payloadIsEmptyObject() {
        Envelope env = Protocol.createResponse("123", "load_model", true, 0, "");

        String json = Protocol.serialize(env);
        JsonObject root = GSON.fromJson(json, JsonObject.class);

        assertTrue(root.has("payload"));
        JsonObject payload = root.getAsJsonObject("payload");
        assertNotNull(payload);
        assertEquals(0, payload.size());
    }

    @Test
    void deserializeCommand_correctlyParsed() {
        String json = """
            {"type":"command","action":"play_motion","id":"abc","payload":{"group":"TapHead"},"timestamp":1710000000000}
            """;

        Optional<Envelope> maybe = Protocol.deserialize(json);

        assertTrue(maybe.isPresent());
        Envelope env = maybe.get();
        assertEquals("command", env.type());
        assertEquals("play_motion", env.action());
        assertEquals("abc", env.id());
        assertEquals(1710000000000L, env.timestamp());
        assertEquals("TapHead", env.payload().get("group").getAsString());
    }

    @Test
    void deserializeResponse_topLevelFields() {
        String json = """
            {"type":"response","action":"load_model","id":"r-1","payload":{},"timestamp":1710000000000,"success":false,"error_code":42,"error_message":"bad model"}
            """;

        Optional<Envelope> maybe = Protocol.deserialize(json);

        assertTrue(maybe.isPresent());
        Envelope env = maybe.get();
        assertEquals("response", env.type());
        assertFalse(env.success());
        assertEquals(42, env.errorCode());
        assertEquals("bad model", env.errorMessage());
    }

    @Test
    void deserializeNonResponse_successIsNull() {
        String json = """
            {"type":"event","action":"ready","id":"e-1","payload":{},"timestamp":1710000000000}
            """;

        Optional<Envelope> maybe = Protocol.deserialize(json);

        assertTrue(maybe.isPresent());
        Envelope env = maybe.get();
        assertNull(env.success());
        assertNull(env.errorCode());
        assertNull(env.errorMessage());
    }

    @Test
    void deserializeMissingField_returnsEmpty() {
        String missingType = """
            {"action":"x","id":"1","payload":{},"timestamp":1710000000000}
            """;
        String missingAction = """
            {"type":"command","id":"1","payload":{},"timestamp":1710000000000}
            """;

        assertTrue(Protocol.deserialize(missingType).isEmpty());
        assertTrue(Protocol.deserialize(missingAction).isEmpty());
    }

    @Test
    void deserializeInvalidJson_returnsEmpty() {
        Optional<Envelope> maybe = Protocol.deserialize("not-json");
        assertTrue(maybe.isEmpty());
    }

    @Test
    void deserializeEmptyString_returnsEmpty() {
        assertTrue(Protocol.deserialize("").isEmpty());
    }

    @Test
    void crossCompatibility_cppResponseFormat() {
        String cppJson =
            "{\"type\":\"response\",\"action\":\"load_model\",\"id\":\"123\",\"payload\":{},\"timestamp\":1710000000000,\"success\":true,\"error_code\":0,\"error_message\":\"\"}";

        Optional<Envelope> maybe = Protocol.deserialize(cppJson);

        assertTrue(maybe.isPresent());
        Envelope env = maybe.get();
        assertEquals("load_model", env.action());
        assertTrue(env.success());
        assertEquals(0, env.errorCode());
        assertEquals("", env.errorMessage());
    }

    @Test
    void generateId_isUnique() {
        String id1 = Protocol.generateId();
        String id2 = Protocol.generateId();

        assertNotNull(id1);
        assertNotNull(id2);
        assertNotEquals(id1, id2);
    }
}
