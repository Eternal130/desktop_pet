#include "monitor/ProcessStatsCollector.hpp"

#include <chrono>

#if defined(_WIN32)
#  include <windows.h>
#  include <psapi.h>
#else
#  include "LAppPal.hpp"
#endif

namespace Monitor {

ProcessStatsCollector::ProcessStatsCollector() {
#if defined(_WIN32)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    m_processorCount = si.dwNumberOfProcessors > 0 ? si.dwNumberOfProcessors : 1;
#else
    m_processorCount = 1;
#endif
}

ProcessStats ProcessStatsCollector::sample() {
    ProcessStats out;

#if defined(_WIN32)
    // ── RSS via WorkingSetSize ─────────────────────────────────────────
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        out.rssBytes = static_cast<uint64_t>(pmc.WorkingSetSize);
    }

    // ── CPU% via GetProcessTimes delta ─────────────────────────────────
    FILETIME ftCreate, ftExit, ftKernel, ftUser;
    if (GetProcessTimes(GetCurrentProcess(), &ftCreate, &ftExit, &ftKernel, &ftUser)) {
        // Combine kernel + user into a single 100ns-resolution cumulative counter.
        ULARGE_INTEGER k; k.LowPart = ftKernel.dwLowDateTime; k.HighPart = ftKernel.dwHighDateTime;
        ULARGE_INTEGER u; u.LowPart = ftUser.dwLowDateTime;   u.HighPart = ftUser.dwHighDateTime;
        const uint64_t now100ns = k.QuadPart + u.QuadPart;

        const double nowMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        if (m_hasBaseline && nowMs > m_prevWallTimeMs) {
            const double elapsedMs = nowMs - m_prevWallTimeMs;
            const uint64_t procDelta100ns = (now100ns > m_prevProcessTime100ns)
                ? (now100ns - m_prevProcessTime100ns) : 0ULL;
            // 100ns units → ms: divide by 10000. Then divide by elapsed and cores.
            const double procDeltaMs = static_cast<double>(procDelta100ns) / 10000.0;
            out.cpuPercent = (procDeltaMs / elapsedMs) * 100.0 / static_cast<double>(m_processorCount);
            if (out.cpuPercent < 0.0) out.cpuPercent = 0.0;
        }

        m_prevProcessTime100ns = now100ns;
        m_prevWallTimeMs       = nowMs;
        m_hasBaseline          = true;
    }
#else
    // Linux Tier 1: stub per plan (only Windows implements real collection).
    out.cpuPercent = 0.0;
    out.rssBytes   = 0;
    LAppPal::PrintLogLn("[ProcessStatsCollector] Linux stub sample (unimplemented)");
#endif

    return out;
}

} // namespace Monitor
