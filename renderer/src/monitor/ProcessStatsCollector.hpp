#pragma once

#include <cstdint>

namespace Monitor {

/**
 * Snapshot of this renderer process's host resource usage. Always populated
 * on Windows; Linux stub returns zeros (Tier 1 scope — see plan T3).
 */
struct ProcessStats {
    double cpuPercent = 0.0;  // [0.0, 100.0 * numCores]; caller may cap
    uint64_t rssBytes = 0;    // resident set size (WorkingSetSize on Win)
};

/**
 * Collects this-process CPU% and RSS. CPU% requires a delta between calls,
 * so the first sample() after construction always returns 0.0 (no baseline).
 *
 * Thread-safety: not thread-safe. Owned and called exclusively from the
 * main render thread (the get_stats command handler), per project rule
 * "no system API calls from the WebSocket callback thread".
 */
class ProcessStatsCollector {
public:
    ProcessStatsCollector();

    /** Non-const: updates internal baseline timestamps. */
    ProcessStats sample();

private:
    uint64_t m_prevProcessTime100ns = 0;  // FILETIME / proc time units
    double   m_prevWallTimeMs       = 0.0;
    unsigned m_processorCount       = 1;
    bool     m_hasBaseline          = false;
};

} // namespace Monitor
