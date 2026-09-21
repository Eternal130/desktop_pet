// MonitorApiImplTest (S7, v1.4) — the real pet::IMonitorApi implementation
// over a REAL InstanceManager harness (same QTemporaryDir + WsServer +
// PendingRequests + savePanel-lambda pattern as TuningApiImplTest).
// Samples are fed through the session's own MonitorDataModel merges (the
// production arrival path — the impl rides snapshotAppended). Locks:
//   1. testHistoryProjectionAndSentinels: history() returns the ring
//      oldest→newest as flat MonitorSample PODs; controller-only halves
//      project 0/-1; Linux-stub renderer halves (nullopt GPU/VRAM)
//      project the -1 sentinel; populated GPU/VRAM project their values;
//      unknown uuid / null manager → empty (never an error).
//   2. testSubscribePushesIncrementally: subscribe(0ms) → every merge
//      pushes exactly one projected sample carrying the LATEST
//      snapshot (carry-forward halves included).
//   3. testThrottleSkipsSamples: minIntervalMs=60000 → the first sample
//      pushes (lastPushMs starts at 0), the following samples inside the
//      window are SKIPPED (no queue: count stays 1); minIntervalMs=0 →
//      every sample pushes.
//   4. testUnsubscribeStopsPush: unsubscribe stops the stream; unknown
//      (uuid, observer) pairs unsubscribe as no-ops.
//   5. testSubscribeUnknownUuidNotFound: unknown uuid → NotFound; null
//      manager (degraded wiring) → NotFound; null observer →
//      InvalidArgument.
//   6. testMultiSubscriberIndependentThrottle: two observers with
//      different intervals are throttled INDEPENDENTLY; re-subscribing
//      refreshes the interval and resets the window.
//   7. testObserverUnsubscribingInCallback: an observer that
//      unsubscribes inside sample() does not break the fanout.
//
// QTEST_MAIN — parity with the other InstanceManager-harness tests (the
// WsServer machinery is constructed). Links pet_panel_core
// (MonitorApiImpl lives there).

#include "core/InstanceManager.hpp"
#include "core/InstanceSession.hpp"
#include "core/PanelConfig.hpp"
#include "core/PanelStateManager.hpp"
#include "core/PluginContextImpl.hpp"
#include "network/PendingRequests.hpp"
#include "network/WsServer.hpp"
#include "ui/MonitorDataModel.hpp"

#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include <QVector>

namespace {

// Recording observer: counts pushes + keeps the payloads (a plain
// SDK observer — the host never requires QObject).
class CountingObserver : public pet::IMonitorObserver
{
public:
    int count = 0;
    QString lastUuid;
    QVector<pet::MonitorSample> samples;

    void sample(const QString& uuid, const pet::MonitorSample& s) override
    {
        ++count;
        lastUuid = uuid;
        samples.append(s);
    }
};

ControllerStats controllerSample(double cpu, qint64 rss)
{
    ControllerStats cs;
    cs.cpuPercent = cpu;
    cs.rssBytes = rss;
    cs.timestampMs = 12345;
    return cs;
}

RendererStats rendererSample(double cpu, qint64 rss)
{
    RendererStats rs; // Linux-stub shape: GPU/VRAM stay nullopt
    rs.cpuPercent = cpu;
    rs.rssBytes = rss;
    rs.timestampMs = 54321;
    return rs;
}

} // namespace

class MonitorApiImplTest : public QObject
{
    Q_OBJECT

private slots:
    void testHistoryProjectionAndSentinels();
    void testSubscribePushesIncrementally();
    void testThrottleSkipsSamples();
    void testUnsubscribeStopsPush();
    void testSubscribeUnknownUuidNotFound();
    void testMultiSubscriberIndependentThrottle();
    void testObserverUnsubscribingInCallback();
};

// ── 1. history projection ───────────────────────────────────────────────────

void MonitorApiImplTest::testHistoryProjectionAndSentinels()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    WsServer server;
    PendingRequests pending;
    InstanceManager mgr(base.path(), server, pending,
                        [&base](const PanelConfig& cfg) {
                            PanelStateManager(base.path()).save(cfg);
                        });
    core::MonitorApiImpl api(&mgr, nullptr);

    // Unknown uuid / empty roster → empty history, never an error.
    QCOMPARE(api.history(QStringLiteral("nope")).size(), 0);

    const QString uuid = mgr.createInstance(QStringLiteral("Mon"));
    QVERIFY(!uuid.isEmpty());
    InstanceSession* session = mgr.instanceAt(0);
    QVERIFY(session != nullptr);
    auto* model = static_cast<MonitorDataModel*>(session->monitorModel());
    QVERIFY(model != nullptr);

    // Controller-only sample: renderer half unreported → renderer cpu/rss
    // at the 0 default, GPU/VRAM at the -1 sentinel.
    model->mergeController(controllerSample(3.5, 1000));
    {
        const QVector<pet::MonitorSample> h = api.history(uuid);
        QCOMPARE(h.size(), 1);
        QCOMPARE(h.at(0).controllerCpu, 3.5);
        QCOMPARE(h.at(0).controllerRssBytes, 1000.0);
        QCOMPARE(h.at(0).rendererCpu, 0.0);
        QCOMPARE(h.at(0).rendererRssBytes, 0.0);
        QCOMPARE(h.at(0).rendererGpu, -1.0);
        QCOMPARE(h.at(0).rendererVramUsedBytes, -1.0);
        QCOMPARE(h.at(0).rendererVramTotalBytes, -1.0);
        QVERIFY(h.at(0).capturedAtMs > 0);
    }

    // Renderer sample (Linux stub: nullopt GPU/VRAM) — carry-forward keeps
    // the controller half; the GPU/VRAM sentinels stay -1.
    model->mergeRenderer(rendererSample(12.5, 2000));
    {
        const QVector<pet::MonitorSample> h = api.history(uuid);
        QCOMPARE(h.size(), 2);
        const pet::MonitorSample& latest = h.at(1);
        QCOMPARE(latest.controllerCpu, 3.5); // carried forward
        QCOMPARE(latest.rendererCpu, 12.5);
        QCOMPARE(latest.rendererRssBytes, 2000.0);
        QCOMPARE(latest.rendererGpu, -1.0);  // nullopt → sentinel
        QCOMPARE(latest.rendererVramUsedBytes, -1.0);
        QCOMPARE(latest.rendererVramTotalBytes, -1.0);
    }

    // Full Windows-shape renderer sample: GPU/VRAM present → projected.
    RendererStats full;
    full.cpuPercent = 20.0;
    full.rssBytes = 3000;
    full.gpuPercent = 42.0;
    full.vramUsedBytes = 5 * 1024 * 1024 * 1024LL;
    full.vramTotalBytes = 8 * 1024 * 1024 * 1024LL;
    full.timestampMs = 9;
    model->mergeRenderer(full);
    {
        const QVector<pet::MonitorSample> h = api.history(uuid);
        QCOMPARE(h.size(), 3);
        const pet::MonitorSample& latest = h.at(2);
        QCOMPARE(latest.rendererGpu, 42.0);
        QCOMPARE(latest.rendererVramUsedBytes, 5.0 * 1024 * 1024 * 1024);
        QCOMPARE(latest.rendererVramTotalBytes, 8.0 * 1024 * 1024 * 1024);
        // Oldest → newest ordering.
        QVERIFY(h.at(0).capturedAtMs <= h.at(1).capturedAtMs);
        QVERIFY(h.at(1).capturedAtMs <= h.at(2).capturedAtMs);
    }
}

// ── 2. subscribe pushes incrementally ──────────────────────────────────────

void MonitorApiImplTest::testSubscribePushesIncrementally()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    WsServer server;
    PendingRequests pending;
    InstanceManager mgr(base.path(), server, pending,
                        [&base](const PanelConfig& cfg) {
                            PanelStateManager(base.path()).save(cfg);
                        });
    core::MonitorApiImpl api(&mgr, nullptr);
    const QString uuid = mgr.createInstance(QStringLiteral("Sub"));
    auto* model =
        static_cast<MonitorDataModel*>(mgr.instanceAt(0)->monitorModel());

    CountingObserver obs;
    pet::MonitorSubscription sub;
    sub.minIntervalMs = 0; // push every sample
    QCOMPARE(api.subscribe(uuid, &obs, sub), pet::PluginError::Ok);

    model->mergeController(controllerSample(1.0, 10));
    model->mergeRenderer(rendererSample(2.0, 20));
    QCOMPARE(obs.count, 2);
    QCOMPARE(obs.lastUuid, uuid);

    // Each push carries the LATEST snapshot (controller half carried
    // forward into the renderer-merge push).
    QCOMPARE(obs.samples.at(1).rendererCpu, 2.0);
    QCOMPARE(obs.samples.at(1).controllerCpu, 1.0);
}

// ── 3. throttle skips samples inside the window ────────────────────────────

void MonitorApiImplTest::testThrottleSkipsSamples()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    WsServer server;
    PendingRequests pending;
    InstanceManager mgr(base.path(), server, pending,
                        [&base](const PanelConfig& cfg) {
                            PanelStateManager(base.path()).save(cfg);
                        });
    core::MonitorApiImpl api(&mgr, nullptr);
    const QString uuid = mgr.createInstance(QStringLiteral("Thr"));
    auto* model =
        static_cast<MonitorDataModel*>(mgr.instanceAt(0)->monitorModel());

    CountingObserver slow;
    pet::MonitorSubscription sub;
    sub.minIntervalMs = 60000; // window can never elapse inside a test
    QCOMPARE(api.subscribe(uuid, &slow, sub), pet::PluginError::Ok);

    model->mergeController(controllerSample(1.0, 10));
    model->mergeController(controllerSample(2.0, 20));
    model->mergeController(controllerSample(3.0, 30));
    // First sample always pushes (lastPushMs starts at 0); the two inside
    // the window are merged away — no queue, no replay later.
    QCOMPARE(slow.count, 1);
    QCOMPARE(slow.samples.at(0).controllerCpu, 1.0);

    // Zero interval → every sample pushes.
    CountingObserver fast;
    pet::MonitorSubscription zero;
    zero.minIntervalMs = 0;
    QCOMPARE(api.subscribe(uuid, &fast, zero), pet::PluginError::Ok);
    model->mergeController(controllerSample(4.0, 40));
    model->mergeController(controllerSample(5.0, 50));
    QCOMPARE(fast.count, 2);
    // The slow subscriber is still inside its window.
    QCOMPARE(slow.count, 1);
}

// ── 4. unsubscribe stops the stream ─────────────────────────────────────────

void MonitorApiImplTest::testUnsubscribeStopsPush()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    WsServer server;
    PendingRequests pending;
    InstanceManager mgr(base.path(), server, pending,
                        [&base](const PanelConfig& cfg) {
                            PanelStateManager(base.path()).save(cfg);
                        });
    core::MonitorApiImpl api(&mgr, nullptr);
    const QString uuid = mgr.createInstance(QStringLiteral("Uns"));
    auto* model =
        static_cast<MonitorDataModel*>(mgr.instanceAt(0)->monitorModel());

    CountingObserver obs;
    pet::MonitorSubscription zero;
    zero.minIntervalMs = 0;
    QCOMPARE(api.subscribe(uuid, &obs, zero), pet::PluginError::Ok);
    model->mergeController(controllerSample(1.0, 10));
    QCOMPARE(obs.count, 1);

    api.unsubscribe(uuid, &obs);
    model->mergeController(controllerSample(2.0, 20));
    model->mergeRenderer(rendererSample(3.0, 30));
    QCOMPARE(obs.count, 1); // stream stopped

    // Unknown pairs are no-ops, never crashes.
    api.unsubscribe(QStringLiteral("nope"), &obs);
    api.unsubscribe(uuid, nullptr);
    QCOMPARE(api.history(uuid).size(), 3);
}

// ── 5. NotFound / InvalidArgument matrix ────────────────────────────────────

void MonitorApiImplTest::testSubscribeUnknownUuidNotFound()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    WsServer server;
    PendingRequests pending;
    InstanceManager mgr(base.path(), server, pending,
                        [&base](const PanelConfig& cfg) {
                            PanelStateManager(base.path()).save(cfg);
                        });
    core::MonitorApiImpl api(&mgr, nullptr);

    CountingObserver obs;
    pet::MonitorSubscription sub;
    QCOMPARE(api.subscribe(QStringLiteral("nope"), &obs, sub),
             pet::PluginError::NotFound);

    const QString uuid = mgr.createInstance(QStringLiteral("Nf"));
    QCOMPARE(api.subscribe(uuid, nullptr, sub),
             pet::PluginError::InvalidArgument);

    // Degraded wiring (null manager): history empty, subscribe NotFound.
    core::MonitorApiImpl degraded(nullptr, nullptr);
    QCOMPARE(degraded.history(uuid).size(), 0);
    QCOMPARE(degraded.subscribe(uuid, &obs, sub),
             pet::PluginError::NotFound);
}

// ── 6. per-subscriber independent throttle ─────────────────────────────────

void MonitorApiImplTest::testMultiSubscriberIndependentThrottle()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    WsServer server;
    PendingRequests pending;
    InstanceManager mgr(base.path(), server, pending,
                        [&base](const PanelConfig& cfg) {
                            PanelStateManager(base.path()).save(cfg);
                        });
    core::MonitorApiImpl api(&mgr, nullptr);
    const QString uuid = mgr.createInstance(QStringLiteral("Mul"));
    auto* model =
        static_cast<MonitorDataModel*>(mgr.instanceAt(0)->monitorModel());

    CountingObserver slow;
    CountingObserver fast;
    pet::MonitorSubscription slowSub;
    slowSub.minIntervalMs = 60000;
    pet::MonitorSubscription fastSub;
    fastSub.minIntervalMs = 0;
    QCOMPARE(api.subscribe(uuid, &slow, slowSub), pet::PluginError::Ok);
    QCOMPARE(api.subscribe(uuid, &fast, fastSub), pet::PluginError::Ok);

    model->mergeController(controllerSample(1.0, 10));
    model->mergeController(controllerSample(2.0, 20));
    QCOMPARE(slow.count, 1);
    QCOMPARE(fast.count, 2);

    // Re-subscribing the slow observer refreshes the interval AND resets
    // the throttle window — the next sample pushes again.
    pet::MonitorSubscription refreshed;
    refreshed.minIntervalMs = 0;
    QCOMPARE(api.subscribe(uuid, &slow, refreshed), pet::PluginError::Ok);
    model->mergeController(controllerSample(3.0, 30));
    QCOMPARE(slow.count, 2);
    QCOMPARE(slow.samples.at(1).controllerCpu, 3.0);
    QCOMPARE(fast.count, 3);
}

// ── 7. observer unsubscribes inside the callback ───────────────────────────

void MonitorApiImplTest::testObserverUnsubscribingInCallback()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    WsServer server;
    PendingRequests pending;
    InstanceManager mgr(base.path(), server, pending,
                        [&base](const PanelConfig& cfg) {
                            PanelStateManager(base.path()).save(cfg);
                        });
    core::MonitorApiImpl api(&mgr, nullptr);
    const QString uuid = mgr.createInstance(QStringLiteral("Cb"));
    auto* model =
        static_cast<MonitorDataModel*>(mgr.instanceAt(0)->monitorModel());

    // Self-unsubscribing observer: the fanout must survive the removal
    // and a second observer must still receive its push.
    struct SelfUnsubscribing : public pet::IMonitorObserver {
        core::MonitorApiImpl* api = nullptr;
        QString uuid;
        int count = 0;
        void sample(const QString& u, const pet::MonitorSample&) override
        {
            ++count;
            api->unsubscribe(u, this);
        }
    } suicider;
    suicider.api = &api;
    suicider.uuid = uuid;

    CountingObserver other;
    pet::MonitorSubscription zero;
    zero.minIntervalMs = 0;
    QCOMPARE(api.subscribe(uuid, &suicider, zero), pet::PluginError::Ok);
    QCOMPARE(api.subscribe(uuid, &other, zero), pet::PluginError::Ok);

    model->mergeController(controllerSample(1.0, 10));
    model->mergeController(controllerSample(2.0, 20));
    QCOMPARE(suicider.count, 1); // pushed once, then unsubscribed itself
    QCOMPARE(other.count, 2);    // unaffected by the sibling's removal
    Q_UNUSED(suicider.uuid);
}

QTEST_MAIN(MonitorApiImplTest)
#include "MonitorApiImplTest.moc"
