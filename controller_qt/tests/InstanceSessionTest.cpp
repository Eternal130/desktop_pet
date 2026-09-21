// InstanceSessionTest (Phase 5, todo 2) — the per-instance orchestrator.
//
// Two test tiers:
//
//   Unit tier (no renderer needed):
//     - testInitialState: fresh session → status="stopped", connected=false,
//       modelLoaded=false, instanceId derived from config UUID.
//     - testQPropertyReads: all 10 Q_PROPERTY accessors return config defaults.
//     - testSettersPersistAndEmit: setOpacity/setVolume/setMuted/setFps mutate
//       m_config and fire their NOTIFY signal.
//     - testBogusPathFailsGracefully: start() with an empty rendererPath →
//       defaultRendererDir() (test-exe-relative) finds no renderer → startFailed
//       emitted synchronously, status="error", no crash. THE core unit test.
//     - testStopIsClean: stop() on a non-running session → status="stopped",
//       no startFailed.
//
//   Integration tier [REQUIRES_RENDERER]:
//     - testRealRendererLifecycle: start → ready → status="running" → stop →
//       status="stopped". Verifies the ready override + 3-stage stop.
//     - testStopSuppressesCrashSignal: the key renderer-teardown-crash guard —
//       stop() with manuallyStopping=true must NOT emit startFailed even though
//       the renderer hits its known 0xC0000005 teardown access-violation.
//
// QTEST_MAIN (NOT APPLESS): QSignalSpy::wait / QTest::qWait pump the event loop
// that QProcess + WebSocket I/O require. Integration tests launch the REAL
// desktop-pet-renderer.exe — requires a display, hence REQUIRES_RENDERER label.

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QString>
#include <QTest>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QDir>
#include <QFile>

#include <optional>

#include "core/InstanceConfig.hpp"
#include "core/InstanceConfigManager.hpp"
#include "core/InstanceSession.hpp"
#include "core/PathResolve.hpp"
#include "network/Envelope.hpp"
#include "network/PendingRequests.hpp"
#include "network/WsServer.hpp"

class InstanceSessionTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();

    // ── Unit tier (no renderer) ────────────────────────────────────────────
    void testInitialState();
    void testQPropertyReads();
    void testSettersPersistAndEmit();
    void testBogusPathFailsGracefully();
    void testStopIsClean();
    // 模型库 B 档 (model library): offline model switch + load-failure
    // observability — both headless.
    void testOfflineLoadModelPersistsConfigOnly();
    void testModelLoadFailedIsObservable();

    // ── S4 async lifecycle (headless fake-sleeper renderer; POSIX) ──────
    void testRestartWaitsForAsyncStop();
    void testStartDuringStopIsQueued();

    // ── Integration tier [REQUIRES_RENDERER] ───────────────────────────────
    void testRealRendererLifecycle();
    void testStopSuppressesCrashSignal();
    void testFullLifecycleSalvoAndHitAreas();
    void testDragEndPersists();
    void testHitSendsPlayMotion();
    void testMotionFinishedNonIdleTriggersScheduler();
    void testMotionFinishedIdleDoesNotTrigger();

    // ── Phase-5 Wave 8 todo 22 (layout) [REQUIRES_RENDERER] ───────────────
    void testLayoutChangedPersists();
    void testWindowResizedPersists();
    void testResetLayoutSendsCommand();
    void testGetLayoutRoutesViaLayoutStateEvent();

private:
    static std::optional<QString> findRenderer();

    // Wait until session.status() equals target, polling via QTest::qWait.
    static bool waitForStatus(InstanceSession& session, const char* target,
                              int timeoutMs);

    // S4: call stop() and wait for the session-level stopFinished signal —
    // the async replacement for asserting "stop() returned ⇒ stopped".
    static bool stopAndWait(InstanceSession& session, int timeoutMs);

    // S4 headless harness: write a fake "renderer" that is really a sleeper
    // script (existence is all resolveRendererPath checks; shebang + exec
    // bit make QProcess run it) into dir; outDir receives the DIRECTORY to
    // set as cfg.rendererPath. POSIX-only — call sites QSKIP on Windows.
    // Out-param signature — QVERIFY2 needs a void return.
    static void makeFakeRendererDir(const QTemporaryDir& dir, QString* outDir);
};

void InstanceSessionTest::initTestCase()
{
    // Not strictly needed (no QSignalSpy on Envelope here), but harmless and
    // matches the project convention.
}

// ────────────────────────────────────────────────────────────────────────────
// Unit tier
// ────────────────────────────────────────────────────────────────────────────

void InstanceSessionTest::testInitialState()
{
    WsServer server;
    PendingRequests pending;
    InstanceConfig cfg = defaultInstanceConfig();
    cfg.label = QStringLiteral("Test Pet");

    InstanceSession session(cfg, server, pending);
    QCOMPARE(session.status(), QStringLiteral("stopped"));
    QVERIFY(!session.connected());
    QVERIFY(!session.modelLoaded());
    QCOMPARE(session.label(), QStringLiteral("Test Pet"));
    QVERIFY(session.instanceId() != 0); // hash of a UUID is effectively never 0
}

void InstanceSessionTest::testQPropertyReads()
{
    WsServer server;
    PendingRequests pending;
    InstanceConfig cfg = defaultInstanceConfig();
    cfg.modelName = QStringLiteral("Haru");
    cfg.opacity = 0.75;
    cfg.targetFps = 30;
    cfg.volume = 0.5;
    cfg.muted = true;

    InstanceSession session(cfg, server, pending);
    QCOMPARE(session.modelName(), QStringLiteral("Haru"));
    QCOMPARE(session.opacity(), 0.75);
    QCOMPARE(session.targetFps(), 30);
    QCOMPARE(session.volume(), 0.5);
    QCOMPARE(session.muted(), true);
    QCOMPARE(session.status(), QStringLiteral("stopped"));
    QVERIFY(!session.connected());
    QVERIFY(!session.modelLoaded());
}

void InstanceSessionTest::testSettersPersistAndEmit()
{
    WsServer server;
    PendingRequests pending;
    InstanceSession session(defaultInstanceConfig(), server, pending);

    QSignalSpy opacitySpy(&session, &InstanceSession::opacityChanged);
    QSignalSpy volumeSpy(&session, &InstanceSession::volumeChanged);
    QSignalSpy mutedSpy(&session, &InstanceSession::mutedChanged);
    QSignalSpy fpsSpy(&session, &InstanceSession::targetFpsChanged);
    QVERIFY(opacitySpy.isValid());
    QVERIFY(volumeSpy.isValid());
    QVERIFY(mutedSpy.isValid());
    QVERIFY(fpsSpy.isValid());

    session.setOpacity(0.3);
    QCOMPARE(session.opacity(), 0.3);
    QCOMPARE(session.config().opacity, 0.3);
    QCOMPARE(opacitySpy.count(), 1);

    // No-op set (same value) does not re-emit.
    session.setOpacity(0.3);
    QCOMPARE(opacitySpy.count(), 1);

    session.setVolume(0.8);
    QCOMPARE(session.volume(), 0.8);
    QCOMPARE(session.config().volume, 0.8);
    QCOMPARE(volumeSpy.count(), 1);

    session.setMuted(true);
    QCOMPARE(session.muted(), true);
    QCOMPARE(session.config().muted, true);
    QCOMPARE(mutedSpy.count(), 1);

    session.setFps(60);
    QCOMPARE(session.targetFps(), 60);
    QCOMPARE(session.config().targetFps, 60);
    QCOMPARE(fpsSpy.count(), 1);
}

// THE core unit test: start() with no renderer resolvable → graceful failure.
// config.rendererPath is empty, so InstanceSession falls back to
// defaultRendererDir() (applicationDirPath + "/../build/bin"). The test exe
// lives in build/controller_qt/tests/, so that resolves to a non-existent
// directory → PathResolve returns nullopt → startFailed + status="error".
void InstanceSessionTest::testBogusPathFailsGracefully()
{
    WsServer server;
    QVERIFY2(server.listen(0), "WsServer failed to listen on port 0");
    PendingRequests pending;

    InstanceConfig cfg = defaultInstanceConfig();
    cfg.rendererPath.clear(); // force defaultRendererDir fallback (not found)

    InstanceSession session(cfg, server, pending);
    QSignalSpy failedSpy(&session, &InstanceSession::startFailed);
    QSignalSpy statusSpy(&session, &InstanceSession::statusChanged);
    QVERIFY(failedSpy.isValid());
    QVERIFY(statusSpy.isValid());

    session.start();

    // startFailed is emitted synchronously (path resolution is synchronous).
    QCOMPARE(failedSpy.count(), 1);
    const QString reason = failedSpy.at(0).at(0).toString();
    QVERIFY2(reason.contains(QStringLiteral("not found")),
             qPrintable(QStringLiteral("expected 'not found' in reason, got: %1").arg(reason)));
    QCOMPARE(session.status(), QStringLiteral("error"));
    QVERIFY(!session.connected());
    QVERIFY(!session.modelLoaded());

    server.close();
}

// stop() on a session that never started (or whose start failed) must leave
// status="stopped" and never emit startFailed. The manuallyStopping flag
// suppresses any crash handling in onProcessExited.
void InstanceSessionTest::testStopIsClean()
{
    WsServer server;
    QVERIFY2(server.listen(0), "WsServer failed to listen on port 0");
    PendingRequests pending;
    InstanceSession session(defaultInstanceConfig(), server, pending);

    QSignalSpy failedSpy(&session, &InstanceSession::startFailed);
    QVERIFY(failedSpy.isValid());

    session.stop();

    QCOMPARE(session.status(), QStringLiteral("stopped"));
    QVERIFY(!session.connected());
    QVERIFY(!session.modelLoaded());
    QCOMPARE(failedSpy.count(), 0); // no crash signal from a clean stop

    server.close();
}

// 模型库 B 档 — offline model switch: loadModel on a NOT-connected session
// must (a) update modelName + emit modelNameChanged, (b) PERSIST the choice
// (the next start() launches the renderer with --model from the config), and
// (c) send NO load_model command (the renderer is not there to receive it —
// decision: 未启动实例换模 = 只写配置不发命令).
void InstanceSessionTest::testOfflineLoadModelPersistsConfigOnly()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");

    WsServer server;
    PendingRequests pending;
    InstanceConfig cfg = defaultInstanceConfig();
    QVERIFY(!cfg.modelName.isEmpty()); // precondition: a default exists to switch FROM

    // configBasePath = temp dir so the persist lands in the test's own db.
    InstanceSession session(cfg, server, pending, dir.path());

    QSignalSpy nameSpy(&session, &InstanceSession::modelNameChanged);
    QSignalSpy commandSpy(&session, &InstanceSession::commandSent);
    QVERIFY(nameSpy.isValid());
    QVERIFY(commandSpy.isValid());

    QVERIFY(!session.connected()); // precondition: offline
    session.loadModel(QStringLiteral("Haru"));

    QCOMPARE(session.modelName(), QStringLiteral("Haru"));
    QCOMPARE(session.config().modelName, QStringLiteral("Haru"));
    QCOMPARE(nameSpy.count(), 1);
    // Config-only: NO command went out (offline switch sends nothing).
    QCOMPARE(commandSpy.count(), 0);
    QVERIFY(!session.modelLoaded());

    // The choice hit disk — a fresh manager over the same dir reads it back.
    InstanceConfigManager persisted(dir.path());
    const auto reloaded = persisted.load(cfg.id);
    QVERIFY2(reloaded.has_value(), "config row missing after offline loadModel");
    QCOMPARE(reloaded->modelName, QStringLiteral("Haru"));
}

// 模型库 B 档 — model_load_failed observability: the renderer's failure event
// must bump modelLoadFailureRevision + record lastModelLoadError + emit
// modelLoadFailed so QML can surface WHY a switch failed. Headless: a fake
// renderer EXE file satisfies path resolution so start() registers the event
// handlers (the launch itself fails asynchronously — harmless, we never pump
// the loop), then the synthetic event is dispatched via onMessage.
void InstanceSessionTest::testModelLoadFailedIsObservable()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");

    // Fake renderer executable (existence is all resolveRendererPath checks).
#ifdef Q_OS_WIN
    const QString exeName = QStringLiteral("desktop-pet-renderer.exe");
#else
    const QString exeName = QStringLiteral("desktop-pet-renderer");
#endif
    QFile fakeExe(QDir(dir.path()).absoluteFilePath(exeName));
    QVERIFY2(fakeExe.open(QIODevice::WriteOnly),
             "precondition: failed to create fake renderer exe");
    fakeExe.close();

    WsServer server;
    QVERIFY2(server.listen(0), "WsServer failed to listen on port 0");
    PendingRequests pending;

    InstanceConfig cfg = defaultInstanceConfig();
    cfg.rendererPath = dir.path(); // resolveRendererPath finds the fake exe here
    cfg.graphicsBackend = QStringLiteral("opengl");

    InstanceSession session(cfg, server, pending, dir.path());
    QCOMPARE(session.modelLoadFailureRevision(), 0);
    QVERIFY(session.lastModelLoadError().isEmpty());

    // Registers the event handlers (returns before any async launch error).
    session.start();

    QSignalSpy failedSpy(&session, &InstanceSession::modelLoadFailed);
    QVERIFY(failedSpy.isValid());

    // Synthetic model_load_failed event (interface.md §B payload shape).
    Envelope env;
    env.type = QStringLiteral("event");
    env.action = QStringLiteral("model_load_failed");
    env.payload.insert(QStringLiteral("error_code"), 1001);
    env.payload.insert(QStringLiteral("error_message"),
                       QStringLiteral("Model path not found: Nope"));
    session.onMessage(env);

    QCOMPARE(session.modelLoadFailureRevision(), 1);
    QCOMPARE(session.lastModelLoadError(),
             QStringLiteral("Model path not found: Nope"));
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(failedSpy.at(0).at(0).toString(),
             QStringLiteral("Model path not found: Nope"));
    QVERIFY(!session.modelLoaded());

    // A second failure bumps the revision again (QML re-evaluation driver).
    session.onMessage(env);
    QCOMPARE(session.modelLoadFailureRevision(), 2);
    QCOMPARE(failedSpy.count(), 2);

    // Clean teardown — stops the (never-started) fake process, suppresses
    // the async FailedToStart crash path.
    QVERIFY2(stopAndWait(session, 3000), "session did not finish stopping");
    server.close();
}

// ────────────────────────────────────────────────────────────────────────────
// S4 async lifecycle (headless fake-sleeper renderer; POSIX-only harness)
// ────────────────────────────────────────────────────────────────────────────

// restart() must WAIT for the async stop before relaunching: it returns
// immediately with status="stopped" while the OLD renderer is still winding
// down, then relaunches exactly once the old process is gone. The kill of
// the old process must NOT surface as startFailed (manuallyStopping).
void InstanceSessionTest::testRestartWaitsForAsyncStop()
{
#ifdef Q_OS_WIN
    QSKIP("fake-renderer sleeper harness is POSIX-only");
#else
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");

    WsServer server;
    QVERIFY2(server.listen(0), "WsServer failed to listen on port 0");
    PendingRequests pending;

    InstanceConfig cfg = defaultInstanceConfig();
    QString rendererDir;
    makeFakeRendererDir(dir, &rendererDir); // fake renderer = sleeper
    cfg.rendererPath = rendererDir;
    cfg.graphicsBackend = QStringLiteral("opengl");

    InstanceSession session(cfg, server, pending, dir.path());
    QSignalSpy failedSpy(&session, &InstanceSession::startFailed);
    QVERIFY(failedSpy.isValid());

    session.start();
    QVERIFY2(session.isProcessRunning(), "fake renderer process failed to launch");

    session.setStopTimeoutMs(300); // fast kill (test seam)

    session.restart();

    // Async proof: restart() returned with the old renderer still winding
    // down, status already flipped to "stopped".
    QCOMPARE(session.status(), QStringLiteral("stopped"));
    QVERIFY2(session.isProcessRunning(),
             "restart() must be non-blocking (old renderer still dying)");

    // Phase 2: the queued relaunch fires once the old process is gone.
    QVERIFY2(waitForStatus(session, "connecting", 5000),
             "restart did not relaunch after the async stop completed");
    QVERIFY2(session.isProcessRunning(), "relaunched process not running");
    QVERIFY2(failedSpy.isEmpty(),
             "old-process kill must not emit startFailed (manuallyStopping guard)");

    QVERIFY2(stopAndWait(session, 5000), "session did not finish stopping");
    QCOMPARE(session.status(), QStringLiteral("stopped"));
    QVERIFY2(failedSpy.isEmpty(), "stop must not emit startFailed");
    server.close();
#endif
}

// start() while a stop is still winding down is QUEUED (never a second
// renderer next to the dying one) and fires once the stop completes.
void InstanceSessionTest::testStartDuringStopIsQueued()
{
#ifdef Q_OS_WIN
    QSKIP("fake-renderer sleeper harness is POSIX-only");
#else
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");

    WsServer server;
    QVERIFY2(server.listen(0), "WsServer failed to listen on port 0");
    PendingRequests pending;

    InstanceConfig cfg = defaultInstanceConfig();
    QString rendererDir;
    makeFakeRendererDir(dir, &rendererDir);
    cfg.rendererPath = rendererDir;
    cfg.graphicsBackend = QStringLiteral("opengl");

    InstanceSession session(cfg, server, pending, dir.path());
    QSignalSpy failedSpy(&session, &InstanceSession::startFailed);
    QVERIFY(failedSpy.isValid());

    session.start();
    QVERIFY2(session.isProcessRunning(), "fake renderer process failed to launch");

    session.setStopTimeoutMs(300);
    session.stop(); // async — old renderer winding down
    QCOMPARE(session.status(), QStringLiteral("stopped"));

    session.start(); // during the in-flight stop — must queue, not launch

    // The queued start lands after the stop completes.
    QVERIFY2(waitForStatus(session, "connecting", 5000),
             "queued start did not fire after the async stop completed");
    QVERIFY2(session.isProcessRunning(), "relaunched process not running");
    QVERIFY2(failedSpy.isEmpty(), "no failure expected in the queued path");

    QVERIFY2(stopAndWait(session, 5000), "session did not finish stopping");
    server.close();
#endif
}

// ────────────────────────────────────────────────────────────────────────────
// Integration tier [REQUIRES_RENDERER]
// ────────────────────────────────────────────────────────────────────────────

void InstanceSessionTest::testRealRendererLifecycle()
{
    const auto path = findRenderer();
    if (!path.has_value())
        QSKIP("renderer binary absent");

    WsServer server;
    QVERIFY2(server.listen(0), "WsServer failed to listen on port 0");
    PendingRequests pending;

    InstanceConfig cfg = defaultInstanceConfig();
    cfg.rendererPath = QStringLiteral(BIN_OUTPUT_DIR); // InstanceSession resolves here
    cfg.graphicsBackend = QStringLiteral("opengl");

    InstanceSession session(cfg, server, pending);

    // Wire the router: WsServer::messageReceived → session.onMessage. This is
    // what main.cpp's todo-3 router will do for real; the test wires it inline.
    QObject::connect(&server, &WsServer::messageReceived,
                     &session, [&session](int, const Envelope& env) {
                         session.onMessage(env);
                     });

    QSignalSpy failedSpy(&session, &InstanceSession::startFailed);
    QSignalSpy statusSpy(&session, &InstanceSession::statusChanged);
    QVERIFY(failedSpy.isValid());
    QVERIFY(statusSpy.isValid());

    session.start();

    // ready → status="running". The renderer takes a few seconds to boot GLFW +
    // load the model骨架 before sending ready.
    QVERIFY2(waitForStatus(session, "running", 15000),
             "renderer did not reach status='running' within 15s");
    QVERIFY(session.connected());
    QVERIFY(failedSpy.isEmpty()); // no failure during a healthy start

    // Clean stop → status="stopped", no startFailed (teardown crash suppressed).
    // S4: stop() is async — wait for the session-level completion signal.
    QSignalSpy sessionStopSpy(&session, &InstanceSession::stopFinished);
    session.stop();
    QCOMPARE(session.status(), QStringLiteral("stopped"));
    QVERIFY(!session.connected());
    QVERIFY2(sessionStopSpy.wait(7000), "session stopFinished not emitted within 7s");
    QVERIFY2(failedSpy.isEmpty(),
             "stop() must not emit startFailed (manuallyStopping suppresses crash)");

    server.close();
}

// The renderer's known teardown access-violation (0xC0000005) fires AFTER the
// WS connection cleanly closes during a user-initiated stop. InstanceSession's
// manuallyStopping flag MUST suppress startFailed for that exit. This is the
// Phase-7 crash-recovery guard (blueprint §4.6.3).
void InstanceSessionTest::testStopSuppressesCrashSignal()
{
    const auto path = findRenderer();
    if (!path.has_value())
        QSKIP("renderer binary absent");

    WsServer server;
    QVERIFY2(server.listen(0), "WsServer failed to listen on port 0");
    PendingRequests pending;

    InstanceConfig cfg = defaultInstanceConfig();
    cfg.rendererPath = QStringLiteral(BIN_OUTPUT_DIR);
    cfg.graphicsBackend = QStringLiteral("opengl");

    InstanceSession session(cfg, server, pending);
    QObject::connect(&server, &WsServer::messageReceived,
                     &session, [&session](int, const Envelope& env) {
                         session.onMessage(env);
                     });

    QSignalSpy failedSpy(&session, &InstanceSession::startFailed);
    QVERIFY(failedSpy.isValid());

    session.start();
    QVERIFY2(waitForStatus(session, "running", 15000),
             "renderer did not reach status='running' within 15s");

    QSignalSpy sessionStopSpy(&session, &InstanceSession::stopFinished);
    session.stop();
    QVERIFY2(sessionStopSpy.wait(7000), "session stopFinished not emitted within 7s");

    // The critical assertion: even though the renderer may crash during
    // teardown, manuallyStopping=true means NO startFailed.
    QVERIFY2(failedSpy.isEmpty(),
             "startFailed emitted during user-initiated stop — manuallyStopping guard failed");
    QCOMPARE(session.status(), QStringLiteral("stopped"));

    server.close();
}

// The full blueprint §8.1 lifecycle: start → ready → 7-command salvo →
// model_loaded → set_hit_areas. Asserts the salvo fires in the exact 7-action
// order AND that set_hit_areas carries Hiyori's real hitAreas (["Body"]).
// Spies on InstanceSession::commandSent — the unified outbound-command signal
// that re-emits both the salvo commands and sendCommand (set_hit_areas).
void InstanceSessionTest::testFullLifecycleSalvoAndHitAreas()
{
    const auto path = findRenderer();
    if (!path.has_value())
        QSKIP("renderer binary absent");

    WsServer server;
    QVERIFY2(server.listen(0), "WsServer failed to listen on port 0");
    PendingRequests pending;

    InstanceConfig cfg = defaultInstanceConfig();
    cfg.rendererPath = QStringLiteral(BIN_OUTPUT_DIR);
    cfg.graphicsBackend = QStringLiteral("opengl");
    cfg.modelName = QStringLiteral("Hiyori");

    InstanceSession session(cfg, server, pending);
    QObject::connect(&server, &WsServer::messageReceived,
                     &session, [&session](int, const Envelope& env) {
                         session.onMessage(env);
                     });

    QSignalSpy cmdSpy(&session, &InstanceSession::commandSent);
    QSignalSpy failedSpy(&session, &InstanceSession::startFailed);
    QVERIFY(cmdSpy.isValid());
    QVERIFY(failedSpy.isValid());

    session.start();

    // ready → status=running → salvo fires synchronously inside the ready
    // handler, so by the time we observe status=running the 7 salvo commands
    // are already captured.
    QVERIFY2(waitForStatus(session, "running", 15000),
             "renderer did not reach status='running' within 15s");

    // model_loaded → set_hit_areas. Poll for modelLoaded (set in the handler).
    QElapsedTimer t;
    t.start();
    while (!t.hasExpired(15000) && !session.modelLoaded())
        QTest::qWait(50);
    QVERIFY2(session.modelLoaded(),
             "model_loaded did not arrive within 15s");

    // Flush any in-flight commandSent emissions.
    QTest::qWait(100);

    // The 7 salvo commands in exact order (StartupSalvo.cpp sendSalvo).
    const QStringList expectedSalvo{
        QStringLiteral("load_model"),
        QStringLiteral("set_position"),
        QStringLiteral("set_size"),
        QStringLiteral("set_opacity"),
        QStringLiteral("set_fps"),
        QStringLiteral("set_volume"),
        QStringLiteral("set_layout"),
    };

    // Extract all captured actions.
    QVERIFY2(cmdSpy.count() >= 8,
             qPrintable(QStringLiteral("expected >=8 commands (7 salvo + "
                                       "set_hit_areas), got %1").arg(cmdSpy.count())));
    QStringList actions;
    for (int i = 0; i < cmdSpy.count(); ++i)
        actions.append(cmdSpy.at(i).at(0).toString());

    // The first 7 must be the salvo in order.
    QCOMPARE(actions.mid(0, 7), expectedSalvo);

    // set_hit_areas must appear after the salvo (it fires on model_loaded,
    // which arrives after ready + load_model).
    int hitAreasIdx = actions.indexOf(QStringLiteral("set_hit_areas"));
    QVERIFY2(hitAreasIdx >= 7,
             qPrintable(QStringLiteral("set_hit_areas not found after salvo "
                                       "(actions=%1)").arg(actions.join(','))));

    // ModelInfo must be populated with Hiyori's real hitAreas (["Body"]).
    QVERIFY2(session.modelInfo().has_value(),
             "modelInfo should be populated after model_loaded");
    QVERIFY(session.modelInfo()->hitAreas.contains(QStringLiteral("Body")));

    QVERIFY2(failedSpy.isEmpty(), "no failure expected during healthy lifecycle");

    // S4: async stop — wait for completion before closing the server (the
    // shutdown command flushes on the event loop this pumps).
    QVERIFY2(stopAndWait(session, 7000), "session did not finish stopping");
    QCOMPARE(session.status(), QStringLiteral("stopped"));

    server.close();
}

// ────────────────────────────────────────────────────────────────────────────
// Phase-5 todo 10 integration tests — hit/drag_end/motion_finished handlers
// ────────────────────────────────────────────────────────────────────────────

// drag_end → window_x/window_y extracted → InstanceConfigManager::save. Read
// back via a FRESH InstanceConfigManager pointed at the same configBasePath
// (a QTemporaryDir) to prove the file on disk has the new coordinates.
void InstanceSessionTest::testDragEndPersists()
{
    const auto path = findRenderer();
    if (!path.has_value())
        QSKIP("renderer binary absent");

    WsServer server;
    QVERIFY2(server.listen(0), "WsServer failed to listen on port 0");
    PendingRequests pending;
    QTemporaryDir tmpDir;
    QVERIFY2(tmpDir.isValid(), "QTemporaryDir failed to create");

    InstanceConfig cfg = defaultInstanceConfig();
    cfg.rendererPath = QStringLiteral(BIN_OUTPUT_DIR);
    cfg.graphicsBackend = QStringLiteral("opengl");
    cfg.modelName = QStringLiteral("Hiyori");

    InstanceSession session(cfg, server, pending, tmpDir.path());
    QObject::connect(&server, &WsServer::messageReceived,
                     &session, [&session](int, const Envelope& env) {
                         session.onMessage(env);
                     });

    session.start();
    QVERIFY2(waitForStatus(session, "running", 15000),
             "renderer did not reach status='running' within 15s");
    QElapsedTimer t;
    t.start();
    while (!t.hasExpired(15000) && !session.modelLoaded())
        QTest::qWait(50);
    QVERIFY2(session.modelLoaded(), "model_loaded did not arrive within 15s");

    // Inject a synthetic drag_end event with known window coordinates.
    QJsonObject payload;
    payload.insert(QStringLiteral("window_x"), 100);
    payload.insert(QStringLiteral("window_y"), 200);
    session.onMessage(createEvent(QStringLiteral("drag_end"), payload));

    // Give the save (atomic write) a beat to complete — it's synchronous on the
    // main thread but QTest::qWait flushes the event loop just in case.
    QTest::qWait(50);

    // Read back via a fresh InstanceConfigManager pointed at the same tmpDir.
    InstanceConfigManager verifier(tmpDir.path());
    const auto reloaded = verifier.load(cfg.id);
    QVERIFY2(reloaded.has_value(), "config file not found after drag_end");
    QCOMPARE(reloaded->windowX, 100);
    QCOMPARE(reloaded->windowY, 200);

    // S4: async stop — wait for completion before closing the server.
    QVERIFY2(stopAndWait(session, 7000), "session did not finish stopping");
    server.close();
}

// hit → InteractionHandler → play_motion. The commandSent signal must capture
// a play_motion action after the synthetic hit event is injected.
void InstanceSessionTest::testHitSendsPlayMotion()
{
    const auto path = findRenderer();
    if (!path.has_value())
        QSKIP("renderer binary absent");

    WsServer server;
    QVERIFY2(server.listen(0), "WsServer failed to listen on port 0");
    PendingRequests pending;

    InstanceConfig cfg = defaultInstanceConfig();
    cfg.rendererPath = QStringLiteral(BIN_OUTPUT_DIR);
    cfg.graphicsBackend = QStringLiteral("opengl");
    cfg.modelName = QStringLiteral("Hiyori");

    InstanceSession session(cfg, server, pending);
    QObject::connect(&server, &WsServer::messageReceived,
                     &session, [&session](int, const Envelope& env) {
                         session.onMessage(env);
                     });

    QSignalSpy cmdSpy(&session, &InstanceSession::commandSent);
    QVERIFY(cmdSpy.isValid());

    session.start();
    QVERIFY2(waitForStatus(session, "running", 15000),
             "renderer did not reach status='running' within 15s");
    QElapsedTimer t;
    t.start();
    while (!t.hasExpired(15000) && !session.modelLoaded())
        QTest::qWait(50);
    QVERIFY2(session.modelLoaded(), "model_loaded did not arrive within 15s");

    cmdSpy.clear(); // discard salvo + set_hit_areas; we only care about post-hit

    // Inject hit{area_id:"body"} — Hiyori's only HitArea. The 3-tier lookup
    // resolves "body" → DEFAULT_MAPPINGS["body"] → TapBody, priority 2.
    QJsonObject hitPayload;
    hitPayload.insert(QStringLiteral("area_id"), QStringLiteral("body"));
    session.onMessage(createEvent(QStringLiteral("hit"), hitPayload));

    QTest::qWait(50);

    // Assert at least one play_motion was captured.
    bool foundPlayMotion = false;
    for (int i = 0; i < cmdSpy.count(); ++i) {
        if (cmdSpy.at(i).at(0).toString() == QStringLiteral("play_motion")) {
            foundPlayMotion = true;
            break;
        }
    }
    QVERIFY2(foundPlayMotion,
             qPrintable(QStringLiteral("play_motion not captured after hit "
                                       "(actions=%1)").arg(cmdSpy.count())));

    // S4: async stop — wait for completion before closing the server.
    QVERIFY2(stopAndWait(session, 7000), "session did not finish stopping");
    server.close();
}

// motion_finished{group:"TapBody"} (non-idle) → Scheduler::triggerNow →
// idleMotionTriggered fires (the scheduler picks an Idle motion immediately).
void InstanceSessionTest::testMotionFinishedNonIdleTriggersScheduler()
{
    const auto path = findRenderer();
    if (!path.has_value())
        QSKIP("renderer binary absent");

    WsServer server;
    QVERIFY2(server.listen(0), "WsServer failed to listen on port 0");
    PendingRequests pending;

    InstanceConfig cfg = defaultInstanceConfig();
    cfg.rendererPath = QStringLiteral(BIN_OUTPUT_DIR);
    cfg.graphicsBackend = QStringLiteral("opengl");
    cfg.modelName = QStringLiteral("Hiyori");

    InstanceSession session(cfg, server, pending);
    QObject::connect(&server, &WsServer::messageReceived,
                     &session, [&session](int, const Envelope& env) {
                         session.onMessage(env);
                     });

    QSignalSpy idleSpy(&session, &InstanceSession::idleMotionTriggered);
    QVERIFY(idleSpy.isValid());

    session.start();
    QVERIFY2(waitForStatus(session, "running", 15000),
             "renderer did not reach status='running' within 15s");
    QElapsedTimer t;
    t.start();
    while (!t.hasExpired(15000) && !session.modelLoaded())
        QTest::qWait(50);
    QVERIFY2(session.modelLoaded(), "model_loaded did not arrive within 15s");

    QTest::qWait(100); // let the scheduler's first tick settle
    const int baseline = idleSpy.count();

    // Inject motion_finished{group:"TapBody"} — non-idle → triggerNow.
    QJsonObject payload;
    payload.insert(QStringLiteral("group"), QStringLiteral("TapBody"));
    payload.insert(QStringLiteral("index"), 0);
    session.onMessage(createEvent(QStringLiteral("motion_finished"), payload));

    QTest::qWait(50);

    QVERIFY2(idleSpy.count() > baseline,
             qPrintable(QStringLiteral("idleMotionTriggered not fired after "
                                       "non-idle motion_finished (before=%1, after=%2)")
                        .arg(baseline).arg(idleSpy.count())));

    // S4: async stop — wait for completion before closing the server.
    QVERIFY2(stopAndWait(session, 7000), "session did not finish stopping");
    server.close();
}

// motion_finished{group:"Idle"} → idle → NO triggerNow → idleMotionTriggered
// count must NOT increase (feedback-loop guard).
void InstanceSessionTest::testMotionFinishedIdleDoesNotTrigger()
{
    const auto path = findRenderer();
    if (!path.has_value())
        QSKIP("renderer binary absent");

    WsServer server;
    QVERIFY2(server.listen(0), "WsServer failed to listen on port 0");
    PendingRequests pending;

    InstanceConfig cfg = defaultInstanceConfig();
    cfg.rendererPath = QStringLiteral(BIN_OUTPUT_DIR);
    cfg.graphicsBackend = QStringLiteral("opengl");
    cfg.modelName = QStringLiteral("Hiyori");
    cfg.idleInterval = 60; // 60s — suppress timer-driven triggers during the test

    InstanceSession session(cfg, server, pending);
    QObject::connect(&server, &WsServer::messageReceived,
                     &session, [&session](int, const Envelope& env) {
                         session.onMessage(env);
                     });

    QSignalSpy idleSpy(&session, &InstanceSession::idleMotionTriggered);
    QVERIFY(idleSpy.isValid());

    session.start();
    QVERIFY2(waitForStatus(session, "running", 15000),
             "renderer did not reach status='running' within 15s");
    QElapsedTimer t;
    t.start();
    while (!t.hasExpired(15000) && !session.modelLoaded())
        QTest::qWait(50);
    QVERIFY2(session.modelLoaded(), "model_loaded did not arrive within 15s");

    QTest::qWait(200); // settle
    const int baseline = idleSpy.count();

    // Inject motion_finished{group:"Idle"} — idle → must NOT triggerNow.
    QJsonObject payload;
    payload.insert(QStringLiteral("group"), QStringLiteral("Idle"));
    payload.insert(QStringLiteral("index"), 0);
    session.onMessage(createEvent(QStringLiteral("motion_finished"), payload));

    QTest::qWait(200);

    QCOMPARE(idleSpy.count(), baseline);

    // S4: async stop — wait for completion before closing the server.
    QVERIFY2(stopAndWait(session, 7000), "session did not finish stopping");
    server.close();
}

// ────────────────────────────────────────────────────────────────────────────
// Phase-5 Wave 8 todo 22 integration tests — layout handlers
// ────────────────────────────────────────────────────────────────────────────

// layout_changed{offset_x, offset_y, scale} → InstanceConfigManager::save.
// Read back via a FRESH InstanceConfigManager pointed at the same tmpDir to
// prove the file on disk has the new layout values.
void InstanceSessionTest::testLayoutChangedPersists()
{
    const auto path = findRenderer();
    if (!path.has_value())
        QSKIP("renderer binary absent");

    WsServer server;
    QVERIFY2(server.listen(0), "WsServer failed to listen on port 0");
    PendingRequests pending;
    QTemporaryDir tmpDir;
    QVERIFY2(tmpDir.isValid(), "QTemporaryDir failed to create");

    InstanceConfig cfg = defaultInstanceConfig();
    cfg.rendererPath = QStringLiteral(BIN_OUTPUT_DIR);
    cfg.graphicsBackend = QStringLiteral("opengl");
    cfg.modelName = QStringLiteral("Hiyori");

    InstanceSession session(cfg, server, pending, tmpDir.path());
    QObject::connect(&server, &WsServer::messageReceived,
                     &session, [&session](int, const Envelope& env) {
                         session.onMessage(env);
                     });

    session.start();
    QVERIFY2(waitForStatus(session, "running", 15000),
             "renderer did not reach status='running' within 15s");
    QElapsedTimer t;
    t.start();
    while (!t.hasExpired(15000) && !session.modelLoaded())
        QTest::qWait(50);
    QVERIFY2(session.modelLoaded(), "model_loaded did not arrive within 15s");

    QJsonObject payload;
    payload.insert(QStringLiteral("offset_x"), 1.5);
    payload.insert(QStringLiteral("offset_y"), 2.5);
    payload.insert(QStringLiteral("scale"), 0.8);
    session.onMessage(createEvent(QStringLiteral("layout_changed"), payload));

    QTest::qWait(50);

    InstanceConfigManager verifier(tmpDir.path());
    const auto reloaded = verifier.load(cfg.id);
    QVERIFY2(reloaded.has_value(), "config file not found after layout_changed");
    QCOMPARE(reloaded->layoutOffsetX, 1.5);
    QCOMPARE(reloaded->layoutOffsetY, 2.5);
    QCOMPARE(reloaded->layoutScale, 0.8);

    // S4: async stop — wait for completion before closing the server.
    QVERIFY2(stopAndWait(session, 7000), "session did not finish stopping");
    server.close();
}

// window_resized{window_width, window_height, window_x, window_y} →
// InstanceConfigManager::save. Read back to prove persistence.
void InstanceSessionTest::testWindowResizedPersists()
{
    const auto path = findRenderer();
    if (!path.has_value())
        QSKIP("renderer binary absent");

    WsServer server;
    QVERIFY2(server.listen(0), "WsServer failed to listen on port 0");
    PendingRequests pending;
    QTemporaryDir tmpDir;
    QVERIFY2(tmpDir.isValid(), "QTemporaryDir failed to create");

    InstanceConfig cfg = defaultInstanceConfig();
    cfg.rendererPath = QStringLiteral(BIN_OUTPUT_DIR);
    cfg.graphicsBackend = QStringLiteral("opengl");
    cfg.modelName = QStringLiteral("Hiyori");

    InstanceSession session(cfg, server, pending, tmpDir.path());
    QObject::connect(&server, &WsServer::messageReceived,
                     &session, [&session](int, const Envelope& env) {
                         session.onMessage(env);
                     });

    session.start();
    QVERIFY2(waitForStatus(session, "running", 15000),
             "renderer did not reach status='running' within 15s");
    QElapsedTimer t;
    t.start();
    while (!t.hasExpired(15000) && !session.modelLoaded())
        QTest::qWait(50);
    QVERIFY2(session.modelLoaded(), "model_loaded did not arrive within 15s");

    QJsonObject payload;
    payload.insert(QStringLiteral("window_width"), 500);
    payload.insert(QStringLiteral("window_height"), 600);
    payload.insert(QStringLiteral("window_x"), 10);
    payload.insert(QStringLiteral("window_y"), 20);
    session.onMessage(createEvent(QStringLiteral("window_resized"), payload));

    QTest::qWait(50);

    InstanceConfigManager verifier(tmpDir.path());
    const auto reloaded = verifier.load(cfg.id);
    QVERIFY2(reloaded.has_value(), "config file not found after window_resized");
    QCOMPARE(reloaded->windowWidth, 500);
    QCOMPARE(reloaded->windowHeight, 600);
    QCOMPARE(reloaded->windowX, 10);
    QCOMPARE(reloaded->windowY, 20);

    // S4: async stop — wait for completion before closing the server.
    QVERIFY2(stopAndWait(session, 7000), "session did not finish stopping");
    server.close();
}

// resetLayout() → commandSent captures "reset_layout" (fire-and-forget: the
// command goes out; no pending request is registered).
void InstanceSessionTest::testResetLayoutSendsCommand()
{
    const auto path = findRenderer();
    if (!path.has_value())
        QSKIP("renderer binary absent");

    WsServer server;
    QVERIFY2(server.listen(0), "WsServer failed to listen on port 0");
    PendingRequests pending;

    InstanceConfig cfg = defaultInstanceConfig();
    cfg.rendererPath = QStringLiteral(BIN_OUTPUT_DIR);
    cfg.graphicsBackend = QStringLiteral("opengl");
    cfg.modelName = QStringLiteral("Hiyori");

    InstanceSession session(cfg, server, pending);
    QObject::connect(&server, &WsServer::messageReceived,
                     &session, [&session](int, const Envelope& env) {
                         session.onMessage(env);
                     });

    QSignalSpy cmdSpy(&session, &InstanceSession::commandSent);
    QVERIFY(cmdSpy.isValid());

    session.start();
    QVERIFY2(waitForStatus(session, "running", 15000),
             "renderer did not reach status='running' within 15s");
    QElapsedTimer t;
    t.start();
    while (!t.hasExpired(15000) && !session.modelLoaded())
        QTest::qWait(50);
    QVERIFY2(session.modelLoaded(), "model_loaded did not arrive within 15s");

    cmdSpy.clear();
    session.resetLayout();
    QTest::qWait(50);

    bool foundReset = false;
    for (int i = 0; i < cmdSpy.count(); ++i) {
        if (cmdSpy.at(i).at(0).toString() == QStringLiteral("reset_layout")) {
            foundReset = true;
            break;
        }
    }
    QVERIFY2(foundReset,
             qPrintable(QStringLiteral("reset_layout not captured (actions=%1)")
                        .arg(cmdSpy.count())));

    // S4: async stop — wait for completion before closing the server.
    QVERIFY2(stopAndWait(session, 7000), "session did not finish stopping");
    server.close();
}

// getLayout() → commandSent captures "get_layout"; the renderer's reply routes
// back as a layout_state EVENT by action (§7.3), so layoutUpdated fires. This
// proves the pseudo-response path: NO pending request is created (sendCommand
// never touches PendingRequests), yet the reply reaches the handler.
void InstanceSessionTest::testGetLayoutRoutesViaLayoutStateEvent()
{
    const auto path = findRenderer();
    if (!path.has_value())
        QSKIP("renderer binary absent");

    WsServer server;
    QVERIFY2(server.listen(0), "WsServer failed to listen on port 0");
    PendingRequests pending;

    InstanceConfig cfg = defaultInstanceConfig();
    cfg.rendererPath = QStringLiteral(BIN_OUTPUT_DIR);
    cfg.graphicsBackend = QStringLiteral("opengl");
    cfg.modelName = QStringLiteral("Hiyori");

    InstanceSession session(cfg, server, pending);
    QObject::connect(&server, &WsServer::messageReceived,
                     &session, [&session](int, const Envelope& env) {
                         session.onMessage(env);
                     });

    QSignalSpy cmdSpy(&session, &InstanceSession::commandSent);
    QSignalSpy layoutSpy(&session, &InstanceSession::layoutUpdated);
    QVERIFY(cmdSpy.isValid());
    QVERIFY(layoutSpy.isValid());

    session.start();
    QVERIFY2(waitForStatus(session, "running", 15000),
             "renderer did not reach status='running' within 15s");
    QElapsedTimer t;
    t.start();
    while (!t.hasExpired(15000) && !session.modelLoaded())
        QTest::qWait(50);
    QVERIFY2(session.modelLoaded(), "model_loaded did not arrive within 15s");

    cmdSpy.clear();
    session.getLayout();

    // The renderer responds via layout_state event over WS — poll for it.
    t.restart();
    while (!t.hasExpired(5000) && layoutSpy.isEmpty())
        QTest::qWait(50);

    bool foundGetLayout = false;
    for (int i = 0; i < cmdSpy.count(); ++i) {
        if (cmdSpy.at(i).at(0).toString() == QStringLiteral("get_layout")) {
            foundGetLayout = true;
            break;
        }
    }
    QVERIFY2(foundGetLayout,
             qPrintable(QStringLiteral("get_layout not captured (actions=%1)")
                        .arg(cmdSpy.count())));

    // The pseudo-response: layout_state event routed by ACTION → layoutUpdated.
    QVERIFY2(!layoutSpy.isEmpty(),
             "layoutUpdated not fired — layout_state event did not route back");

    // S4: async stop — wait for completion before closing the server.
    QVERIFY2(stopAndWait(session, 7000), "session did not finish stopping");
    server.close();
}

// ────────────────────────────────────────────────────────────────────────────
// Helpers
// ────────────────────────────────────────────────────────────────────────────

std::optional<QString> InstanceSessionTest::findRenderer()
{
    const QString binDir = QStringLiteral(BIN_OUTPUT_DIR);
    return core::resolveRendererPath(binDir, QStringLiteral("opengl"));
}

bool InstanceSessionTest::waitForStatus(InstanceSession& session, const char* target,
                                        int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (!t.hasExpired(timeoutMs)) {
        if (session.status() == QLatin1String(target))
            return true;
        QTest::qWait(50);
    }
    return session.status() == QLatin1String(target);
}

bool InstanceSessionTest::stopAndWait(InstanceSession& session, int timeoutMs)
{
    // Spy must exist BEFORE stop() so the queued stopFinished cannot slip by.
    QSignalSpy spy(&session, &InstanceSession::stopFinished);
    session.stop();
    return spy.wait(timeoutMs);
}

void InstanceSessionTest::makeFakeRendererDir(const QTemporaryDir& dir,
                                              QString* outDir)
{
    // A fake renderer DIRECTORY containing a "desktop-pet-renderer" that is
    // really a 30s sleeper script — existence is all resolveRendererPath
    // checks, and a shebang + exec bit make QProcess run it. start() then has
    // a genuinely alive child process (which never speaks WS), exercising the
    // async stop / two-phase machinery headlessly.
    const QString rendererDir =
        QDir(dir.path()).absoluteFilePath(QStringLiteral("renderer"));
    QVERIFY2(QDir().mkpath(rendererDir),
             qPrintable(QStringLiteral("failed to create %1").arg(rendererDir)));
    const QString exe =
        QDir(rendererDir).absoluteFilePath(QStringLiteral("desktop-pet-renderer"));
    QFile f(exe);
    QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Truncate),
             qPrintable(QStringLiteral("failed to create %1").arg(exe)));
    f.write(QStringLiteral("#!/bin/sh\nexec sleep 30\n").toUtf8());
    f.close();
    QVERIFY2(QFile::setPermissions(exe,
                                   QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                       | QFileDevice::ExeOwner
                                       | QFileDevice::ReadGroup
                                       | QFileDevice::ExeGroup
                                       | QFileDevice::ReadOther
                                       | QFileDevice::ExeOther),
             qPrintable(QStringLiteral("failed to chmod %1").arg(exe)));
    *outDir = rendererDir;
}

QTEST_MAIN(InstanceSessionTest)

#include "InstanceSessionTest.moc"
