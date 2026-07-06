#include "monitor/linux/StubGpuMonitor.hpp"

namespace Monitor {
namespace linux_ {

bool StubGpuMonitor::initialize() {
    return false;
}

GpuMetrics StubGpuMonitor::sample() {
    GpuMetrics m;
    m.gpuName = "Linux (monitoring not implemented)";
    return m;
}

} // namespace linux_
} // namespace Monitor
