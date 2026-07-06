#pragma once

#include "monitor/IGpuMonitor.hpp"

#include <cstdint>

// Forward-declare PDH handles to avoid leaking <pdh.h> into the header.
typedef void* PDH_HQUERY;
typedef void* PDH_HCOUNTER;

namespace Monitor {
namespace win {

/**
 * Windows GPU utilization via PDH (Performance Data Helper).
 *
 * Reads the per-engine counter `\GPU Engine(*)\Utilization Percentage` and
 * sums the instances attributed to this process (filtered by `pid_<self>_`
 * prefix). Reliable on NVIDIA/Intel WDDM drivers; AMD Windows drivers may
 * report zero or omit the counter entirely — sample() then returns
 * gpuUtilizationPercent = nullopt (graceful degradation, not a crash).
 *
 * All methods run on the main render thread (project rule: no system API
 * calls from the WebSocket callback thread).
 */
class PdhGpuMonitor : public IGpuMonitor {
public:
    PdhGpuMonitor();
    ~PdhGpuMonitor() override;

    PdhGpuMonitor(const PdhGpuMonitor&) = delete;
    PdhGpuMonitor& operator=(const PdhGpuMonitor&) = delete;

    bool initialize() override;
    GpuMetrics sample() override;

private:
    void disposeQuery();
    void refreshCountersForPid();

    PDH_HQUERY           m_query     = nullptr;
    PDH_HCOUNTER*        m_counters  = nullptr;
    size_t               m_counterCount = 0;
    uint32_t             m_pid       = 0;
    bool                 m_initialized = false;
};

} // namespace win
} // namespace Monitor
