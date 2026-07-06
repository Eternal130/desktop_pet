#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace Monitor {

/**
 * Snapshot of GPU-related metrics. Optional fields model platform- and
 * driver-dependent availability (e.g. Linux stub leaves everything nullopt
 * except gpuName; AMD/Windows PDH may leave gpuUtilizationPercent nullopt).
 *
 * Wire contract: these feed the `stats_state` event payload and must stay
 * byte-for-byte consistent with controller model/RendererStats.java.
 */
struct GpuMetrics {
    std::optional<double> gpuUtilizationPercent;
    std::optional<uint64_t> vramUsedBytes;
    std::optional<uint64_t> vramTotalBytes;
    std::string gpuName;
};

/**
 * Platform-abstract GPU monitor. Implementations are responsible for any
 * per-instance warmup required by their backing API (PDH needs two collects;
 * DXGI needs no warmup). All methods are invoked from the main render thread
 * by the get_stats command handler — never from the WebSocket callback thread.
 */
class IGpuMonitor {
public:
    virtual ~IGpuMonitor() = default;

    /**
     * Perform one-time initialization (open queries, capture adapter, prime
     * rate counters). Returns false if the backing API is unavailable; the
     * caller will then report all GPU fields as null in the stats payload.
     */
    virtual bool initialize() = 0;

    /** Collect a fresh sample. May return nullopt fields if the metric is
     *  currently unavailable (transient PDH failure, driver gap, etc.). */
    virtual GpuMetrics sample() = 0;
};

} // namespace Monitor
