#include "system/ResourceStatsCollector.hpp"

// spdlog MUST be included before logging/Logging.hpp — Logging.hpp references
// the SPDLOG_* macros but does NOT include spdlog headers itself (T5 finding
// in .omo/notepads/qt-controller-foundation/learnings.md).
#include <spdlog/spdlog.h>
#include "logging/Logging.hpp"

#include <QDateTime>
#include <QFile>
#include <QTextStream>

#if defined(Q_OS_WIN)
#  include <windows.h>
#  include <psapi.h>
#endif

#if defined(Q_OS_LINUX)
#  include <unistd.h>  // sysconf, _SC_CLK_TCK, _SC_NPROCESSORS_ONLN
#endif

namespace {

#if defined(Q_OS_WIN)
// Convert a Win32 FILETIME (kernel/user process time, 100ns units since
// 1601-01-01 UTC) to milliseconds. We only ever DIFF two of these, so the
// absolute epoch offset cancels and we can treat the result as "process
// milliseconds of CPU consumed". 1 ms = 10000 × 100ns.
inline qint64 filetimeToMs(const FILETIME& ft)
{
    ULARGE_INTEGER u;
    u.LowPart  = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return static_cast<qint64>(u.QuadPart / 10000ULL);
}
#endif

} // namespace

std::function<qint64()> ResourceStatsCollector::defaultClock()
{
    return []() { return QDateTime::currentMSecsSinceEpoch(); };
}

ResourceStatsCollector::ResourceStatsCollector()
    : m_clockMs(defaultClock())
{
#if defined(Q_OS_WIN)
    m_processHandle = GetCurrentProcess();
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    m_processorCount = si.dwNumberOfProcessors > 0 ? si.dwNumberOfProcessors : 1u;
#else
    m_statPath   = QStringLiteral("/proc/self/stat");
    m_statusPath = QStringLiteral("/proc/self/status");
    const long ticks = sysconf(_SC_CLK_TCK);
    m_clockTicksPerSec = ticks > 0 ? static_cast<qint64>(ticks) : 100;
    const long cores = sysconf(_SC_NPROCESSORS_ONLN);
    m_processorCount = cores > 0 ? static_cast<qint64>(cores) : 1;
#endif
}

#if defined(Q_OS_WIN)
ResourceStatsCollector::ResourceStatsCollector(void* processHandle,
                                               std::function<qint64()> clockMs)
    : m_processHandle(processHandle)
    , m_clockMs(std::move(clockMs))
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    m_processorCount = si.dwNumberOfProcessors > 0 ? si.dwNumberOfProcessors : 1u;
}
#else
ResourceStatsCollector::ResourceStatsCollector(QString statPath,
                                               QString statusPath,
                                               std::function<qint64()> clockMs)
    : m_statPath(std::move(statPath))
    , m_statusPath(std::move(statusPath))
    , m_clockMs(std::move(clockMs))
{
    const long ticks = sysconf(_SC_CLK_TCK);
    m_clockTicksPerSec = ticks > 0 ? static_cast<qint64>(ticks) : 100;
    const long cores = sysconf(_SC_NPROCESSORS_ONLN);
    m_processorCount = cores > 0 ? static_cast<qint64>(cores) : 1;
}
#endif

ControllerStats ResourceStatsCollector::collect()
{
    ControllerStats out;
    out.timestampMs = m_clockMs();

#if defined(Q_OS_WIN)
    collectWindows(out);
#elif defined(Q_OS_LINUX)
    collectLinux(out);
#else
    logFailureOnce("collect(): unsupported platform");
#endif

    return out;
}

// ---------------------------------------------------------------------------
// cpuPercent = (delta_proc_ms / delta_wall_ms) * 100 / num_cores
// 100 = all cores fully saturated by this process. Matches the renderer's
// ProcessStatsCollector.cpp Windows branch (the only usable reference).
// ---------------------------------------------------------------------------

#if defined(Q_OS_WIN)
void ResourceStatsCollector::collectWindows(ControllerStats& out)
{
    // RSS — WorkingSetSize is in bytes (no conversion needed).
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(m_processHandle, &pmc, sizeof(pmc))) {
        out.rssBytes = static_cast<qint64>(pmc.WorkingSetSize);
    } else {
        logFailureOnce("GetProcessMemoryInfo");
    }

    // CPU time (kernel + user, 100ns FILETIME units → ms).
    FILETIME ftCreate, ftExit, ftKernel, ftUser;
    if (!GetProcessTimes(m_processHandle, &ftCreate, &ftExit, &ftKernel, &ftUser)) {
        logFailureOnce("GetProcessTimes");
        return;
    }
    const qint64 procMs = filetimeToMs(ftKernel) + filetimeToMs(ftUser);

    if (m_hasBaseline && out.timestampMs > m_prevWallMs) {
        const double elapsedMs  = static_cast<double>(out.timestampMs - m_prevWallMs);
        const qint64   procDelta = procMs > m_prevProcMs ? (procMs - m_prevProcMs) : 0;
        out.cpuPercent = (static_cast<double>(procDelta) / elapsedMs) * 100.0
                         / static_cast<double>(m_processorCount);
        if (out.cpuPercent < 0.0) out.cpuPercent = 0.0;
    }
    m_prevProcMs  = procMs;
    m_prevWallMs  = out.timestampMs;
    m_hasBaseline = true;
}
#endif

#if defined(Q_OS_LINUX)
void ResourceStatsCollector::collectLinux(ControllerStats& out)
{
    // CPU — /proc/self/stat fields 14 (utime) + 15 (stime), clock ticks.
    qint64 procTicks = 0;
    if (readProcStatTicks(procTicks)) {
        // ticks → ms: (ticks * 1000) / ticksPerSec
        const qint64 procMs = procTicks * 1000 / m_clockTicksPerSec;
        if (m_hasBaseline && out.timestampMs > m_prevWallMs) {
            const double elapsedMs  = static_cast<double>(out.timestampMs - m_prevWallMs);
            const qint64   procDelta = procMs > m_prevProcMs ? (procMs - m_prevProcMs) : 0;
            out.cpuPercent = (static_cast<double>(procDelta) / elapsedMs) * 100.0
                             / static_cast<double>(m_processorCount);
            if (out.cpuPercent < 0.0) out.cpuPercent = 0.0;
        }
        m_prevProcMs  = procMs;
        m_prevWallMs  = out.timestampMs;
        m_hasBaseline = true;
    } else {
        logFailureOnce("/proc/self/stat read or parse");
    }

    // RSS — /proc/self/status VmRSS line, value in kB → bytes.
    qint64 rssKb = 0;
    if (readVmRssKb(rssKb)) {
        out.rssBytes = rssKb * 1024;
    } else {
        logFailureOnce("/proc/self/status VmRSS parse");
    }
}

// /proc/self/stat format (see proc(5)): "<pid> (<comm>) <state> <utime>
// <stime> ...". The comm field can contain spaces AND parens, so we cannot
// naively tokenize from the start — we find the LAST ')' and tokenize the
// tail. Field 3 = state starts the tail; utime is field 14 (tail index 11),
// stime is field 15 (tail index 12).
bool ResourceStatsCollector::readProcStatTicks(qint64& outTicks)
{
    QFile f(m_statPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    const QByteArray line = f.readAll();
    f.close();

    const int closeParen = line.lastIndexOf(')');
    if (closeParen < 0) {
        return false;
    }
    const QByteArray tail = line.mid(closeParen + 2);

    qint64 utime = 0, stime = 0;
    int    fieldIdx = 0;
    bool   gotUtime = false, gotStime = false;
    const char*       it  = tail.constData();
    const char* const end = it + tail.size();
    while (it != end) {
        while (it != end && (*it == ' ' || *it == '\t' ||
                             *it == '\n' || *it == '\r')) {
            ++it;
        }
        if (it == end) break;
        const char* tokStart = it;
        while (it != end && *it != ' ' && *it != '\t' &&
               *it != '\n' && *it != '\r') {
            ++it;
        }
        if (fieldIdx == 11) {        // utime (field 14 overall)
            bool ok = false;
            utime = QByteArray::fromRawData(tokStart,
                                            static_cast<int>(it - tokStart))
                        .toLongLong(&ok);
            if (!ok) return false;
            gotUtime = true;
        } else if (fieldIdx == 12) { // stime (field 15 overall)
            bool ok = false;
            stime = QByteArray::fromRawData(tokStart,
                                            static_cast<int>(it - tokStart))
                        .toLongLong(&ok);
            if (!ok) return false;
            gotStime = true;
            break;                   // no need to scan further
        }
        ++fieldIdx;
    }

    if (!gotUtime || !gotStime) {
        return false;
    }
    outTicks = utime + stime;
    return true;
}

// /proc/self/status line: "VmRSS:     12345 kB" (value is in kB per proc(5)).
// Case-sensitive prefix match — VmRSS is always exactly that spelling.
bool ResourceStatsCollector::readVmRssKb(qint64& outKb)
{
    QFile f(m_statusPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    QTextStream ts(&f);
    QString line;
    const QString prefix = QStringLiteral("VmRSS:");
    while (ts.readLineInto(&line)) {
        if (!line.startsWith(prefix, Qt::CaseSensitive)) {
            continue;
        }
        // "VmRSS:     12345 kB" → strip prefix, trim → "12345 kB"
        const QString rest = line.mid(prefix.length()).trimmed();
        qsizetype n = 0;
        while (n < rest.size() && rest.at(n).isDigit()) {
            ++n;
        }
        if (n == 0) {
            f.close();
            return false;
        }
        bool ok = false;
        outKb = rest.first(n).toLongLong(&ok);
        f.close();
        return ok;
    }
    f.close();
    return false;  // no VmRSS line found
}
#endif

void ResourceStatsCollector::logFailureOnce(const char* what)
{
    if (m_warned) {
        return;
    }
    m_warned = true;
    LOG_WARN("ResourceStatsCollector: {} failed; fields zeroed, "
             "timestampMs set (further failures silenced)", what);
}
