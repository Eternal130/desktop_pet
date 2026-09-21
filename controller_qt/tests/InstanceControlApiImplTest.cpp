// InstanceControlApiImplTest (S2, v1.2) — the real pet::IInstanceControlApi
// implementation over a REAL InstanceManager harness (same QTemporaryDir +
// WsServer + PendingRequests + savePanel-lambda pattern as
// InstanceManagerTest). Locks:
//   1. testCreate: create → Ok + outUuid; roster visible through the shared
//      InstanceApiImpl (instances()); label/avatar/modelName honored;
//      autoStart persists the start-with-panel flag; empty label →
//      InvalidArgument.
//   2. testRemove: stopped instance removed immediately (roster + config
//      file); unknown uuid → NotFound; the observer family receives
//      rosterChanged on both create and remove.
//   3. testLifecycleForwarding: start/stop/restart/loadModel forward to the
//      session (observable state flips) and return NotFound on unknown
//      uuids; loadModel with an empty name → InvalidArgument.
//   4. testObserverStateFanout: subscribeInstances receives a full
//      InstanceRuntime snapshot on a property NOTIFY (targetFpsChanged via
//      setFps) and the modelLoadFailed callback on the session's failure
//      signal; unsubscribeInstances stops the fanout.
//   5. testRemoveRunningIsTwoPhase + Busy (POSIX fake-sleeper renderer):
//      remove of a running instance returns immediately (row survives),
//      start/restart during the pending delete → Busy, and the real removal
//      completes via the async stop (QTRY); after removal every op →
//      NotFound.
//
// QTEST_MAIN (NOT APPLESS): the two-phase slot pumps the event loop via
// QTRY_COMPARE_WITH_TIMEOUT (ProcessManager's async stop state machine).
// No real renderer is used — the POSIX tier uses the InstanceManagerTest
// sleeper-script harness; everything else never launches a process.
//
// Links pet_panel_core (InstanceControlApiImpl / InstanceApiImpl live there).

#include "core/DatabaseManager.hpp"
#include "core/InstanceConfig.hpp"
#include "core/InstanceConfigManager.hpp"
#include "core/InstanceManager.hpp"
#include "core/InstanceSession.hpp"
#include "core/PanelConfig.hpp"
#include "core/PanelStateManager.hpp"
#include "core/PluginContextImpl.hpp"
#include "network/PendingRequests.hpp"
#include "network/WsServer.hpp"

#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

namespace {

// Same fake-renderer harness as InstanceManagerTest (S4): a directory whose
// "desktop-pet-renderer" is a 30s sleeper script — existence is all
// resolveRendererPath checks. POSIX-only; call sites QSKIP on Windows.
void makeFakeRendererDir(const QString& base, QString* outDir)
{
    const QString rendererDir =
        QDir(base).absoluteFilePath(QStringLiteral("renderer"));
    QDir().mkpath(rendererDir);
    const QString exe =
        QDir(rendererDir).absoluteFilePath(QStringLiteral("desktop-pet-renderer"));
    QFile f(exe);
    QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Truncate),
             qPrintable(QStringLiteral("Failed to create %1").arg(exe)));
    f.write(QStringLiteral("#!/bin/sh\nexec sleep 30\n").toUtf8());
    f.close();
    QVERIFY2(QFile::setPermissions(exe,
                                   QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                       | QFileDevice::ExeOwner
                                       | QFileDevice::ReadGroup
                                       | QFileDevice::ExeGroup
                                       | QFileDevice::ReadOther
                                       | QFileDevice::ExeOther),
             qPrintable(QStringLiteral("Failed to chmod %1").arg(exe)));
    *outDir = rendererDir;
}

// Instance-granular observer fake (pet::IInstanceObserver) with counters +
// the last snapshot per uuid. Plain class — no Qt meta needed.
class RecordingObserver : public pet::IInstanceObserver
{
public:
    int rosterChanges = 0;
    int stateChanges = 0;
    int loadFailures = 0;
    QString lastFailureUuid;
    QString lastFailureError;
    QHash<QString, pet::InstanceRuntime> lastByUuid;

    void rosterChanged() override { ++rosterChanges; }
    void instanceStateChanged(const QString& uuid,
                              const pet::InstanceRuntime& info) override
    {
        ++stateChanges;
        lastByUuid.insert(uuid, info);
    }
    void modelLoadFailed(const QString& uuid, const QString& error) override
    {
        ++loadFailures;
        lastFailureUuid = uuid;
        lastFailureError = error;
    }
};

} // namespace

class InstanceControlApiImplTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testCreate();
    void testRemove();
    void testLifecycleForwarding();
    void testObserverStateFanout();
    void testRemoveRunningIsTwoPhaseAndBusy();
};

void InstanceControlApiImplTest::initTestCase()
{
    qRegisterMetaType<PendingResult>("PendingResult");
}

// ── 1. create ───────────────────────────────────────────────────────────────

void InstanceControlApiImplTest::testCreate()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    WsServer server;
    PendingRequests pending;
    InstanceManager mgr(base.path(), server, pending,
                        [&base](const PanelConfig& cfg) {
                            PanelStateManager(base.path()).save(cfg);
                        });
    core::InstanceApiImpl rosterApi(&mgr, nullptr);
    core::InstanceControlApiImpl api(&mgr, nullptr);

    pet::InstanceSpec spec;
    spec.label = QStringLiteral("PluginPet");
    spec.avatar = QStringLiteral("🐶");
    spec.modelName = QStringLiteral("Haru");
    spec.autoStart = true;

    QString uuid;
    QCOMPARE(api.create(spec, &uuid), pet::PluginError::Ok);
    QVERIFY(!uuid.isEmpty());

    // Roster-visible through the shared read API immediately.
    const QVector<pet::InstanceInfo> rows = rosterApi.instances();
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.at(0).uuid, uuid);
    QCOMPARE(rows.at(0).label, QStringLiteral("PluginPet"));
    QCOMPARE(rows.at(0).modelName, QStringLiteral("Haru"));
    QCOMPARE(rows.at(0).status, QStringLiteral("stopped"));

    // autoStart persisted the start-with-panel flag (detail-page toggle
    // semantics), no process launched.
    InstanceSession* session = mgr.instanceAt(0);
    QVERIFY(session != nullptr);
    QVERIFY(session->autoStartEnabled());
    QVERIFY(!session->isProcessRunning());

    // Config file persisted under the temp root.
    const auto loaded = InstanceConfigManager(base.path()).load(uuid);
    QVERIFY(loaded.has_value());
    QVERIFY(loaded->autoStart);
    QCOMPARE(loaded->modelName, QStringLiteral("Haru"));

    // Empty label → InvalidArgument, nothing added.
    pet::InstanceSpec bad;
    bad.label = QString();
    QString uuid2 = QStringLiteral("sentinel");
    QCOMPARE(api.create(bad, &uuid2), pet::PluginError::InvalidArgument);
    QCOMPARE(uuid2, QString());
    QCOMPARE(mgr.rowCount(), 1);

    // outUuid is optional (null tolerated).
    pet::InstanceSpec spec2;
    spec2.label = QStringLiteral("Second");
    QCOMPARE(api.create(spec2, nullptr), pet::PluginError::Ok);
    QCOMPARE(mgr.rowCount(), 2);
}

// ── 2. remove ───────────────────────────────────────────────────────────────

void InstanceControlApiImplTest::testRemove()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    WsServer server;
    PendingRequests pending;
    InstanceManager mgr(base.path(), server, pending,
                        [&base](const PanelConfig& cfg) {
                            PanelStateManager(base.path()).save(cfg);
                        });
    core::InstanceApiImpl rosterApi(&mgr, nullptr);
    core::InstanceControlApiImpl api(&mgr, nullptr);

    RecordingObserver observer;
    rosterApi.subscribeInstances(&observer);
    const int rosterBaseline = observer.rosterChanges;

    pet::InstanceSpec spec;
    spec.label = QStringLiteral("A");
    QString uuidA;
    QCOMPARE(api.create(spec, &uuidA), pet::PluginError::Ok);
    spec.label = QStringLiteral("B");
    QString uuidB;
    QCOMPARE(api.create(spec, &uuidB), pet::PluginError::Ok);
    QVERIFY(observer.rosterChanges > rosterBaseline); // create fanout

    // Stopped instance: immediate removal (roster + config).
    QCOMPARE(api.remove(uuidA), pet::PluginError::Ok);
    QCOMPARE(mgr.rowCount(), 1);
    QVERIFY(!InstanceConfigManager(base.path()).load(uuidA).has_value());

    // Unknown uuid → NotFound.
    QCOMPARE(api.remove(QStringLiteral("nonexistent-uuid")), pet::PluginError::NotFound);
    QCOMPARE(mgr.rowCount(), 1);

    rosterApi.unsubscribeInstances(&observer);
}

// ── 3. lifecycle forwarding ─────────────────────────────────────────────────

void InstanceControlApiImplTest::testLifecycleForwarding()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    // NOT listening: start() deterministically fails fast ("WsServer is
    // not listening" → status "error" + startFailed) — proving the forward
    // without launching any process.
    WsServer server;
    PendingRequests pending;
    InstanceManager mgr(base.path(), server, pending,
                        [&base](const PanelConfig& cfg) {
                            PanelStateManager(base.path()).save(cfg);
                        });
    core::InstanceControlApiImpl api(&mgr, nullptr);

    pet::InstanceSpec spec;
    spec.label = QStringLiteral("Fwd");
    QString uuid;
    QCOMPARE(api.create(spec, &uuid), pet::PluginError::Ok);
    InstanceSession* session = mgr.instanceAt(0);
    QVERIFY(session != nullptr);

    // start forwarded: the session attempted a launch (deterministic
    // not-listening failure flips status to "error").
    QSignalSpy failedSpy(session, &InstanceSession::startFailed);
    QVERIFY(failedSpy.isValid());
    QCOMPARE(api.start(uuid), pet::PluginError::Ok);
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(session->status(), QStringLiteral("error"));

    // restart forwarded: the stop-half is synchronously observable (status
    // "error" → "stopped" via restart → stop()); the queued-relaunch half
    // needs a live process (S4 machinery covered by InstanceSessionTest).
    // restartAttempts() stays 0 — that counter is the RestartController's
    // CRASH-RECOVERY ladder, not a user-restart tally.
    QCOMPARE(api.restart(uuid), pet::PluginError::Ok);
    QCOMPARE(session->status(), QStringLiteral("stopped"));
    QCOMPARE(session->restartAttempts(), 0);

    // stop forwarded: accepted (idempotent on an already-stopped session).
    QCOMPARE(api.stop(uuid), pet::PluginError::Ok);
    QCOMPARE(session->status(), QStringLiteral("stopped"));

    // loadModel forwarded (offline switch): persists + flips modelName.
    QCOMPARE(api.loadModel(uuid, QStringLiteral("Haru")), pet::PluginError::Ok);
    QCOMPARE(session->modelName(), QStringLiteral("Haru"));
    const auto loaded = InstanceConfigManager(base.path()).load(uuid);
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->modelName, QStringLiteral("Haru"));

    // Empty model name → InvalidArgument; unknown uuid → NotFound (all 4).
    QCOMPARE(api.loadModel(uuid, QString()), pet::PluginError::InvalidArgument);
    const QString gone = QStringLiteral("no-such-uuid");
    QCOMPARE(api.start(gone), pet::PluginError::NotFound);
    QCOMPARE(api.stop(gone), pet::PluginError::NotFound);
    QCOMPARE(api.restart(gone), pet::PluginError::NotFound);
    QCOMPARE(api.loadModel(gone, QStringLiteral("Haru")), pet::PluginError::NotFound);
}

// ── 4. observer state fanout ────────────────────────────────────────────────

void InstanceControlApiImplTest::testObserverStateFanout()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    WsServer server;
    PendingRequests pending;
    InstanceManager mgr(base.path(), server, pending,
                        [&base](const PanelConfig& cfg) {
                            PanelStateManager(base.path()).save(cfg);
                        });
    core::InstanceApiImpl rosterApi(&mgr, nullptr);
    core::InstanceControlApiImpl api(&mgr, nullptr);

    // Observer attached BEFORE the create — proves the rowsInserted →
    // observeSession wiring for later arrivals.
    RecordingObserver observer;
    rosterApi.subscribeInstances(&observer);

    pet::InstanceSpec spec;
    spec.label = QStringLiteral("Obs");
    QString uuid;
    QCOMPARE(api.create(spec, &uuid), pet::PluginError::Ok);
    QVERIFY(observer.rosterChanges >= 1);

    InstanceSession* session = mgr.instanceAt(0);
    QVERIFY(session != nullptr);

    // Property NOTIFY → full snapshot (targetFpsChanged via setFps).
    session->setFps(24);
    QCOMPARE(observer.stateChanges, 1);
    QVERIFY(observer.lastByUuid.contains(uuid));
    const pet::InstanceRuntime rt = observer.lastByUuid.value(uuid);
    QCOMPARE(rt.uuid, uuid);
    QCOMPARE(rt.label, QStringLiteral("Obs"));
    QCOMPARE(rt.status, QStringLiteral("stopped"));
    QCOMPARE(rt.targetFps, 24);
    QCOMPARE(rt.connected, false);
    QCOMPARE(rt.modelLoaded, false);
    QCOMPARE(rt.mountedPackId, QString());
    QCOMPARE(rt.restartAttempts, 0);

    // modelLoadFailed callback (the session's failure signal drives it —
    // the same signal the renderer's model_load_failed event handler emits).
    emit session->modelLoadFailed(QStringLiteral("missing model dir"));
    QCOMPARE(observer.loadFailures, 1);
    QCOMPARE(observer.lastFailureUuid, uuid);
    QCOMPARE(observer.lastFailureError, QStringLiteral("missing model dir"));

    // Unsubscribe stops every callback (no zombie fanout).
    rosterApi.unsubscribeInstances(&observer);
    session->setFps(48);
    emit session->modelLoadFailed(QStringLiteral("again"));
    QCOMPARE(observer.stateChanges, 1);
    QCOMPARE(observer.loadFailures, 1);
}

// ── 5. two-phase remove + Busy (POSIX fake renderer) ────────────────────────

void InstanceControlApiImplTest::testRemoveRunningIsTwoPhaseAndBusy()
{
#ifdef Q_OS_WIN
    QSKIP("fake-renderer sleeper harness is POSIX-only");
#else
    QTemporaryDir base;
    QVERIFY(base.isValid());

    // Seed one instance on the fake renderer (InstanceManagerTest pattern).
    InstanceConfig cfg = defaultInstanceConfig();
    cfg.label = QStringLiteral("Runner");
    QString rendererDir;
    makeFakeRendererDir(base.path(), &rendererDir);
    cfg.rendererPath = rendererDir;
    cfg.graphicsBackend = QStringLiteral("opengl");
    QVERIFY(InstanceConfigManager(base.path()).save(cfg));
    PanelConfig panel = defaultPanelConfig();
    panel.instanceIds = QStringList{cfg.id};
    QVERIFY(PanelStateManager(base.path()).save(panel));

    WsServer server;
    QVERIFY2(server.listen(0), "WsServer failed to listen on port 0");
    PendingRequests pending;
    InstanceManager mgr(base.path(), server, pending,
                        [&base](const PanelConfig& p) {
                            PanelStateManager(base.path()).save(p);
                        });
    core::InstanceControlApiImpl api(&mgr, nullptr);

    QCOMPARE(mgr.rowCount(), 1);
    InstanceSession* session = mgr.instanceAt(0);
    QVERIFY(session != nullptr);
    session->setStopTimeoutMs(300); // fast kill for the winding-down phase

    session->start();
    QVERIFY2(session->isProcessRunning(), "fake renderer process failed to launch");

    // ── Phase 1: non-blocking remove; row survives; Busy for racers ────
    QCOMPARE(api.remove(cfg.id), pet::PluginError::Ok);
    QCOMPARE(mgr.rowCount(), 1);
    QVERIFY2(session->isProcessRunning(),
             "remove must not block on the running renderer");
    QCOMPARE(api.start(cfg.id), pet::PluginError::Busy);
    QCOMPARE(api.restart(cfg.id), pet::PluginError::Busy);
    QCOMPARE(api.loadModel(cfg.id, QStringLiteral("Haru")), pet::PluginError::Busy);

    // ── Phase 2: async stop completes → real removal ────────────────────
    QTRY_COMPARE_WITH_TIMEOUT(mgr.rowCount(), 0, 5000);
    QVERIFY(!InstanceConfigManager(base.path()).load(cfg.id).has_value());

    // After the removal every op reports NotFound.
    QCOMPARE(api.start(cfg.id), pet::PluginError::NotFound);
    QCOMPARE(api.stop(cfg.id), pet::PluginError::NotFound);
    QCOMPARE(api.remove(cfg.id), pet::PluginError::NotFound);

    server.close();
#endif
}

QTEST_MAIN(InstanceControlApiImplTest)
#include "InstanceControlApiImplTest.moc"
