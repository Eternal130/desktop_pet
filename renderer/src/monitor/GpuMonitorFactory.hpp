#pragma once

#include <memory>

#include "monitor/IGpuMonitor.hpp"

namespace Monitor {

/**
 * Construct the platform-appropriate IGpuMonitor.
 *
 *   _WIN32  : CompositeGpuMonitor (PdhGpuMonitor for GPU%, DxgiVramMonitor
 *            for VRAM + name). Returns nullptr if ENABLE_GPU_MONITOR=OFF.
 *   __linux__: StubGpuMonitor (placeholder; per plan T3).
 *   other   : nullptr (graceful null-path in the get_stats handler).
 *
 * Returned pointer may be null — callers must check before initialize().
 */
std::unique_ptr<IGpuMonitor> createGpuMonitor();

} // namespace Monitor
