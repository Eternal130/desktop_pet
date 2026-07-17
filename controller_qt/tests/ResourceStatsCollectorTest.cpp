// ResourceStatsCollectorTest — Wave 8 todo 17.
//
// Locks the blueprint §4.8.1 self-collection contract for the Qt controller:
//   - cpuPercent / rssBytes / timestampMs populated from THIS process via
//     platform APIs (Win32 GetProcessTimes + GetProcessMemoryInfo on Windows;
//     /proc/self/stat + /proc/self/status on Linux — implemented FROM
//     SCRATCH, NOT copied from the renderer's stub).
//   - First sample returns cpuPercent=0.0 (no baseline).
//   - Two-sample diff produces a finite, non-negative cpuPercent.
//   - rssBytes > 0 on a real running process (the test allocates memory).
//     On Linux this doubles as the M5 guard — proves /proc parsing works
//     rather than the renderer's stub-zero path.
//   - Never-throws contract: injecting a bad platform resource (NULL handle
//     on Windows / non-existent /proc path on Linux) zeroes the affected
//     fields but ALWAYS sets timestampMs and does NOT throw.
//
// QTEST_MAIN (NOT APPLESS): QTest::qWait (used to separate the two samples
// by a real wall-clock gap so the CPU% diff has a non-zero denominator)
// requires the QCoreApplication event loop. Same pattern as SchedulerTest /
// PendingRequestsTest.
//
// The test recompiles ResourceStatsCollector.cpp directly (same pattern as
// the other system/ tests — no shared lib yet). Links Qt6::Core +
// Qt6::Test + spdlog (ResourceStatsCollector.cpp includes logging/Logging.hpp
// which references the SPDLOG_* macros). On Windows, `psapi` is also linked
// (GetProcessMemoryInfo lives in Psapi.lib).

#include "system/ResourceStatsCollector.hpp"

#include <QTest>
#include <limits>

class ResourceStatsCollectorTest : public QObject
{
    Q_OBJECT

private slots:
    void testCollectReturnsValidTimestamp();
    void testFirstSampleCpuIsZero();
    void testCpuPercentAfterTwoSamples();
    void testRssBytesPositive();
    void testFailureNeverThrows();
};

// ===========================================================================
// collect() always populates timestampMs, even on the very first call.
// A monotonic-clock guarantee means a freshly-constructed collector's first
// sample has timestampMs > 0 (post-1970).
// ===========================================================================
void ResourceStatsCollectorTest::testCollectReturnsValidTimestamp()
{
    ResourceStatsCollector c;
    const ControllerStats s = c.collect();

    QVERIFY2(s.timestampMs > 0,
             "timestampMs must always be set (>0), even on the first call");
}

// ===========================================================================
// First sample after construction has cpuPercent == 0.0 because there is no
// previous baseline to diff against (§4.8.1 "注意 CPU% 需要两次采样差值计算
// （首次返回 0）").
// ===========================================================================
void ResourceStatsCollectorTest::testFirstSampleCpuIsZero()
{
    ResourceStatsCollector c;
    const ControllerStats s = c.collect();

    QCOMPARE(s.cpuPercent, 0.0);
}

// ===========================================================================
// Two samples separated by a real wall-clock gap produce a finite, non-
// negative cpuPercent. The test process IS running (Qt event loop during
// QTest::qWait), so the kernel/user time delta is well-defined and >0 in
// the denominator. We do NOT assert an exact CPU usage value — it depends
// on scheduler timing and is inherently flaky. We assert finiteness + the
// [0, 100] contract bound.
// ===========================================================================
void ResourceStatsCollectorTest::testCpuPercentAfterTwoSamples()
{
    ResourceStatsCollector c;
    c.collect();                       // establish baseline (returns 0.0)
    QTest::qWait(100);                 // ~100ms wall gap for the diff

    const ControllerStats s = c.collect();
    QVERIFY2(std::isfinite(s.cpuPercent),
             "cpuPercent must be finite (not NaN/Inf) after two samples");
    QVERIFY2(s.cpuPercent >= 0.0,
             "cpuPercent must be non-negative");
    QVERIFY2(s.cpuPercent <= 100.0,
             "cpuPercent must not exceed 100 (all-cores-saturated scale)");
}

// ===========================================================================
// rssBytes > 0 on a real running process. The test process has the Qt
// runtime + QTest + this binary mapped in — resident set is non-zero.
//
// On Linux this is the M5 guard: it proves the /proc/self/status VmRSS
// parser works end-to-end (the renderer's ProcessStatsCollector Linux half
// returns zeros from its stub, so if we accidentally copied that path this
// assertion would fail).
// ===========================================================================
void ResourceStatsCollectorTest::testRssBytesPositive()
{
    ResourceStatsCollector c;
    const ControllerStats s = c.collect();

    QVERIFY2(s.rssBytes > 0,
             "rssBytes must be > 0 for a real running process");
}

// ===========================================================================
// Never-throws contract (§4.8.1 "不抛异常原则"). Inject a platform resource
// that cannot be sampled:
//   Windows: NULL process handle → GetProcessTimes + GetProcessMemoryInfo
//            both fail.
//   Linux:   non-existent /proc paths → QFile::open fails for both files.
// Fields are zeroed (cpuPercent + rssBytes), timestampMs is still set, and
// collect() returns normally (no exception escapes — verified by the slot
// reaching its assertions).
// ===========================================================================
void ResourceStatsCollectorTest::testFailureNeverThrows()
{
#if defined(Q_OS_WIN)
    ResourceStatsCollector c(nullptr);   // NULL handle → all calls fail
#else
    ResourceStatsCollector c(QStringLiteral("/nonexistent/stat"),
                             QStringLiteral("/nonexistent/status"));
#endif
    const ControllerStats s = c.collect();

    // The slot reaching here proves no throw. Lock the field contract too.
    QCOMPARE(s.cpuPercent, 0.0);
    QCOMPARE(s.rssBytes, qint64(0));
    QVERIFY2(s.timestampMs > 0,
             "timestampMs must be set even when sampling fails");
}

QTEST_MAIN(ResourceStatsCollectorTest)
#include "ResourceStatsCollectorTest.moc"
