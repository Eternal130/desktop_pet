#include "monitor/ProcessStatsCollector.hpp"
#include "LAppPal.hpp"

#include <chrono>

#if defined(_WIN32)
#  include <windows.h>
#  include <psapi.h>
#else
#  include <unistd.h>  // sysconf, _SC_NPROCESSORS_ONLN/_SC_CLK_TCK/_SC_PAGESIZE
#  include <fstream>   // std::ifstream for /proc reads
#  include <sstream>   // std::istringstream field splitting
#  include <string>
#endif

namespace Monitor {

ProcessStatsCollector::ProcessStatsCollector() {
#if defined(_WIN32)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    m_processorCount = si.dwNumberOfProcessors > 0 ? si.dwNumberOfProcessors : 1;
#else
    // Core count cannot change at runtime — cached in the member, mirroring
    // the GetSystemInfo path above.
    const long cores = sysconf(_SC_NPROCESSORS_ONLN);
    m_processorCount = (cores > 0) ? static_cast<unsigned>(cores) : 1;
#endif
}

ProcessStats ProcessStatsCollector::sample() {
    ProcessStats out;

#if defined(_WIN32)
    {
        struct PmcEx2 {
            DWORD cb; DWORD PageFaultCount;
            SIZE_T PeakWorkingSetSize, WorkingSetSize;
            SIZE_T QuotaPeakPagedPoolUsage, QuotaPagedPoolUsage;
            SIZE_T QuotaPeakNonPagedPoolUsage, QuotaNonPagedPoolUsage;
            SIZE_T PagefileUsage, PeakPagefileUsage, PrivateUsage, PrivateWorkingSetSize;
            ULONGLONG SharedCommitUsage;
        };
        PmcEx2 pmc2{};
        pmc2.cb = sizeof(pmc2);
        if (GetProcessMemoryInfo(GetCurrentProcess(),
                reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc2), sizeof(pmc2))
            && pmc2.PrivateWorkingSetSize > 0) {
            out.rssBytes = static_cast<uint64_t>(pmc2.PrivateWorkingSetSize);
        } else {
            PROCESS_MEMORY_COUNTERS pmc{};
            if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
                out.rssBytes = static_cast<uint64_t>(pmc.WorkingSetSize);
            }
        }
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
    // Linux: /proc-based sampling, structurally isomorphic with the Windows
    // branch above. Never-throws contract: any parse failure keeps the
    // zero-initialized `out`, leaves the previous baseline intact, and only
    // logs on failure (success stays silent to avoid per-frame log spam).
    try {
        bool statmOk = false;
        bool statOk = false;

        // ── RSS via /proc/self/statm field 2 (resident pages) ────────────
        // Plain RSS corresponds to the Windows WorkingSetSize fallback;
        // PrivateWorkingSetSize has no direct /proc equivalent.
        {
            std::ifstream statmFile("/proc/self/statm");
            unsigned long long totalPages = 0;
            unsigned long long residentPages = 0;
            const long pageSize = sysconf(_SC_PAGESIZE);
            if (pageSize > 0 && statmFile >> totalPages >> residentPages
                && residentPages > 0) {
                out.rssBytes = residentPages * static_cast<unsigned long long>(pageSize);
                statmOk = true;
            }
        }

        // ── CPU% via /proc/self/stat utime+stime tick delta ──────────────
        // comm (field 2) may contain spaces/parentheses, so parse only what
        // follows the LAST ')'. Tokens there: state ppid pgrp session tty_nr
        // tpgid flags minflt cminflt majflt cmajflt utime stime — i.e. skip
        // 11 tokens, then utime/stime are fields 14+15 of the whole line.
        {
            std::ifstream statFile("/proc/self/stat");
            std::string statLine;
            if (std::getline(statFile, statLine)) {
                const std::size_t closeParen = statLine.rfind(')');
                if (closeParen != std::string::npos) {
                    std::istringstream after(statLine.substr(closeParen + 1));
                    std::string skip;
                    unsigned long long utimeTicks = 0;
                    unsigned long long stimeTicks = 0;
                    bool skipped = true;
                    for (int i = 0; i < 11; ++i) {
                        if (!(after >> skip)) { skipped = false; break; }
                    }
                    if (skipped && (after >> utimeTicks >> stimeTicks)) {
                        statOk = true;
                        // Cumulative process time. Stored in m_prevProcessTime100ns
                        // ("proc time units": 100ns on Windows, CLK_TCK ticks here).
                        const unsigned long long nowTicks = utimeTicks + stimeTicks;

                        const double nowMs = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now().time_since_epoch()).count();

                        if (m_hasBaseline && nowMs > m_prevWallTimeMs) {
                            const double elapsedMs = nowMs - m_prevWallTimeMs;
                            const unsigned long long deltaTicks = (nowTicks > m_prevProcessTime100ns)
                                ? (nowTicks - m_prevProcessTime100ns) : 0ULL;
                            // ticks → ms via CLK_TCK. Then divide by elapsed and cores.
                            static const long clkTck = sysconf(_SC_CLK_TCK);
                            const double ticksPerSec = (clkTck > 0)
                                ? static_cast<double>(clkTck) : 100.0;
                            const double procDeltaMs = static_cast<double>(deltaTicks) * 1000.0 / ticksPerSec;
                            out.cpuPercent = (procDeltaMs / elapsedMs) * 100.0
                                / static_cast<double>(m_processorCount);
                            if (out.cpuPercent < 0.0) out.cpuPercent = 0.0;
                        }

                        m_prevProcessTime100ns = nowTicks;
                        m_prevWallTimeMs       = nowMs;
                        m_hasBaseline          = true;
                    }
                }
            }
        }

        if (!statmOk || !statOk) {
            LAppPal::PrintLogLn(
                "[ProcessStatsCollector] Linux /proc parse failed (statm=%s, stat=%s); returning zero/last values",
                statmOk ? "ok" : "fail", statOk ? "ok" : "fail");
        }
    } catch (...) {
        // never-throws contract: /proc parsing must never take down the render
        // loop. Fall back to zero/last values. Baseline members are only ever
        // written as one consecutive group of non-throwing assignments, so
        // they stay consistent for the next sample even after an exception.
        LAppPal::PrintLogLn(
            "[ProcessStatsCollector] Linux sample failed (exception); returning zero/last values");
    }
#endif

    return out;
}

} // namespace Monitor
