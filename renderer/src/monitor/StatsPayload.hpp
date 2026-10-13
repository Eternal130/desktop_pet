#pragma once

#include <nlohmann/json.hpp>

#include "monitor/IGpuMonitor.hpp"
#include "monitor/ProcessStatsCollector.hpp"

namespace Monitor {

/**
 * Build the `stats_state` event payload from a process sample and an optional
 * GPU sample. Pure function: no I/O, no allocation of system handles — fully
 * unit-testable by injecting fake values.
 *
 * Wire contract (must stay byte-for-byte identical to the spec under
 * docs/protocol/):
 *   cpu_percent       — double
 *   rss_bytes         — uint64
 *   gpu_percent       — uint32 or null
 *   gpu_name          — string or null (null when IGpuMonitor absent)
 *   vram_used_bytes   — uint64 or null
 *   vram_total_bytes  — uint64 or null
 *   fps               — double (EMA-smoothed measured frame rate, 1 decimal;
 *                       -1 sentinel when no sample yet)
 *   timestamp_ms      — int64 (system_clock since epoch in ms)
 *
 * `gpu` may be nullptr (no monitor initialized on this platform / init failed)
 * — all gpu_* fields are then emitted as JSON null. `fps` < 0 (FpsCounter
 * sentinel before the first frame) is passed through as -1.
 */
nlohmann::json buildStatsPayload(ProcessStats ps, const GpuMetrics* gpu, double fps);

} // namespace Monitor
