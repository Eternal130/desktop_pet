#pragma once

#include "monitor/IGpuMonitor.hpp"

#include <string>

namespace Monitor {
namespace linux_ {

/**
 * Linux placeholder. Real GPU/VRAM collection (sysfs / NVML / fdinfo) is
 * deferred past Tier 1 (plan T3 scope). initialize() returns false; sample()
 * returns an empty GpuMetrics whose gpuName is a sentinel string the UI can
 * render as "不可用".
 */
class StubGpuMonitor : public IGpuMonitor {
public:
    bool initialize() override;
    GpuMetrics sample() override;
};

} // namespace linux_
} // namespace Monitor
