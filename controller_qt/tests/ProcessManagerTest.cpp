// ProcessManagerTest (task T13) — QProcess wrapper for renderer lifecycle.
//
// Two test tiers:
//
//   Unit tier (no renderer needed):
//     - testInitialState: before start → not running, pid 0, empty args
//     - testCliArgsConstruction: required args present, optional omitted
//     - testOptionalArgsIncluded: x/y/width/height added when >= 0
//
//   Integration tier [REQUIRES_RENDERER]:
//     - testRealRendererLifecycle: start → ready (10s) → stdout → shutdown
//     - testCrashDetection: start → ready → kill() → exited(crashed=true)
//
// QTEST_MAIN (NOT APPLESS): QProcess + QSignalSpy::wait need the event loop.
// Integration tests launch the REAL desktop-pet-renderer.exe which opens a GLFW
// window — requires a real display, hence the REQUIRES_RENDERER ctest label.

#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QString>
#include <QTest>
#include <QElapsedTimer>

#include <optional>

#include "core/PathResolve.hpp"
#include "core/ProcessManager.hpp"
#include "network/Envelope.hpp"
#include "network/Protocol.hpp"
#include "network/WsServer.hpp"

Q_DECLARE_METATYPE(Envelope)

class ProcessManagerTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();

    // ── Unit tier (no renderer) ────────────────────────────────────────────
    void testInitialState();
    void testCliArgsConstruction();
    void testOptionalArgsIncluded();

    // ── Integration tier [REQUIRES_RENDERER] ───────────────────────────────
    void testRealRendererLifecycle();
    void testCrashDetection();
    void testGracefulShutdown();
    void testManuallyStoppingFlag();

private:
    // Resolve the renderer exe via PathResolve (T14). Returns nullopt if the
    // binary is absent (caller QSKIPs). Uses BIN_OUTPUT_DIR baked at compile
    // time — defaultRendererDir() would resolve wrong because the test exe
    // lives in build/controller_qt/tests/, not build/bin/.
    static std::optional<QString> findRenderer();

    // Wait until msgSpy captures an Envelope whose action matches. Polls via
    // QTest::qWait (event-loop pump). Returns true on match, false on timeout.
    static bool waitForAction(QSignalSpy& spy, const char* action, int timeoutMs);

    // Send a `shutdown` command envelope through the server.
    static void sendShutdown(WsServer& server);
};

void ProcessManagerTest::initTestCase()
{
    qRegisterMetaType<Envelope>();
}

// ────────────────────────────────────────────────────────────────────────────
// Unit tier
// ────────────────────────────────────────────────────────────────────────────

// A freshly constructed ProcessManager reports not-running, pid 0, no args.
void ProcessManagerTest::testInitialState()
{
    ProcessManager pm;
    QVERIFY(!pm.isRunning());
    QCOMPARE(pm.processId(), qint64(0));
    QVERIFY(pm.arguments().isEmpty());
}

// startRenderer with a dummy (non-existent) path still populates arguments()
// synchronously — QProcess::start sets program+args before the async launch
// fails. The FailedToStart error is silently ignored (ProcessManager does not
// connect errorOccurred), so no spurious exited fires.
void ProcessManagerTest::testCliArgsConstruction()
{
    ProcessManager pm;
    pm.startRenderer(QStringLiteral("/nonexistent/dummy-renderer"),
                     9001, 0,
                     QStringLiteral("deadbeef"),
                     QStringLiteral("Hiyori"));

    const QStringList args = pm.arguments();
    // Required args (blueprint §3.4).
    QVERIFY(args.contains(QStringLiteral("--port")));
    QVERIFY(args.contains(QStringLiteral("9001")));
    QVERIFY(args.contains(QStringLiteral("--instance-id")));
    QVERIFY(args.contains(QStringLiteral("0")));
    QVERIFY(args.contains(QStringLiteral("--token")));
    QVERIFY(args.contains(QStringLiteral("deadbeef")));
    QVERIFY(args.contains(QStringLiteral("--model")));
    QVERIFY(args.contains(QStringLiteral("Hiyori")));
    // Optional args omitted when defaults (-1) are used.
    QVERIFY(!args.contains(QStringLiteral("--x")));
    QVERIFY(!args.contains(QStringLiteral("--y")));
    QVERIFY(!args.contains(QStringLiteral("--width")));
    QVERIFY(!args.contains(QStringLiteral("--height")));
}

// Passing x/y/width/height >= 0 adds them to the args list.
void ProcessManagerTest::testOptionalArgsIncluded()
{
    ProcessManager pm;
    pm.startRenderer(QStringLiteral("/nonexistent/dummy-renderer"),
                     9001, 0,
                     QStringLiteral("cafebabe"),
                     QStringLiteral("Hiyori"),
                     100, 200, 300, 400);

    const QStringList args = pm.arguments();
    QCOMPARE(args.filter(QStringLiteral("--x")).size(), 1);
    QVERIFY(args.contains(QStringLiteral("100")));
    QCOMPARE(args.filter(QStringLiteral("--y")).size(), 1);
    QVERIFY(args.contains(QStringLiteral("200")));
    QCOMPARE(args.filter(QStringLiteral("--width")).size(), 1);
    QVERIFY(args.contains(QStringLiteral("300")));
    QCOMPARE(args.filter(QStringLiteral("--height")).size(), 1);
    QVERIFY(args.contains(QStringLiteral("400")));
}

// ────────────────────────────────────────────────────────────────────────────
// Integration tier [REQUIRES_RENDERER]
// ────────────────────────────────────────────────────────────────────────────

// Full lifecycle: start renderer → receive `ready` via WsServer within 10s →
// stdout lines captured → send shutdown → process exits. The renderer has a
// known teardown crash (0xC0000005) that fires AFTER the WS connection closes
// (T6 PoC finding), so the exit assertion tolerates either a clean exit (0,
// false) or a teardown crash (non-zero, crashed=true). The proof of life is
// that `ready` arrived and stdout was pumped — that means ProcessManager
// correctly launched the renderer with the right CLI args + working dir.
void ProcessManagerTest::testRealRendererLifecycle()
{
    const auto path = findRenderer();
    if (!path.has_value())
        QSKIP("renderer binary absent");

    WsServer server;
    QVERIFY2(server.listen(0), "WsServer failed to listen on port 0");
    server.registerToken(0, QStringLiteral("lifecycletoken"));
    const quint16 port = server.serverPort();

    ProcessManager pm;
    QSignalSpy msgSpy(&server, &WsServer::messageReceived);
    QSignalSpy stdoutSpy(&pm, &ProcessManager::stdoutLine);
    QSignalSpy exitedSpy(&pm, &ProcessManager::exited);

    pm.startRenderer(*path, port, 0, QStringLiteral("lifecycletoken"),
                     QStringLiteral("Hiyori"));

    // ready must arrive within 10s (handshake.md §4).
    QVERIFY2(waitForAction(msgSpy, "ready", 10000),
             "renderer did not send 'ready' within 10s");

    // stdout lines must appear (renderer prints startup logs via LAppPal).
    // Give buffered stdout a moment to flush after ready.
    if (stdoutSpy.isEmpty())
        QVERIFY2(stdoutSpy.wait(5000), "no stdout lines within 5s after ready");
    QVERIFY2(stdoutSpy.count() > 0,
             qPrintable(QString("expected stdout lines, got %1")
                            .arg(stdoutSpy.count())));

    // Graceful shutdown: send the shutdown command, wait for exit.
    sendShutdown(server);
    QVERIFY2(exitedSpy.wait(10000),
             "renderer did not exit within 10s after shutdown");

    QVERIFY(exitedSpy.count() >= 1);
    const int exitCode = exitedSpy.at(0).at(0).toInt();
    const bool crashed = exitedSpy.at(0).at(1).toBool();
    // Tolerate the known teardown crash: either clean (0, false) or crash
    // (non-zero, true). A non-zero non-crash would be a real anomaly.
    QVERIFY2(exitCode == 0 || crashed,
             qPrintable(QString("unexpected exit: code=%1 crashed=%2")
                            .arg(exitCode).arg(crashed)));

    server.close();
}

// Crash detection: start renderer → wait for ready → kill() → exited must
// report crashed=true within 2s. kill() calls QProcess::kill() (SIGKILL on
// Unix, TerminateProcess on Windows), which produces QProcess::CrashExit.
void ProcessManagerTest::testCrashDetection()
{
    const auto path = findRenderer();
    if (!path.has_value())
        QSKIP("renderer binary absent");

    WsServer server;
    QVERIFY2(server.listen(0), "WsServer failed to listen on port 0");
    server.registerToken(0, QStringLiteral("crashtoken"));
    const quint16 port = server.serverPort();

    ProcessManager pm;
    QSignalSpy msgSpy(&server, &WsServer::messageReceived);
    QSignalSpy exitedSpy(&pm, &ProcessManager::exited);

    pm.startRenderer(*path, port, 0, QStringLiteral("crashtoken"),
                     QStringLiteral("Hiyori"));

    // Wait for ready so we know the renderer is fully up before killing.
    QVERIFY2(waitForAction(msgSpy, "ready", 10000),
             "renderer did not send 'ready' within 10s");
    QVERIFY(exitedSpy.isEmpty()); // sanity: not exited yet

    pm.kill();

    // exited(crashed=true) must arrive within 2s (task spec).
    QVERIFY2(exitedSpy.wait(2000),
             "exited signal not emitted within 2s after kill()");
    QVERIFY(exitedSpy.count() >= 1);
    const bool crashed = exitedSpy.at(0).at(1).toBool();
    QVERIFY2(crashed, "exited must report crashed=true after kill()");

    server.close();
}

// ────────────────────────────────────────────────────────────────────────────
// Helpers
// ────────────────────────────────────────────────────────────────────────────

// 3-stage graceful shutdown (T15, blueprint §4.6.4): wire a shutdown sender
// via setShutdownSender, call stop(), assert clean exit within 5s. Exercises
// the full send-shutdown → poll-waitForFinished → exit path. The renderer's
// known teardown crash (0xC0000005) fires AFTER the WS connection closes —
// because m_manuallyStopping=true, onFinished reports crashed=false.
void ProcessManagerTest::testGracefulShutdown()
{
    const auto path = findRenderer();
    if (!path.has_value())
        QSKIP("renderer binary absent");

    WsServer server;
    QVERIFY2(server.listen(0), "WsServer failed to listen on port 0");
    server.registerToken(0, QStringLiteral("gracefultoken"));
    const quint16 port = server.serverPort();

    ProcessManager pm;
    QSignalSpy msgSpy(&server, &WsServer::messageReceived);
    QSignalSpy exitedSpy(&pm, &ProcessManager::exited);

    // Wire the shutdown sender exactly as the app will (T15 context):
    // serialize(Protocol::buildShutdown()) → sendText.
    pm.setShutdownSender([&server]() {
        const QByteArray json =
            serialize(Protocol::buildShutdown()).toJson(QJsonDocument::Compact);
        server.sendText(0, QString::fromUtf8(json));
    });

    pm.startRenderer(*path, port, 0, QStringLiteral("gracefultoken"),
                     QStringLiteral("Hiyori"));

    QVERIFY2(waitForAction(msgSpy, "ready", 10000),
             "renderer did not send 'ready' within 10s");

    // stop() is blocking — sends shutdown, polls waitForFinished up to 5s.
    const bool clean = pm.stop();
    QVERIFY2(clean, "stop() should return true (renderer exited within 5s)");

    QVERIFY2(exitedSpy.count() >= 1, "exited signal not emitted by stop()");
    const bool crashed = exitedSpy.at(0).at(1).toBool();
    // manuallyStopping was true → crashed must be false even if the renderer
    // hit its known teardown access-violation during exit.
    QCOMPARE(crashed, false);

    // No residual process.
    QVERIFY2(!pm.isRunning(), "renderer process still running after stop()");

    server.close();
}

// manuallyStopping flag observability (T15 m5): the exited() callback must be
// able to read isManuallyStopping() to distinguish user-stop from crash.
//   Scenario A: stop() → flag is true when exited fires.
//   Scenario B: kill() (external) → flag is false, crashed=true.
void ProcessManagerTest::testManuallyStoppingFlag()
{
    const auto path = findRenderer();
    if (!path.has_value())
        QSKIP("renderer binary absent");

    // ── Scenario A: stop() sets the flag before exited fires ──────────────
    {
        WsServer server;
        QVERIFY2(server.listen(0), "WsServer failed to listen on port 0");
        server.registerToken(0, QStringLiteral("flagtokenA"));
        const quint16 port = server.serverPort();

        ProcessManager pm;
        QSignalSpy msgSpy(&server, &WsServer::messageReceived);

        // Capture the flag value at the exact moment exited() fires. The
        // lambda runs synchronously inside onFinished's emit, BEFORE the flag
        // is cleared at the end of onFinished.
        bool flagDuringExit = false;
        connect(&pm, &ProcessManager::exited, &pm,
                [&pm, &flagDuringExit](int, bool) {
                    flagDuringExit = pm.isManuallyStopping();
                });

        pm.setShutdownSender([&server]() {
            const QByteArray json =
                serialize(Protocol::buildShutdown()).toJson(QJsonDocument::Compact);
            server.sendText(0, QString::fromUtf8(json));
        });

        pm.startRenderer(*path, port, 0, QStringLiteral("flagtokenA"),
                         QStringLiteral("Hiyori"));
        QVERIFY2(waitForAction(msgSpy, "ready", 10000),
                 "renderer did not send 'ready' within 10s");

        QVERIFY2(pm.stop(), "stop() should return true");
        QVERIFY2(flagDuringExit,
                 "isManuallyStopping() must be true when exited fires during stop()");
        QVERIFY2(!pm.isManuallyStopping(),
                 "isManuallyStopping() must be cleared after stop() returns");

        server.close();
    }

    // ── Scenario B: external kill → flag is false, crashed=true ───────────
    {
        WsServer server;
        QVERIFY2(server.listen(0), "WsServer failed to listen on port 0");
        server.registerToken(0, QStringLiteral("flagtokenB"));
        const quint16 port = server.serverPort();

        ProcessManager pm;
        QSignalSpy msgSpy(&server, &WsServer::messageReceived);
        QSignalSpy exitedSpy(&pm, &ProcessManager::exited);

        bool flagDuringExit = true;
        connect(&pm, &ProcessManager::exited, &pm,
                [&pm, &flagDuringExit](int, bool) {
                    flagDuringExit = pm.isManuallyStopping();
                });

        pm.startRenderer(*path, port, 0, QStringLiteral("flagtokenB"),
                         QStringLiteral("Hiyori"));
        QVERIFY2(waitForAction(msgSpy, "ready", 10000),
                 "renderer did not send 'ready' within 10s");

        pm.kill();

        QVERIFY2(exitedSpy.wait(2000),
                 "exited not emitted within 2s after kill()");
        const bool crashed = exitedSpy.at(0).at(1).toBool();
        QVERIFY2(crashed, "external kill must report crashed=true");
        QVERIFY2(!flagDuringExit,
                 "isManuallyStopping() must be false on external kill");

        server.close();
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Original helpers
// ────────────────────────────────────────────────────────────────────────────

std::optional<QString> ProcessManagerTest::findRenderer()
{
    const QString binDir = QStringLiteral(BIN_OUTPUT_DIR);
    return core::resolveRendererPath(binDir, QStringLiteral("opengl"));
}

bool ProcessManagerTest::waitForAction(QSignalSpy& spy, const char* action,
                                       int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (!t.hasExpired(timeoutMs)) {
        for (int i = 0; i < spy.count(); ++i) {
            // Phase 5 todo 11: signal is (int instanceId, Envelope) — env at [1].
            const Envelope env = spy.at(i).at(1).value<Envelope>();
            if (env.action == QLatin1String(action))
                return true;
        }
        QTest::qWait(100); // pump the event loop
    }
    // Final check after the loop expires.
    for (int i = 0; i < spy.count(); ++i) {
        const Envelope env = spy.at(i).at(1).value<Envelope>();
        if (env.action == QLatin1String(action))
            return true;
    }
    return false;
}

void ProcessManagerTest::sendShutdown(WsServer& server)
{
    const QByteArray json =
        serialize(createCommand(QStringLiteral("shutdown"), QJsonObject{}))
            .toJson(QJsonDocument::Compact);
    // Phase 5 todo 11: sendText routes by instanceId; tests use instance_id=0.
    server.sendText(0, QString::fromUtf8(json));
}

QTEST_MAIN(ProcessManagerTest)
#include "ProcessManagerTest.moc"
