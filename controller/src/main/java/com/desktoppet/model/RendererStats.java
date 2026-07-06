package com.desktoppet.model;

import com.google.gson.JsonObject;

/**
 * Resource usage snapshot for the renderer process, received via the
 * {@code stats_state} event. Boxed (nullable) fields represent metrics that may
 * be unavailable on some platforms (e.g. GPU/VRAM on the Linux stub).
 *
 * <p>Wire contract — snake_case JSON keys, matching
 * {@code docs/protocol/events.md} and the renderer {@code get_stats} handler:
 * {@code cpu_percent}, {@code rss_bytes}, {@code gpu_percent} (nullable),
 * {@code gpu_name}, {@code vram_used_bytes} (nullable),
 * {@code vram_total_bytes} (nullable), {@code timestamp_ms}. Nullable keys may
 * be either JSON {@code null} or absent on the wire; {@link #fromJson} accepts both.
 */
public record RendererStats(
    double cpuPercent,
    long rssBytes,
    Double gpuPercent,
    String gpuName,
    Long vramUsedBytes,
    Long vramTotalBytes,
    long timestampMs
) {
    public static RendererStats fromJson(JsonObject obj) {
        return new RendererStats(
            obj.get("cpu_percent").getAsDouble(),
            obj.get("rss_bytes").getAsLong(),
            hasNonNull(obj, "gpu_percent") ? obj.get("gpu_percent").getAsDouble() : null,
            hasNonNull(obj, "gpu_name") ? obj.get("gpu_name").getAsString() : null,
            hasNonNull(obj, "vram_used_bytes") ? obj.get("vram_used_bytes").getAsLong() : null,
            hasNonNull(obj, "vram_total_bytes") ? obj.get("vram_total_bytes").getAsLong() : null,
            obj.get("timestamp_ms").getAsLong()
        );
    }

    public JsonObject toJson() {
        JsonObject obj = new JsonObject();
        obj.addProperty("cpu_percent", cpuPercent);
        obj.addProperty("rss_bytes", rssBytes);
        obj.addProperty("gpu_percent", gpuPercent);
        obj.addProperty("gpu_name", gpuName);
        obj.addProperty("vram_used_bytes", vramUsedBytes);
        obj.addProperty("vram_total_bytes", vramTotalBytes);
        obj.addProperty("timestamp_ms", timestampMs);
        return obj;
    }

    private static boolean hasNonNull(JsonObject obj, String key) {
        return obj.has(key) && !obj.get(key).isJsonNull();
    }
}
