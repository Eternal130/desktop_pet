package com.desktoppet.network;

import com.desktoppet.model.Envelope;
import com.desktoppet.model.RendererStats;
import com.google.gson.JsonObject;
import java.util.Optional;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.*;

/**
 * Contract test for the {@code get_stats}/{@code stats_state} protocol layer.
 * Locks the snake_case wire keys and the {@link RendererStats} round-trip
 * semantics shared with the C++ renderer (T3 {@code get_stats} handler).
 */
class StatsContractTest {

    @Test
    void testRoundTripFullyPopulated() {
        RendererStats original = new RendererStats(
            42.5,
            100_000_000L,
            73.2,
            "NVIDIA RTX 4070",
            512_000_000L,
            8_000_000_000L,
            1_700_000_000_000L
        );

        Envelope env = Protocol.createEvent(Protocol.EVENT_STATS_STATE, original.toJson());
        String wire = Protocol.serialize(env);
        Optional<Envelope> maybe = Protocol.deserialize(wire);

        assertTrue(maybe.isPresent(), "envelope must deserialize");
        Envelope parsed = maybe.get();
        assertEquals("event", parsed.type());
        assertEquals(Protocol.EVENT_STATS_STATE, parsed.action());

        RendererStats restored = RendererStats.fromJson(parsed.payload());

        assertEquals(original.cpuPercent(), restored.cpuPercent(), 0.0001);
        assertEquals(original.rssBytes(), restored.rssBytes());
        assertEquals(original.gpuPercent(), restored.gpuPercent(), 0.0001);
        assertEquals(original.gpuName(), restored.gpuName());
        assertEquals(original.vramUsedBytes(), restored.vramUsedBytes());
        assertEquals(original.vramTotalBytes(), restored.vramTotalBytes());
        assertEquals(original.timestampMs(), restored.timestampMs());
    }

    @Test
    void testNullFieldsPropagate() {
        RendererStats original = new RendererStats(
            10.0,
            5_000_000L,
            null,
            null,
            null,
            null,
            1_700_000_000_001L
        );

        Envelope env = Protocol.createEvent(Protocol.EVENT_STATS_STATE, original.toJson());
        String wire = Protocol.serialize(env);
        Optional<Envelope> maybe = Protocol.deserialize(wire);

        assertTrue(maybe.isPresent(), "envelope with null payload fields must deserialize");
        RendererStats restored = RendererStats.fromJson(maybe.get().payload());

        assertEquals(10.0, restored.cpuPercent(), 0.0001);
        assertEquals(5_000_000L, restored.rssBytes());
        assertNull(restored.gpuPercent(), "gpuPercent must round-trip as null without NPE");
        assertNull(restored.gpuName(), "gpuName must round-trip as null without NPE");
        assertNull(restored.vramUsedBytes(), "vramUsedBytes must round-trip as null without NPE");
        assertNull(restored.vramTotalBytes(), "vramTotalBytes must round-trip as null without NPE");
        assertEquals(1_700_000_000_001L, restored.timestampMs());
    }

    @Test
    void testAbsentNullableKeyTreatedAsNull() {
        JsonObject partial = new JsonObject();
        partial.addProperty("cpu_percent", 5.0);
        partial.addProperty("rss_bytes", 1_000L);
        partial.addProperty("timestamp_ms", 99L);

        RendererStats parsed = RendererStats.fromJson(partial);

        assertEquals(5.0, parsed.cpuPercent(), 0.0001);
        assertEquals(1_000L, parsed.rssBytes());
        assertEquals(99L, parsed.timestampMs());
        assertNull(parsed.gpuPercent(), "absent gpu_percent must parse as null");
        assertNull(parsed.gpuName(), "absent gpu_name must parse as null");
        assertNull(parsed.vramUsedBytes(), "absent vram_used_bytes must parse as null");
        assertNull(parsed.vramTotalBytes(), "absent vram_total_bytes must parse as null");
    }

    @Test
    void testGetStatsCommandEnvelopeShape() {
        Envelope cmd = Protocol.createCommand(Protocol.ACTION_GET_STATS, new JsonObject());
        String wire = Protocol.serialize(cmd);
        Optional<Envelope> maybe = Protocol.deserialize(wire);

        assertTrue(maybe.isPresent());
        Envelope parsed = maybe.get();
        assertEquals("command", parsed.type());
        assertEquals(Protocol.ACTION_GET_STATS, parsed.action());
        assertNotNull(parsed.id());
        assertFalse(parsed.id().isEmpty());
    }

    @Test
    void testErrorCodeConstantInRange() {
        assertEquals(9001, Protocol.ERROR_STATS_COLLECTION_FAILED);
        assertTrue(Protocol.ERROR_STATS_COLLECTION_FAILED >= 9000
                && Protocol.ERROR_STATS_COLLECTION_FAILED < 9100,
            "stats error codes must live in the 9000-9099 band");
    }
}
