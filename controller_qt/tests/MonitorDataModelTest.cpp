#include <QJsonObject>
#include <QTest>
#include <QSignalSpy>

#include "ui/MonitorDataModel.hpp"

// MonitorDataModelTest (Phase 5 Wave 8 todo 18) — locks the data-model
// contract that MonitorPage.qml + InstanceSession's 2s poller rely on.
//
// Five slots cover the data-model invariants:
//   - testRingBufferCap: 65 controller merges → history.size()==60 (FIFO drop)
//   - testIsStalePredicate: static isStale() boundary (never/just-fresh/stale)
//   - testMergeRenderer: stats_state payload → renderer history grows; null
//     GPU/VRAM fields preserved through the round-trip
//   - testMergePreservesOtherSide: mergeController then mergeRenderer keeps
//     BOTH halves populated in the latest snapshot (the carry-forward rule)
//   - testClearHistory: clears state, lastRendererUpdateMs → 0
//
// Recompiles MonitorDataModel.cpp directly (same pattern as the other tests
// — no shared lib yet). Links Qt6::Core + Qt6::Test + spdlog (the .cpp
// includes logging/Logging.hpp transitively via ResourceStatsCollector.hpp;
// spdlog is statically linked). QTEST_APPLESS_MAIN — QDateTime + QJsonObject
// are synchronous, no event loop needed.

class MonitorDataModelTest : public QObject
{
    Q_OBJECT

private slots:
    void testRingBufferCap();
    void testIsStalePredicate();
    void testMergeRenderer();
    void testMergePreservesOtherSide();
    void testClearHistory();
    void testRendererStatsFromJson();
};

void MonitorDataModelTest::testRingBufferCap()
{
    MonitorDataModel m;
    QCOMPARE(m.history().size(), 0);

    ControllerStats cs{};
    cs.cpuPercent = 12.5;
    cs.rssBytes   = 100 * 1024 * 1024;  // 100 MB
    cs.timestampMs = 1719990000000LL;

    // Push 65 samples — the cap is 60, so the oldest 5 should be dropped.
    for (int i = 0; i < 65; ++i) {
        cs.cpuPercent = i;
        m.mergeController(cs);
    }

    QCOMPARE(m.history().size(), MonitorDataModel::HISTORY_CAP);
    QCOMPARE(m.history().size(), 60);

    // First surviving sample should be index 5 (the 6th push), not 0.
    const auto h = m.history();
    QVERIFY(h.first().controller.has_value());
    QCOMPARE(h.first().controller->cpuPercent, 5.0);

    // Latest sample is the most-recent push.
    QVERIFY(h.last().controller.has_value());
    QCOMPARE(h.last().controller->cpuPercent, 64.0);

    // Chart series getter returns the same length (controller side populated
    // on every sample).
    QCOMPARE(m.controllerCpuSeries().size(), 60);
}

void MonitorDataModelTest::testIsStalePredicate()
{
    // Never-updated → stale regardless of threshold (lastUpdate <= 0 path).
    QVERIFY(MonitorDataModel::isStale(1000, 0));
    QVERIFY(MonitorDataModel::isStale(1000, -1));
    QVERIFY(MonitorDataModel::isStale(1000, 0, 5000));
    QVERIFY(MonitorDataModel::isStale(1000, 0, 0));  // lastUpdate=0 wins

    // Exactly at threshold → NOT stale (strict >).
    QVERIFY(!MonitorDataModel::isStale(11000, 1000, 10000));

    // Positive lastUpdate + 0 threshold + nonzero gap → stale (gap > 0).
    QVERIFY(MonitorDataModel::isStale(1001, 1000, 0));

    // 1ms over threshold → stale.
    QVERIFY(MonitorDataModel::isStale(11001, 1000, 10000));

    // Way over → stale.
    QVERIFY(MonitorDataModel::isStale(999999, 1000, 10000));

    // Negative threshold treated as 0 — gap strictly > 0 → stale.
    QVERIFY(MonitorDataModel::isStale(1001, 1000, -5));
}

void MonitorDataModelTest::testMergeRenderer()
{
    MonitorDataModel m;

    // Build a stats_state payload matching interface.md §K (Linux stub form:
    // gpu_percent / vram_* are JSON null).
    QJsonObject linuxStub;
    linuxStub.insert("cpu_percent", 8.2);
    linuxStub.insert("rss_bytes", 72351744.0);
    linuxStub.insert("gpu_percent", QJsonValue::Null);
    linuxStub.insert("gpu_name",    QJsonValue::Null);
    linuxStub.insert("vram_used_bytes",  QJsonValue::Null);
    linuxStub.insert("vram_total_bytes", QJsonValue::Null);
    linuxStub.insert("timestamp_ms", 1719990000000.0);

    const RendererStats rs = RendererStats::fromJson(linuxStub);
    QCOMPARE(rs.cpuPercent, 8.2);
    QCOMPARE(rs.rssBytes,   72351744LL);
    QVERIFY(!rs.gpuPercent.has_value());    // null preserved
    QVERIFY(rs.gpuName.isEmpty());
    QVERIFY(!rs.vramUsedBytes.has_value());
    QVERIFY(!rs.vramTotalBytes.has_value());
    QCOMPARE(rs.timestampMs, 1719990000000LL);

    m.mergeRenderer(rs);
    QCOMPARE(m.history().size(), 1);
    QVERIFY(m.history().first().renderer.has_value());

    // Q_INVOKABLE getters: GPU/VRAM null → -1.0 sentinel.
    QCOMPARE(m.latestRendererCpuPercent(), 8.2);
    QCOMPARE(m.latestRendererGpuPercent(), -1.0);
    QCOMPARE(m.latestRendererVramUsedBytes(), -1.0);
    QCOMPARE(m.latestRendererVramTotalBytes(), -1.0);

    // Empty GPU/VRAM series (null samples skipped).
    QVERIFY(m.rendererGpuSeries().isEmpty());
    QVERIFY(m.rendererVramSeries().isEmpty());
    QCOMPARE(m.rendererCpuSeries().size(), 1);

    // lastRendererUpdateMs must now be > 0 (set by mergeRenderer).
    QVERIFY(m.lastRendererUpdateMs() > 0);
}

void MonitorDataModelTest::testMergePreservesOtherSide()
{
    MonitorDataModel m;

    ControllerStats cs{};
    cs.cpuPercent = 42.0;
    cs.rssBytes   = 200 * 1024 * 1024;
    cs.timestampMs = 1719990000001LL;
    m.mergeController(cs);

    // After controller-only merge: latest has controller, no renderer.
    auto snap1 = m.latestSnapshot();
    QVERIFY(snap1.has_value());
    QVERIFY(snap1->controller.has_value());
    QVERIFY(!snap1->renderer.has_value());

    RendererStats rs{};
    rs.cpuPercent = 7.5;
    rs.rssBytes   = 80 * 1024 * 1024;
    rs.timestampMs = 1719990000002LL;
    m.mergeRenderer(rs);

    // After renderer merge: latest has BOTH halves (carry-forward rule).
    auto snap2 = m.latestSnapshot();
    QVERIFY(snap2.has_value());
    QVERIFY(snap2->controller.has_value());
    QCOMPARE(snap2->controller->cpuPercent, 42.0);  // preserved
    QVERIFY(snap2->renderer.has_value());
    QCOMPARE(snap2->renderer->cpuPercent, 7.5);     // new

    // Symmetric: another controller merge preserves the renderer sample.
    cs.cpuPercent = 50.0;
    m.mergeController(cs);
    auto snap3 = m.latestSnapshot();
    QVERIFY(snap3->renderer.has_value());
    QCOMPARE(snap3->renderer->cpuPercent, 7.5);     // preserved
    QCOMPARE(snap3->controller->cpuPercent, 50.0);  // new

    // Chart series: 3 controller samples (every merge has a controller half —
    // mergeRenderer carries the prior controller forward into its snapshot),
    // 2 renderer samples (the controller-only merge at the top has no renderer
    // half; the renderer merge + the follow-up controller merge both carry
    // renderer forward into their snapshots).
    QCOMPARE(m.controllerCpuSeries().size(), 3);
    QCOMPARE(m.rendererCpuSeries().size(), 2);
}

void MonitorDataModelTest::testClearHistory()
{
    MonitorDataModel m;

    ControllerStats cs{};
    m.mergeController(cs);
    RendererStats rs{};
    m.mergeRenderer(rs);
    QVERIFY(m.history().size() >= 2);
    QVERIFY(m.lastRendererUpdateMs() > 0);

    QSignalSpy clearedSpy(&m, &MonitorDataModel::historyCleared);
    m.clearHistory();
    QCOMPARE(clearedSpy.count(), 1);

    QCOMPARE(m.history().size(), 0);
    QVERIFY(!m.latestSnapshot().has_value());
    QCOMPARE(m.lastRendererUpdateMs(), 0);

    // All chart series empty after clear.
    QVERIFY(m.controllerCpuSeries().isEmpty());
    QVERIFY(m.rendererCpuSeries().isEmpty());
}

void MonitorDataModelTest::testRendererStatsFromJson()
{
    // Windows full payload (interface.md §K example).
    QJsonObject full;
    full.insert("cpu_percent", 12.5);
    full.insert("rss_bytes",   89128960.0);
    full.insert("gpu_percent", 35.0);
    full.insert("gpu_name",    QStringLiteral("NVIDIA GeForce RTX 3060"));
    full.insert("vram_used_bytes",  104857600.0);
    full.insert("vram_total_bytes", 8589934592.0);
    full.insert("timestamp_ms", 1719990000000.0);

    const RendererStats r = RendererStats::fromJson(full);
    QCOMPARE(r.cpuPercent, 12.5);
    QCOMPARE(r.rssBytes,   89128960LL);
    QVERIFY(r.gpuPercent.has_value());
    QCOMPARE(*r.gpuPercent, 35.0);
    QCOMPARE(r.gpuName, QStringLiteral("NVIDIA GeForce RTX 3060"));
    QVERIFY(r.vramUsedBytes.has_value());
    QCOMPARE(*r.vramUsedBytes, 104857600LL);
    QVERIFY(r.vramTotalBytes.has_value());
    QCOMPARE(*r.vramTotalBytes, 8589934592LL);
    QCOMPARE(r.timestampMs, 1719990000000LL);

    // Missing keys → defaults (no throw).
    QJsonObject empty;
    const RendererStats r2 = RendererStats::fromJson(empty);
    QCOMPARE(r2.cpuPercent, 0.0);
    QCOMPARE(r2.rssBytes, 0);
    QVERIFY(!r2.gpuPercent.has_value());
    QVERIFY(!r2.vramUsedBytes.has_value());

    // Partial Windows payload (GPU present, VRAM total missing).
    QJsonObject partial;
    partial.insert("cpu_percent", 5.0);
    partial.insert("rss_bytes", 1000.0);
    partial.insert("gpu_percent", 50.0);
    // gpu_name + vram_* omitted.
    const RendererStats r3 = RendererStats::fromJson(partial);
    QCOMPARE(r3.cpuPercent, 5.0);
    QVERIFY(r3.gpuPercent.has_value());
    QCOMPARE(*r3.gpuPercent, 50.0);
    QVERIFY(r3.gpuName.isEmpty());
    QVERIFY(!r3.vramUsedBytes.has_value());
    QVERIFY(!r3.vramTotalBytes.has_value());
}

QTEST_APPLESS_MAIN(MonitorDataModelTest)
#include "MonitorDataModelTest.moc"
