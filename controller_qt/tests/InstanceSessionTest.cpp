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
#include <QJsonDocument>
#include <QDir>
#include <QFile>
#include <QWebSocket>

#include <optional>

#include "core/DatabaseManager.hpp"
#include "core/InstanceConfig.hpp"
#include "core/InstanceConfigManager.hpp"
#include "core/InstanceSession.hpp"
#include "core/EventBus.hpp"
#include "core/PathResolve.hpp"
#include "core/PluginContextImpl.hpp"
#include "network/Envelope.hpp"
#include "network/PendingRequests.hpp"
#include "network/WsServer.hpp"

namespace {

// ── Headless pack-idle fixtures (Phase 8 todo 20 idle wiring) ────────────────
// Minimal protobuf wire-format serializer for synthesizing a meta.mko (the
// same approach as MetaMkoParserTest — the reader consumes real protobuf
// wire bytes, so the synthesized bundle is byte-equivalent to a real .mko).
// Only the two wire types bundles.proto uses are needed: varint + string.
QByteArray encodeVarint(quint64 value)
{
    QByteArray out;
    while (value >= 0x80) {
        out.append(static_cast<char>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    out.append(static_cast<char>(value));
    return out;
}

QByteArray encodeTag(int fieldNumber, int wireType)
{
    return encodeVarint((static_cast<quint64>(fieldNumber) << 3)
                        | static_cast<quint64>(wireType));
}

QByteArray encodeVarintField(int fieldNumber, quint64 value)
{
    return encodeTag(fieldNumber, 0) + encodeVarint(value);
}

QByteArray encodeStringField(int fieldNumber, const QString& value)
{
    const QByteArray utf8 = value.toUtf8();
    return encodeTag(fieldNumber, 2) + encodeVarint(utf8.size()) + utf8;
}

QByteArray encodeMessageField(int fieldNumber, const QByteArray& message)
{
    return encodeTag(fieldNumber, 2) + encodeVarint(message.size()) + message;
}

// A Bundle whose ONLY group is "idle" (priority omitted → MetaMkoParser's
// default 0) with 2 motion actions + 1 motionless action — mirrors the real
// pack shape the scheduler's pack mode cycles.
QByteArray buildPackIdleOnlyBundle()
{
    QByteArray meta;
    meta += encodeStringField(1, QStringLiteral("Pack Idle"));
    meta += encodeStringField(2, QStringLiteral("pack_idle"));

    QByteArray group;
    group += encodeStringField(1, QStringLiteral("idle"));
    group += encodeStringField(3, QStringLiteral("Idle"));

    QByteArray a1; // motion + fades
    a1 += encodeVarintField(1, 1);
    a1 += encodeStringField(2, QStringLiteral("idle"));
    a1 += encodeStringField(3, QStringLiteral("motions/idle01.motion3.json"));
    a1 += encodeVarintField(7, 250);
    a1 += encodeVarintField(8, 500);

    QByteArray a2;
    a2 += encodeVarintField(1, 2);
    a2 += encodeStringField(2, QStringLiteral("idle"));
    a2 += encodeStringField(3, QStringLiteral("motions/idle02.motion3.json"));

    QByteArray a3; // motionless — excluded from the idle pool
    a3 += encodeVarintField(1, 3);
    a3 += encodeStringField(2, QStringLiteral("idle"));
    a3 += encodeStringField(6, QStringLiteral("no motion"));

    QByteArray bundle;
    bundle += encodeMessageField(1, meta);
    bundle += encodeMessageField(4, group);
    bundle += encodeMessageField(5, a1);
    bundle += encodeMessageField(5, a2);
    bundle += encodeMessageField(5, a3);
    return bundle;
}

// Write the synthesized idle-only meta.mko under <parentDir>/pack/meta.mko.
// Returns the pack directory (the mountVoicePack argument).
QString writeIdleOnlyPack(const QString& parentDir)
{
    const QString packDir = QDir(parentDir).absoluteFilePath(QStringLiteral("pack"));
    if (!QDir().mkpath(packDir))
        return {};
    QFile f(QDir(packDir).absoluteFilePath(QStringLiteral("meta.mko")));
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write(buildPackIdleOnlyBundle());
    f.close();
    return packDir;
}

// Write a minimal <rendererDir>/Resources/Models/<modelName>/<modelName>
// .model3.json (the path shape resolveModel3JsonPath expects). `json` is the
// full file body — "{}"-ish for a motionless model, or one with Motions.
bool writeModel3Json(const QString& rendererDir, const QString& modelName,
                     const QByteArray& json)
{
    const QString dir = QDir(rendererDir).absoluteFilePath(
        QStringLiteral("Resources/Models/%1").arg(modelName));
    if (!QDir().mkpath(dir))
        return false;
    QFile f(QDir(dir).absoluteFilePath(
        QStringLiteral("%1.model3.json").arg(modelName)));
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(json);
    f.close();
    return f.error() == QFile::NoError;
}

// Send an event envelope from the fake-renderer socket (server routes it to
// messageReceived → the test's session.onMessage wiring).
void sendEventFromClient(QWebSocket& client, const Envelope& env)
{
    client.sendTextMessage(QString::fromUtf8(
        serialize(env).toJson(QJsonDocument::Compact)));
}

// Parse a captured textMessageReceived string into its JSON object.
QJsonObject parseWireJson(const QString& message)
{
    return QJsonDocument::fromJson(message.toUtf8()).object();
}

} // namespace


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
    // Silent-failure fixes: connected switches must actually dispatch
    // load_model, and a rejected switch must roll the persisted config
    // back — both headless (fake exe + synthetic ready/model_load_failed).
    void testConnectedLoadModelSendsCommand();
    void testModelLoadFailedRollsBackPersistedModel();
    // Silent-persistence-failure fix (exp-4 root cause #2): the backend
    // selector must REPORT persist failures (bool) and write through the
    // injectable shared DatabaseManager — all headless.
    void testSetGraphicsBackendPersists();
    void testSetGraphicsBackendPersistFailureReported();
    void testSetGraphicsBackendSharedDatabase();

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

    // ── Phase-8 todo 20 idle wiring (headless sleeper + fake-renderer WS) ──
    void testPackIdleSchedulerHeadless();
    void testIdleSchedulerModelModeLowercaseGroup();

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
// Silent-failure fixes: connected loadModel dispatch + durable rollback
// ────────────────────────────────────────────────────────────────────────────

// Headless shared harness: a fake renderer EXE satisfies path resolution so
// start() registers the event handlers, then a synthetic `ready` event is
// dispatched DIRECTLY via onMessage (no real socket needed) — the ready
// override flips status/connected and fires the salvo synchronously, giving
// the test a CONNECTED session without launching anything.
namespace {

bool makeFakeRendererExe(const QTemporaryDir& dir)
{
#ifdef Q_OS_WIN
    const QString exeName = QStringLiteral("desktop-pet-renderer.exe");
#else
    const QString exeName = QStringLiteral("desktop-pet-renderer");
#endif
    QFile fakeExe(QDir(dir.path()).absoluteFilePath(exeName));
    if (!fakeExe.open(QIODevice::WriteOnly))
        return false;
    fakeExe.close();
    return true;
}

Envelope syntheticEvent(const QString& action)
{
    Envelope env;
    env.type = QStringLiteral("event");
    env.action = action;
    return env;
}

} // namespace

// A CONNECTED session's loadModel must dispatch the load_model command
// (observed on the unified commandSent stream — the same observability the
// salvo test uses; the WsServer has no client, so sendText WARN-no-ops —
// that is fine, the envelope dispatch is what this pins down) AND persist
// the choice. Regression guard for the "applied but nothing happened"
// symptom class.
void InstanceSessionTest::testConnectedLoadModelSendsCommand()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");
    QVERIFY2(makeFakeRendererExe(dir), "precondition: fake renderer exe");

    WsServer server;
    QVERIFY2(server.listen(0), "WsServer failed to listen on port 0");
    PendingRequests pending;

    InstanceConfig cfg = defaultInstanceConfig();
    cfg.rendererPath = dir.path();
    cfg.graphicsBackend = QStringLiteral("opengl");

    InstanceSession session(cfg, server, pending, dir.path());
    session.start();

    // Synthetic ready → connected (salvo fires synchronously inside).
    session.onMessage(syntheticEvent(QStringLiteral("ready")));
    QVERIFY2(session.connected(), "ready event must flip connected");

    QSignalSpy cmdSpy(&session, &InstanceSession::commandSent);
    QSignalSpy nameSpy(&session, &InstanceSession::modelNameChanged);
    QVERIFY(cmdSpy.isValid());

    session.loadModel(QStringLiteral("Haru"));

    // Exactly one outbound command: load_model (the switch command itself).
    QCOMPARE(cmdSpy.count(), 1);
    QCOMPARE(cmdSpy.at(0).at(0).toString(), QStringLiteral("load_model"));
    QCOMPARE(session.modelName(), QStringLiteral("Haru"));
    QCOMPARE(nameSpy.count(), 1);

    // And it persisted (online switches also survive a restart).
    InstanceConfigManager persisted(dir.path());
    const auto reloaded = persisted.load(cfg.id);
    QVERIFY2(reloaded.has_value(), "config row missing after connected loadModel");
    QCOMPARE(reloaded->modelName, QStringLiteral("Haru"));

    QVERIFY2(stopAndWait(session, 3000), "session did not finish stopping");
    server.close();
}

// model_load_failed after a user switch must roll m_config.modelName back
// to the PRE-switch value AND re-persist it (loadModel persisted the failed
// name; without the rollback every later launch retries the broken model).
// Also pins the ordering the QML failure copy depends on (failed name still
// current at modelLoadFailed-emit time) and the startup-failure guard (a
// model_load_failed with NO switch in flight must not touch the config).
void InstanceSessionTest::testModelLoadFailedRollsBackPersistedModel()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");
    QVERIFY2(makeFakeRendererExe(dir), "precondition: fake renderer exe");

    WsServer server;
    QVERIFY2(server.listen(0), "WsServer failed to listen on port 0");
    PendingRequests pending;

    InstanceConfig cfg = defaultInstanceConfig();
    cfg.rendererPath = dir.path();
    cfg.graphicsBackend = QStringLiteral("opengl");
    QVERIFY(cfg.modelName != QStringLiteral("Haru")); // precondition: a model to roll back TO

    InstanceSession session(cfg, server, pending, dir.path());
    session.start();
    session.onMessage(syntheticEvent(QStringLiteral("ready")));
    QVERIFY(session.connected());

    // Capture modelName AT modelLoadFailed-emit time (QML reads it in its
    // signal handlers to name the failed model in the dialog copy).
    QString nameAtSignal;
    QObject::connect(&session, &InstanceSession::modelLoadFailed,
                     &session, [&nameAtSignal, &session](const QString&) {
                         nameAtSignal = session.modelName();
                     });
    QSignalSpy nameSpy(&session, &InstanceSession::modelNameChanged);
    QSignalSpy failedSpy(&session, &InstanceSession::modelLoadFailed);
    QVERIFY(nameSpy.isValid());
    QVERIFY(failedSpy.isValid());

    // The doomed switch: Hiyori → Haru (persisted), renderer rejects.
    session.loadModel(QStringLiteral("Haru"));
    QCOMPARE(session.modelName(), QStringLiteral("Haru"));
    QCOMPARE(nameSpy.count(), 1);

    Envelope failEnv = syntheticEvent(QStringLiteral("model_load_failed"));
    failEnv.payload.insert(QStringLiteral("error_code"), 1001);
    failEnv.payload.insert(QStringLiteral("error_message"),
                           QStringLiteral("Model path not found: Haru"));
    session.onMessage(failEnv);

    // Failure surfaced with the FAILED name still current at emit time...
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(nameAtSignal, QStringLiteral("Haru"));
    // ...then the rollback flipped it back and notified QML.
    QCOMPARE(session.modelName(), cfg.modelName);
    QCOMPARE(session.config().modelName, cfg.modelName);
    QCOMPARE(nameSpy.count(), 2);

    // The rollback hit disk — a fresh manager over the same dir reads the
    // pre-switch model, not the failed one.
    InstanceConfigManager persisted(dir.path());
    const auto reloaded = persisted.load(cfg.id);
    QVERIFY2(reloaded.has_value(), "config row missing after rollback");
    QCOMPARE(reloaded->modelName, cfg.modelName);

    // Startup-failure guard: a model_load_failed with NO switch in flight
    // (e.g. the salvo's load of the configured model) leaves the config
    // untouched — the configured model is the user's informed choice.
    session.onMessage(failEnv);
    QCOMPARE(failedSpy.count(), 2);
    QCOMPARE(session.modelName(), cfg.modelName);
    QCOMPARE(nameSpy.count(), 2); // no extra modelNameChanged

    QVERIFY2(stopAndWait(session, 3000), "session did not finish stopping");
    server.close();
}

// ────────────────────────────────────────────────────────────────────────────
// Silent-persistence-failure fix (exp-4 root cause #2) — graphics backend
// ────────────────────────────────────────────────────────────────────────────

// Happy path: setGraphicsBackend returns true, flips the live property +
// NOTIFY, and the choice PERSISTS (a fresh InstanceConfigManager over the
// same config dir reads it back). The same-value call stays an accepted
// no-op (true, no re-save, no second NOTIFY).
void InstanceSessionTest::testSetGraphicsBackendPersists()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");

    WsServer server;
    PendingRequests pending;
    InstanceConfig cfg = defaultInstanceConfig();
    QCOMPARE(cfg.graphicsBackend, QStringLiteral("opengl")); // precondition: the default

    InstanceSession session(cfg, server, pending, dir.path());

    QSignalSpy gfxSpy(&session, &InstanceSession::graphicsBackendChanged);
    QVERIFY(gfxSpy.isValid());

    QVERIFY2(session.setGraphicsBackend(QStringLiteral("vulkan")),
             "happy-path setGraphicsBackend must report success");
    QCOMPARE(session.graphicsBackend(), QStringLiteral("vulkan"));
    QCOMPARE(session.config().graphicsBackend, QStringLiteral("vulkan"));
    QCOMPARE(gfxSpy.count(), 1);

    // Persisted: a fresh manager over the same dir reads the choice back.
    InstanceConfigManager persisted(dir.path());
    const auto reloaded = persisted.load(cfg.id);
    QVERIFY2(reloaded.has_value(), "config row missing after setGraphicsBackend");
    QCOMPARE(reloaded->graphicsBackend, QStringLiteral("vulkan"));

    // Same-value no-op: accepted (true — nothing to persist), no NOTIFY.
    QVERIFY(session.setGraphicsBackend(QStringLiteral("vulkan")));
    QCOMPARE(gfxSpy.count(), 1);
}

// Failure path: a config dir whose app.db CANNOT be opened (configBasePath
// points at a regular FILE — the lazy open's mkpath fails) → save() false.
// The failure must be REPORTED (false), and all-or-nothing semantics keep
// BOTH the live snapshot and the NOTIFY on the old backend — the QML combo
// bounce and the on-disk row agree. The same-value no-op still returns
// true even on this broken dir (nothing to persist).
void InstanceSessionTest::testSetGraphicsBackendPersistFailureReported()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");

    // configBasePath = a FILE: <basePath>/app.db sits under a
    // non-directory → DatabaseManager::open's mkpath fails → the owned
    // manager latches the failed open → every save() returns false.
    const QString notADirPath =
        QDir(dir.path()).absoluteFilePath(QStringLiteral("not-a-dir"));
    QFile notADir(notADirPath);
    QVERIFY2(notADir.open(QIODevice::WriteOnly),
             "precondition: failed to block the db path");
    notADir.close();

    WsServer server;
    PendingRequests pending;
    InstanceConfig cfg = defaultInstanceConfig();

    InstanceSession session(cfg, server, pending, notADirPath);

    QSignalSpy gfxSpy(&session, &InstanceSession::graphicsBackendChanged);
    QVERIFY(gfxSpy.isValid());

    QVERIFY2(!session.setGraphicsBackend(QStringLiteral("vulkan")),
             "persist failure must be reported as false (was silent before)");
    // All-or-nothing: the live snapshot stays on the old backend.
    QCOMPARE(session.graphicsBackend(), QStringLiteral("opengl"));
    QCOMPARE(session.config().graphicsBackend, QStringLiteral("opengl"));
    QCOMPARE(gfxSpy.count(), 0);

    // Same-value call on the broken dir still reports true (no-op).
    QVERIFY(session.setGraphicsBackend(QStringLiteral("opengl")));
    QCOMPARE(gfxSpy.count(), 0);
}

// Shared-db injection: a session with the panel's ONE shared
// DatabaseManager injected (the InstanceManager::wireSession production
// path) writes through it — a SEPARATE InstanceConfigManager bound to the
// same db reads the write back. Production sessions therefore share one
// connection / one failure domain instead of each opening (and latching a
// failed) private connection to the same app.db.
void InstanceSessionTest::testSetGraphicsBackendSharedDatabase()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");

    WsServer server;
    PendingRequests pending;

    // Declared BEFORE the session so destruction order keeps the raw
    // injected pointer valid for the session's whole lifetime.
    DatabaseManager db;
    QVERIFY2(db.open(QDir(dir.path()).absoluteFilePath(QStringLiteral("app.db"))),
             "precondition: shared db failed to open");

    InstanceConfig cfg = defaultInstanceConfig();
    InstanceSession session(cfg, server, pending, dir.path());
    session.setConfigDatabase(&db); // the wireSession injection (m_db path)

    QVERIFY2(session.setGraphicsBackend(QStringLiteral("vulkan")),
             "shared-db setGraphicsBackend must report success");
    QCOMPARE(session.graphicsBackend(), QStringLiteral("vulkan"));

    // Visible through a separate manager bound to the SAME shared db —
    // the session never opened a private connection.
    InstanceConfigManager sameDb;
    sameDb.setDatabase(&db);
    const auto reloaded = sameDb.load(cfg.id);
    QVERIFY2(reloaded.has_value(), "config row missing via the shared db");
    QCOMPARE(reloaded->graphicsBackend, QStringLiteral("vulkan"));
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

// hit → (S8b) the bus adapters → InteractionHandler → play_motion. The hit
// decision chain now runs on the event bus, so the test injects a REAL
// EventBus + shared EventApiImpl (the PanelApplication wiring shape) via
// setEventPublisher; the commandSent signal must still capture a play_motion
// action after the synthetic hit event is injected.
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

    // S8b: real bus (must outlive the session) + the shared events impl —
    // the hit adapters subscribe on injection and carry the decision chain.
    core::EventBus bus;
    core::EventApiImpl eventApi(&bus);

    InstanceSession session(cfg, server, pending);
    session.setEventPublisher(&eventApi);
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
// Phase-8 todo 20 idle wiring — headless pack-idle scheduling tests
// ────────────────────────────────────────────────────────────────────────────

// Pack idle mode end-to-end (headless): a motionless model + a mounted pack
// with an "idle" group → the scheduler runs in PACK mode. The fake-renderer
// socket (a QWebSocket handshaked with a test-registered token) both receives
// the play_motion_ext commands (full JSON → priority assertions) and injects
// motion_finished events. Locks:
//   - motionless model alone → scheduler NEVER starts (non-idle
//     motion_finished is a no-op — the frozen-on-last-frame bug shape);
//   - mountVoicePack (re)starts the scheduler in pack mode;
//   - non-idle behavior motion_finished → triggerNow → immediate pack idle
//     play_motion_ext with priority=1 over the pack's motion paths;
//   - idle motion_finished (an echoed pack idle path) → NO triggerNow.
void InstanceSessionTest::testPackIdleSchedulerHeadless()
{
#ifdef Q_OS_WIN
    QSKIP("fake-renderer sleeper harness is POSIX-only");
#else
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");

    WsServer server;
    QVERIFY2(server.listen(0), "WsServer failed to listen on port 0");
    PendingRequests pending;

    QString rendererDir;
    makeFakeRendererDir(dir, &rendererDir); // fake renderer = sleeper
    // Motionless model (the giwa-idol2023 shape: no Motions at all).
    QVERIFY2(writeModel3Json(rendererDir, QStringLiteral("PackIdle"),
                             QByteArrayLiteral("{\"Version\":3,\"FileReferences\":{},\"HitAreas\":[]}")),
             "precondition: failed to write synthetic model3.json");
    const QString packDir = writeIdleOnlyPack(dir.path());

    InstanceConfig cfg = defaultInstanceConfig();
    cfg.rendererPath = rendererDir;
    cfg.graphicsBackend = QStringLiteral("opengl");
    cfg.modelName = QStringLiteral("PackIdle");
    cfg.idleInterval = 60; // suppress timer ticks — ticks are driven via triggerNow

    InstanceSession session(cfg, server, pending, dir.path());
    QObject::connect(&server, &WsServer::messageReceived,
                     &session, [&session](int, const Envelope& env) {
                         session.onMessage(env);
                     });

    session.start();
    session.setStopTimeoutMs(300); // fast kill (test seam) — sleeper never exits gracefully

    // model_loaded with a motionless model → NO idle source → scheduler
    // stays stopped (previously a WARN + never started, the root cause).
    session.onMessage(createEvent(QStringLiteral("model_loaded")));
    QVERIFY(session.modelLoaded());

    // Fake-renderer socket: override the session's random token with a known
    // one (WsServer::registerToken replaces), then handshake as this instance.
    const QString knownToken = QStringLiteral("cafef00d");
    server.registerToken(session.instanceId(), knownToken);
    QWebSocket client;
    QSignalSpy msgSpy(&client, &QWebSocket::textMessageReceived);
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1/?instance_id=%2&token=%3")
                         .arg(server.serverPort())
                         .arg(session.instanceId())
                         .arg(knownToken)));
    QElapsedTimer t;
    t.start();
    while (!t.hasExpired(5000) && !session.connected())
        QTest::qWait(25);
    QVERIFY2(session.connected(), "fake renderer socket did not connect");

    // BEFORE mount: a non-idle behavior motion_finished must be a NO-OP —
    // the scheduler never started for the motionless model (triggerNow's
    // !m_running guard). Nothing may reach the renderer socket.
    QJsonObject behaviorFinished;
    behaviorFinished.insert(
        QStringLiteral("motion_path"),
        QDir(packDir).absoluteFilePath(QStringLiteral("motions/behavior01.motion3.json")));
    sendEventFromClient(client, createEvent(QStringLiteral("motion_finished"),
                                            behaviorFinished));
    QVERIFY2(!msgSpy.wait(500),
             "scheduler fired before any idle source existed");

    // Mount the pack with the "idle" group → scheduler restarts in pack mode.
    QVERIFY2(session.mountVoicePack(packDir), "mountVoicePack failed");

    // Same non-idle behavior motion_finished → triggerNow fires an immediate
    // pack idle tick: a play_motion_ext with priority=1 (Idle tier) over one
    // of the pack's two motion paths.
    sendEventFromClient(client, createEvent(QStringLiteral("motion_finished"),
                                            behaviorFinished));
    QVERIFY2(msgSpy.wait(3000),
             "no play_motion_ext after non-idle motion_finished (pack mode)");
    const QJsonObject cmd1 = parseWireJson(msgSpy.takeFirst().at(0).toString());
    QCOMPARE(cmd1.value(QStringLiteral("action")).toString(),
             QStringLiteral("play_motion_ext"));
    QCOMPARE(cmd1.value(QStringLiteral("payload"))
                 .toObject()
                 .value(QStringLiteral("priority"))
                 .toInt(), 1);
    const QString idlePath = cmd1.value(QStringLiteral("payload"))
                                 .toObject()
                                 .value(QStringLiteral("motion_path"))
                                 .toString();
    QVERIFY2(idlePath.endsWith(QStringLiteral("motions/idle01.motion3.json"))
                 || idlePath.endsWith(QStringLiteral("motions/idle02.motion3.json")),
             qPrintable(QStringLiteral("unexpected idle motion_path: %1").arg(idlePath)));

    // Idle motion_finished (echo the pack idle path the renderer just
    // "played") → NO triggerNow — the feedback-loop guard via
    // MountedBehaviorEngine::isIdleMotionPath.
    QJsonObject idleFinished;
    idleFinished.insert(QStringLiteral("motion_path"), idlePath);
    sendEventFromClient(client, createEvent(QStringLiteral("motion_finished"),
                                            idleFinished));
    QVERIFY2(!msgSpy.wait(800),
             "idle motion_finished must not trigger another idle motion");

    client.close();
    QVERIFY2(stopAndWait(session, 5000), "session did not finish stopping");
    server.close();
#endif
}

// Model mode with a LOWERCASE "idle" group: the two-tier Idle resolution
// (exact "Idle" → case-insensitive scan) must pass the RESOLVED group name
// to the scheduler — fireOnce's map lookup is exact, so passing the literal
// "Idle" would never tick an "idle"-keyed model. Headless: the sleeper
// renderer never speaks, we only assert idleMotionTriggered carries the
// model's original lowercase key and that a play_motion went out.
void InstanceSessionTest::testIdleSchedulerModelModeLowercaseGroup()
{
#ifdef Q_OS_WIN
    QSKIP("fake-renderer sleeper harness is POSIX-only");
#else
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");

    WsServer server;
    QVERIFY2(server.listen(0), "WsServer failed to listen on port 0");
    PendingRequests pending;

    QString rendererDir;
    makeFakeRendererDir(dir, &rendererDir);
    QVERIFY2(writeModel3Json(
                 rendererDir, QStringLiteral("LowerIdle"),
                 QByteArrayLiteral("{\"Version\":3,\"FileReferences\":{\"Motions\":"
                                   "{\"idle\":[{\"File\":\"i1.motion3.json\"},"
                                   "{\"File\":\"i2.motion3.json\"}],"
                                   "\"TapBody\":[{\"File\":\"t1.motion3.json\"}]}}}")),
             "precondition: failed to write synthetic model3.json");

    InstanceConfig cfg = defaultInstanceConfig();
    cfg.rendererPath = rendererDir;
    cfg.graphicsBackend = QStringLiteral("opengl");
    cfg.modelName = QStringLiteral("LowerIdle");
    cfg.idleInterval = 1; // 1s ticks

    InstanceSession session(cfg, server, pending, dir.path());
    QSignalSpy idleSpy(&session, &InstanceSession::idleMotionTriggered);
    QSignalSpy cmdSpy(&session, &InstanceSession::commandSent);
    QVERIFY(idleSpy.isValid());
    QVERIFY(cmdSpy.isValid());

    session.start();
    session.setStopTimeoutMs(300); // fast kill (test seam) — sleeper never exits gracefully
    session.onMessage(createEvent(QStringLiteral("model_loaded")));
    QVERIFY(session.modelLoaded());

    // The scheduler must tick with the RESOLVED lowercase group name.
    QElapsedTimer t;
    t.start();
    while (!t.hasExpired(5000) && idleSpy.isEmpty())
        QTest::qWait(50);
    QVERIFY2(!idleSpy.isEmpty(),
             "scheduler never ticked for the lowercase 'idle' group model");
    QCOMPARE(idleSpy.at(0).at(0).toString(), QStringLiteral("idle"));

    // …and each tick sent a play_motion over that same resolved group.
    bool foundPlayMotion = false;
    for (int i = 0; i < cmdSpy.count(); ++i) {
        if (cmdSpy.at(i).at(0).toString() == QStringLiteral("play_motion")) {
            foundPlayMotion = true;
            break;
        }
    }
    QVERIFY2(foundPlayMotion, "no play_motion captured for the resolved idle group");

    QVERIFY2(stopAndWait(session, 5000), "session did not finish stopping");
    server.close();
#endif
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
