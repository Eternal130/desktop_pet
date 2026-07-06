#pragma once

#include "monitor/IGpuMonitor.hpp"

#include <cstdint>
#include <string>

// Forward-declare to keep DXGI headers out of the public surface.
struct IDXGIAdapter3;

namespace Monitor {
namespace win {

/**
 * Windows VRAM usage via DXGI IDXGIAdapter3::QueryVideoMemoryInfo
 * (per-process LOCAL segment). The dedicated total comes from
 * IDXGIAdapter1::GetDesc1().DedicatedVideoMemory.
 *
 * Known limitation (plan TL;DR + Oracle BLOCKER-2): on the Vulkan variant,
 * DXGI per-process tracking may underreport or omit vkAllocateMemory
 * allocations — driver-dependent. The OpenGL variant is reliable. This is
 * a documented trade-off for keeping VulkanBackend.cpp untouched.
 */
class DxgiVramMonitor : public IGpuMonitor {
public:
    DxgiVramMonitor() = default;
    ~DxgiVramMonitor() override;

    DxgiVramMonitor(const DxgiVramMonitor&) = delete;
    DxgiVramMonitor& operator=(const DxgiVramMonitor&) = delete;

    bool initialize() override;
    GpuMetrics sample() override;

private:
    IDXGIAdapter3* m_adapter   = nullptr; // released in dtor
    uint64_t       m_vramTotal = 0;
    std::string    m_gpuName;
};

} // namespace win
} // namespace Monitor
