#pragma once

#include <QtGlobal>      // qint64
#include <QString>
#include <functional>

// ResourceStatsCollector (Wave 8 todo 17) — self-resource collection via
// platform APIs. Replaces the JavaFX controller's OSHI-based
// `core/ResourceStatsCollector.java` with direct Win32 / /proc calls (D7).
// The JVM-heap fields (`heapUsedBytes` / `heapMaxBytes`) are dropped —
// N/A for a Qt application (no JVM, no MemoryMXBean equivalent).
//
// Platform split:
//   Windows:
//     CPU% — GetProcessTimes (kernel+user FILETIME, 100ns units) diffed
//            against the previous sample, divided by the wall-clock delta
//            and the logical core count (0..100 scale where 100 = all cores).
//            First sample after construction returns cpuPercent=0.0 because
//            there is no baseline to diff against.
//     RSS  — GetProcessMemoryInfo → PROCESS_MEMORY_COUNTERS::WorkingSetSize
//            (bytes). Requires linking `psapi` on Windows.
//   Linux:
//     CPU% — /proc/self/stat fields 14 (utime) + 15 (stime) in clock ticks,
//            converted to ms via sysconf(_SC_CLK_TCK), diffed against the
//            previous sample, divided by wall-clock delta and core count.
//     RSS  — /proc/self/status "VmRSS:" line value in kB → bytes (* 1024).
//   Other: cpuPercent + rssBytes stay 0; timestampMs still set.
//
// **M5 fix (CRITICAL)**: the renderer's `monitor/ProcessStatsCollector.cpp`
// Linux half is a STUB that returns zeros + logs "Linux stub sample
// (unimplemented)". Do NOT copy it. The /proc parsing here is implemented
// from scratch per architecture-blueprint.md §4.8.1.
//
// **Never-throws contract** (§4.8.1 "不抛异常原则"): every platform call is
// wrapped. On ANY failure (GetProcessTimes fails, /proc unreadable, parse
// error, unsupported platform) the affected fields are zeroed BUT
// `timestampMs` is always set. A WARN is logged on the FIRST failure only
// (subsequent failures are silent — the resource-monitor page renders "—").
//
// **Threading**: NOT thread-safe. Owned exclusively by the caller that drives
// collect() — todo 18's MonitorPage poller (a 2s QTimer on the Qt main
// thread). The previous-sample baseline (`m_prevProcMs` / `m_prevWallMs`) is
// mutable state that must not be raced.
//
// **Testability** via std::function injection (same pattern as
// AutoLaunchManager's suppliers):
//   - clockMs:      epoch-millisecond clock. Default:
//                   QDateTime::currentMSecsSinceEpoch().
//   - processHandle (Windows): the HANDLE passed to GetProcessTimes /
//                   GetProcessMemoryInfo. Default: GetCurrentProcess().
//   - statPath / statusPath (Linux): the /proc paths read. Default:
//                   /proc/self/stat + /proc/self/status. Tests inject
//                   non-existent paths to exercise the never-throws path.

// Resource-usage snapshot of THIS controller process. The Qt port of Java's
// `model/ControllerStats.java` record — drops the JVM-heap fields (N/A).
//
// All fields are point-in-time samples captured at `timestampMs`; no
// historical aggregation is performed at this layer.
struct ControllerStats
{
    // Process CPU load since the PREVIOUS sample, 0..100 (100 = all logical
    // cores fully busy). 0.0 on the first sample (no baseline) or on failure.
    double cpuPercent = 0.0;

    // Resident set size of this process in BYTES. 0 on failure.
    qint64 rssBytes = 0;

    // Epoch milliseconds at which the sample was captured. ALWAYS set, even
    // when cpuPercent + rssBytes are zeroed on failure.
    qint64 timestampMs = 0;
};

class ResourceStatsCollector
{
public:
    // Default clock supplier — exposed so test ctors can reference it in
    // default arguments without re-typing the lambda. Returns
    // QDateTime::currentMSecsSinceEpoch(). Declared BEFORE the test ctors so
    // their default arguments can name it (C++ requires name lookup at the
    // point of default-argument parsing, not at call time).
    static std::function<qint64()> defaultClock();

    // Production constructor — wires the real platform defaults:
    //   Windows: process handle = GetCurrentProcess()
    //   Linux:   stat path = /proc/self/stat, status path = /proc/self/status
    //   clock = QDateTime::currentMSecsSinceEpoch()
    // Zero-config; todo 18 (MonitorPage) constructs this directly.
    ResourceStatsCollector();

    // ---- Test-injectable constructors ----
#if defined(Q_OS_WIN)
    // Windows test ctor. Inject a process handle (use NULL to exercise the
    // never-throws path — GetProcessTimes/GetProcessMemoryInfo fail) and/or
    // a fake clock.
    explicit ResourceStatsCollector(void* processHandle,
                                    std::function<qint64()> clockMs = defaultClock());
#else
    // Linux test ctor. Inject /proc paths (use non-existent paths to
    // exercise the never-throws path) and/or a fake clock.
    explicit ResourceStatsCollector(QString statPath,
                                    QString statusPath,
                                    std::function<qint64()> clockMs = defaultClock());
#endif

    // Sample this process's CPU% + RSS. Updates the internal baseline so the
    // NEXT call can diff against this one. The first call returns
    // cpuPercent=0.0 (no baseline). Never throws — on any failure the
    // affected fields are zeroed but timestampMs is always populated.
    ControllerStats collect();

private:
    // Platform-dependent state. Only the branch matching the compile-time
    // platform is initialized; the other branch's members stay default.
#if defined(Q_OS_WIN)
    void*  m_processHandle  = nullptr;
    unsigned m_processorCount = 1u;   // CPU% scaling denominator
#else
    QString m_statPath;               // /proc/self/stat (or test override)
    QString m_statusPath;             // /proc/self/status (or test override)
    qint64 m_clockTicksPerSec = 100;  // sysconf(_SC_CLK_TCK); 100 on most Linux
    qint64 m_processorCount   = 1;    // sysconf(_SC_NPROCESSORS_ONLN)
#endif

    std::function<qint64()> m_clockMs;

    // Two-sample-diff baseline. m_hasBaseline=false on the first collect();
    // set true after the first successful sample is stored.
    qint64 m_prevProcMs  = 0;
    qint64 m_prevWallMs  = 0;
    bool   m_hasBaseline = false;

    // First-failure-only WARN gate. Prevents log spam when /proc is
    // permanently unreadable or the handle is bad.
    bool   m_warned      = false;

    // ---- Platform dispatch (defined in the .cpp) ----
#if defined(Q_OS_WIN)
    void collectWindows(ControllerStats& out);
#else
    void collectLinux(ControllerStats& out);
    // Parse /proc/self/stat → sum of utime+stime ticks (fields 14+15).
    // Returns false on open/parse failure (never throws).
    bool readProcStatTicks(qint64& outTicks);
    // Parse /proc/self/status "VmRSS:" line → kB value.
    // Returns false on open/parse/missing-line failure (never throws).
    bool readVmRssKb(qint64& outKb);
#endif

    // First-occurrence WARN logger for sampling failures. Idempotent —
    // second+ calls are silent.
    void logFailureOnce(const char* what);
};
