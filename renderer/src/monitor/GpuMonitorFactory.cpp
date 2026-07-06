#include "monitor/GpuMonitorFactory.hpp"

#include "LAppPal.hpp"

#if defined(_WIN32) && defined(GPU_MONITOR_WIN)
#  include "monitor/win/PdhGpuMonitor.hpp"
#  include "monitor/win/DxgiVramMonitor.hpp"
#elif defined(__linux__) && defined(GPU_MONITOR_LINUX)
#  include "monitor/linux/StubGpuMonitor.hpp"
#endif

#include <utility>

namespace Monitor {

#if defined(_WIN32) && defined(GPU_MONITOR_WIN)

namespace {

/**
 * Merge a PDH (GPU%) monitor with a DXGI (VRAM + name) monitor into a single
 * IGpuMonitor view. PDH is authoritative for utilization; DXGI for memory and
 * the adapter's display name. If DXGI failed to initialize we still report
 * GPU% from PDH with an empty gpuName.
 */
class CompositeGpuMonitor : public IGpuMonitor {
public:
    CompositeGpuMonitor(std::unique_ptr<win::PdhGpuMonitor> pdh,
                        std::unique_ptr<win::DxgiVramMonitor> dxgi)
        : m_pdh(std::move(pdh)), m_dxgi(std::move(dxgi)) {}

    bool initialize() override {
        bool pdhOk  = m_pdh  && m_pdh->initialize();
        bool dxgiOk = m_dxgi && m_dxgi->initialize();
        if (!pdhOk) {
            LAppPal::PrintLogLn("[CompositeGpuMonitor] PDH init failed — gpu_percent will be null");
        }
        if (!dxgiOk) {
            LAppPal::PrintLogLn("[CompositeGpuMonitor] DXGI init failed — vram fields will be null");
        }
        return pdhOk || dxgiOk;
    }

    GpuMetrics sample() override {
        GpuMetrics out;
        if (m_pdh) {
            const GpuMetrics g = m_pdh->sample();
            out.gpuUtilizationPercent = g.gpuUtilizationPercent;
        }
        if (m_dxgi) {
            const GpuMetrics d = m_dxgi->sample();
            out.vramUsedBytes  = d.vramUsedBytes;
            out.vramTotalBytes = d.vramTotalBytes;
            if (!d.gpuName.empty()) out.gpuName = d.gpuName;
        }
        return out;
    }

private:
    std::unique_ptr<win::PdhGpuMonitor>   m_pdh;
    std::unique_ptr<win::DxgiVramMonitor> m_dxgi;
};

} // namespace

std::unique_ptr<IGpuMonitor> createGpuMonitor() {
    auto pdh = std::make_unique<win::PdhGpuMonitor>();
    auto dxgi = std::make_unique<win::DxgiVramMonitor>();
    return std::make_unique<CompositeGpuMonitor>(std::move(pdh), std::move(dxgi));
}

#elif defined(__linux__) && defined(GPU_MONITOR_LINUX)

std::unique_ptr<IGpuMonitor> createGpuMonitor() {
    return std::make_unique<linux_::StubGpuMonitor>();
}

#else

std::unique_ptr<IGpuMonitor> createGpuMonitor() {
    LAppPal::PrintLogLn("[GpuMonitorFactory] no GPU monitor backend compiled for this platform");
    return nullptr;
}

#endif

} // namespace Monitor
