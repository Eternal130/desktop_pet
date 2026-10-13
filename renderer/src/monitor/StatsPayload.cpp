#include "monitor/StatsPayload.hpp"

#include <chrono>
#include <cmath>

namespace Monitor {

namespace {

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace

nlohmann::json buildStatsPayload(ProcessStats ps, const GpuMetrics* gpu, double fps) {
    nlohmann::json payload;
    payload["cpu_percent"] = ps.cpuPercent;
    payload["rss_bytes"]    = ps.rssBytes;

    if (gpu) {
        payload["gpu_percent"] = gpu->gpuUtilizationPercent
            ? nlohmann::json(*gpu->gpuUtilizationPercent)
            : nlohmann::json(nullptr);
        payload["gpu_name"] = gpu->gpuName;
        payload["vram_used_bytes"] = gpu->vramUsedBytes
            ? nlohmann::json(*gpu->vramUsedBytes)
            : nlohmann::json(nullptr);
        payload["vram_total_bytes"] = gpu->vramTotalBytes
            ? nlohmann::json(*gpu->vramTotalBytes)
            : nlohmann::json(nullptr);
    } else {
        payload["gpu_percent"]      = nullptr;
        payload["gpu_name"]         = nullptr;
        payload["vram_used_bytes"]  = nullptr;
        payload["vram_total_bytes"] = nullptr;
    }

    // Measured frame rate (EMA over ~1s, fed by LAppDelegate::Run). One
    // decimal; -1 sentinel (no sample yet) passes through untouched.
    payload["fps"] = (fps >= 0.0)
        ? nlohmann::json(std::round(fps * 10.0) / 10.0)
        : nlohmann::json(-1.0);

    payload["timestamp_ms"] = nowMs();
    return payload;
}

} // namespace Monitor
