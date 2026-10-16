// allow: SIZE_OK — single cohesive QTest SUT with two tiers (unit + integration).
// The 4 unit slots share a capturing-sender harness; the 1 integration slot
// shares the WsServer+ProcessManager harness with ProcessManagerTest (T13).
// Splitting by tier would split the shared harness duplication and obscure
// the "sendSalvo invariants" narrative the file documents end-to-end.

// StartupSalvoTest (task T16) — verifies the controller→renderer bootstrap
// command stream produced by StartupSalvo.
//
// Two tiers:
//
//   Unit tier (no renderer needed):
//     - testSalvoOrderAndCount: sendSalvo → exactly 7 commands in order
//       (load_model, set_position, set_size, set_opacity, set_fps, set_volume,
//        set_layout)
//     - testNoSetScaleEverSent: PRIMARY acceptance criterion — the captured
//       JSON stream MUST NOT contain "set_scale" anywhere
//     - testSalvoCommandsInProtocolCatalog: every sent action exists in
//       command_all_20.json's expected_action_set (cross-check against T3)
//
//   Integration tier [REQUIRES_RENDERER]:
//     - testFullSalvoWithLiveRenderer: WsServer + ProcessManager + StartupSalvo
//       wired together. After `ready` → sendSalvo, after `model_loaded` →
//       sendSetHitAreas. Asserts 7 + 1 = 8 commands captured, correct order,
//       no set_scale.
//     - testScopedSalvoWithLiveRenderer (v1.13): a temp PluginData-domain
//       copy of the Hiyori fixture loaded through the path-bearing salvo —
//       asserts model_loaded arrives in path mode and the scoped copy's
//       hit areas round-trip via set_hit_areas.
//
// QTEST_MAIN (NOT APPLESS) — the integration tier uses QSignalSpy::wait /
// QTest::qWait to pump the event loop for the WS handshake + model loading.

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include <optional>

#include "core/ModelInfoParser.hpp"
#include "core/PathResolve.hpp"
#include "core/ProcessManager.hpp"
#include "core/StartupSalvo.hpp"
#include "network/Envelope.hpp"
#include "network/Protocol.hpp"
#include "network/WsServer.hpp"

Q_DECLARE_METATYPE(Envelope)

class StartupSalvoTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();

    // ── Unit tier (no renderer) ────────────────────────────────────────────
    void testSalvoOrderAndCount();
    void testNoSetScaleEverSent();
    void testSalvoCommandsInProtocolCatalog();
    // ── v1.13 scoped salvo (resource isolation, rev2 §3.4) ──────────────
    void testScopedSalvoCarriesPathInFirstCommand();
    void testUnscopedSalvoIsByteIdenticalLegacy();

    // ── Integration tier [REQUIRES_RENDERER] ───────────────────────────────
    void testFullSalvoWithLiveRenderer();
    // v1.13 scoped e2e (oracle follow-up Should#4): a plugin-data-domain
    // model copy loaded through the path-bearing salvo against the REAL
    // renderer (model_loaded + hit-areas round-trip).
    void testScopedSalvoWithLiveRenderer();

private:
    // Locate the protocol fixture command_all_25.json. Baked at compile time
    // (PROTOCOL_FIXTURES_DIR) so the test finds it from any CWD.
    static QJsonObject loadFixture();

    // Resolve the renderer exe (integration tier). Returns nullopt when the
    // binary is absent — caller QSKIPs.
    static std::optional<QString> findRenderer();

    // Wait for msgSpy to capture an Envelope whose action matches `action`.
    // Polls via QTest::qWait (event-loop pump) up to timeoutMs.
    static bool waitForAction(QSignalSpy& spy, const char* action, int timeoutMs);

    // Default config used by every unit slot — matches InstanceConfigLike's
    // default-constructed values (modelName="Hiyori", all positions/sizes
    // populated, layout offsets at 0/0/1.0).
    static InstanceConfigLike defaultConfig();
};

void StartupSalvoTest::initTestCase() {
    qRegisterMetaType<Envelope>();
}

// ────────────────────────────────────────────────────────────────────────────
// Unit tier helpers
// ────────────────────────────────────────────────────────────────────────────

QJsonObject StartupSalvoTest::loadFixture() {
    const QString path = QStringLiteral(PROTOCOL_FIXTURES_DIR)
                         + QStringLiteral("/command_all_20.json");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        // Cannot QFAIL here — QFAIL expands to `return;` which is invalid in a
        // function returning QJsonObject. Mark the test failure explicitly,
        // then return an empty object so callers' QVERIFY/QCOMPARE fail loudly.
        QTest::qFail(qPrintable(QStringLiteral("cannot open fixture: %1").arg(path)),
                     __FILE__, __LINE__);
        return {};
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    return doc.object();
}

InstanceConfigLike StartupSalvoTest::defaultConfig() {
    return InstanceConfigLike{};
}

// ────────────────────────────────────────────────────────────────────────────
// Unit tier
// ────────────────────────────────────────────────────────────────────────────

// sendSalvo fires exactly 7 commands in the documented order, and each one
// is also surfaced via the commandSent signal (so the integration tier can
// observe the stream without intercepting WsServer::sendText).
void StartupSalvoTest::testSalvoOrderAndCount() {
    StartupSalvo salvo;

    QStringList captured;
    salvo.setCommandSender([&captured](const QString& json) {
        captured.append(json);
    });
    QSignalSpy spy(&salvo, &StartupSalvo::commandSent);
    QVERIFY(spy.isValid());

    const QStringList sent = salvo.sendSalvo(defaultConfig());

    const QStringList expected{
        QStringLiteral("load_model"),
        QStringLiteral("set_position"),
        QStringLiteral("set_size"),
        QStringLiteral("set_opacity"),
        QStringLiteral("set_fps"),
        QStringLiteral("set_volume"),
        QStringLiteral("set_layout"),
    };

    QCOMPARE(sent.size(), 7);
    QCOMPARE(captured.size(), 7);
    QCOMPARE(spy.count(), 7);
    QCOMPARE(sent, expected);

    // The commandSent signal carries the same action list — verify the signal
    // path matches the sender path slot-for-slot (anti-drift guard).
    for (int i = 0; i < 7; ++i) {
        const QString actionFromSignal = spy.at(i).at(0).toString();
        QCOMPARE(actionFromSignal, expected.at(i));
    }
}

// PRIMARY acceptance criterion: set_scale MUST NEVER appear anywhere in the
// captured JSON stream. Asserted as a substring search so it catches both the
// action field and any stray payload key (defensive against future regressions
// in Protocol::buildSetLayout).
void StartupSalvoTest::testNoSetScaleEverSent() {
    StartupSalvo salvo;

    QStringList captured;
    salvo.setCommandSender([&captured](const QString& json) {
        captured.append(json);
    });

    salvo.sendSalvo(defaultConfig());

    // Also exercise sendSetHitAreas — it must never emit set_scale either.
    QJsonArray areas;
    areas.append(QStringLiteral("Head"));
    areas.append(QStringLiteral("Body"));
    salvo.sendSetHitAreas(areas);

    for (const QString& json : std::as_const(captured)) {
        QVERIFY2(!json.contains(QStringLiteral("\"set_scale\"")),
                 qPrintable(QStringLiteral("set_scale leaked into stream: %1")
                                .arg(json)));
        QVERIFY2(!json.contains(QStringLiteral("\"scale\""))
                     || json.contains(QStringLiteral("\"set_layout\"")),
                 qPrintable(QStringLiteral(
                    "scale key appeared outside a set_layout envelope: %1")
                    .arg(json)));
    }
}

// Every sent action is a member of command_all_20.json's expected_action_set.
// This guards against typos / speculative action names that aren't in the
// protocol catalog (e.g. a future "set_window_opacity" misspelling).
void StartupSalvoTest::testSalvoCommandsInProtocolCatalog() {
    StartupSalvo salvo;
    salvo.setCommandSender([](const QString&) {});

    const QStringList sent = salvo.sendSalvo(defaultConfig());
    QVERIFY(!sent.isEmpty());

    const QJsonObject fixture = loadFixture();
    const QJsonArray catalog = fixture.value("expected_action_set").toArray();
    QVERIFY2(!catalog.isEmpty(), "fixture has no expected_action_set[]");

    QSet<QString> catalogSet;
    for (const QJsonValue& v : catalog)
        catalogSet.insert(v.toString());

    for (const QString& action : sent) {
        QVERIFY2(catalogSet.contains(action),
                 qPrintable(QStringLiteral(
                    "salvo action not in protocol catalog: %1").arg(action)));
    }
}

// ────────────────────────────────────────────────────────────────────────────
// v1.13 scoped salvo (resource isolation, rev2 §3.4)
// ────────────────────────────────────────────────────────────────────────────

// A scoped InstanceConfigLike (resolvedModelPath non-empty) makes the FIRST
// command a load_model carrying BOTH model_path (the logical name —
// model_loaded.model_id echoes it) and path (the absolute physical path).
void StartupSalvoTest::testScopedSalvoCarriesPathInFirstCommand() {
    StartupSalvo salvo;

    QStringList captured;
    salvo.setCommandSender([&captured](const QString& json) {
        captured.append(json);
    });
    QSignalSpy spy(&salvo, &StartupSalvo::commandSent);
    QVERIFY(spy.isValid());

    InstanceConfigLike cfg = defaultConfig();
    cfg.modelScope = QStringLiteral("com.example.pet");
    cfg.resolvedModelPath =
        QStringLiteral("/data/PluginData/com.example.pet/Models/X/X.model3.json");

    const QStringList sent = salvo.sendSalvo(cfg);

    // Salvo shape unchanged: 7 commands, load_model first.
    QCOMPARE(sent.size(), 7);
    QCOMPARE(sent.first(), QStringLiteral("load_model"));

    const QJsonObject first =
        QJsonDocument::fromJson(captured.first().toUtf8()).object();
    QCOMPARE(first.value(QStringLiteral("action")).toString(),
             QStringLiteral("load_model"));
    const QJsonObject payload = first.value(QStringLiteral("payload")).toObject();
    QCOMPARE(payload.value(QStringLiteral("model_path")).toString(),
             QStringLiteral("Hiyori"));
    QCOMPARE(payload.value(QStringLiteral("path")).toString(),
             cfg.resolvedModelPath);

    // No OTHER command in the stream carries a "path" payload key.
    for (int i = 1; i < captured.size(); ++i) {
        const QJsonObject other =
            QJsonDocument::fromJson(captured.at(i).toUtf8()).object();
        QVERIFY2(!other.value(QStringLiteral("payload")).toObject().contains(
                     QStringLiteral("path")),
                 qPrintable(QStringLiteral(
                     "path leaked into a non-load_model command: %1")
                         .arg(captured.at(i))));
    }
}

// An unscoped config (resolvedModelPath empty — the host domain default)
// produces the salvo the pre-v1.13 code produced: load_model's payload
// carries model_path ONLY (no path key) and every command's payload
// matches the legacy shape. (The raw wire strings cannot be compared
// byte-for-byte across salvo invocations — each command carries a fresh
// UUID id + timestamp — so the identity assertion is on the
// (action, payload) pairs, which ARE the wire content modulo the envelope
// identity fields.)
void StartupSalvoTest::testUnscopedSalvoIsByteIdenticalLegacy() {
    StartupSalvo salvo;

    QStringList legacy;
    salvo.setCommandSender([&legacy](const QString& json) {
        legacy.append(json);
    });
    salvo.sendSalvo(defaultConfig());

    // A config that explicitly carries a modelScope but NO resolved path
    // (the ready-time resolution MISSED) must degrade to the same legacy
    // wire form — the salvo never invents a path.
    QStringList scopedMiss;
    salvo.setCommandSender([&scopedMiss](const QString& json) {
        scopedMiss.append(json);
    });
    InstanceConfigLike miss = defaultConfig();
    miss.modelScope = QStringLiteral("com.example.pet");
    miss.resolvedModelPath = QString(); // resolver miss
    salvo.sendSalvo(miss);

    QCOMPARE(scopedMiss.size(), legacy.size());
    for (int i = 0; i < legacy.size(); ++i) {
        const QJsonObject a =
            QJsonDocument::fromJson(legacy.at(i).toUtf8()).object();
        const QJsonObject b =
            QJsonDocument::fromJson(scopedMiss.at(i).toUtf8()).object();
        QCOMPARE(b.value(QStringLiteral("action")).toString(),
                 a.value(QStringLiteral("action")).toString());
        QCOMPARE(b.value(QStringLiteral("payload")).toObject(),
                 a.value(QStringLiteral("payload")).toObject());
    }
    const QJsonObject first =
        QJsonDocument::fromJson(legacy.first().toUtf8()).object();
    QVERIFY2(!first.value(QStringLiteral("payload")).toObject().contains(
                 QStringLiteral("path")),
             "unscoped load_model must not carry a path key");
}

// ────────────────────────────────────────────────────────────────────────────
// Integration tier [REQUIRES_RENDERER]
// ────────────────────────────────────────────────────────────────────────────

// Full bootstrap against the REAL renderer: ready → sendSalvo (7 cmds) →
// model_loaded → sendSetHitAreas (1 cmd). Captures all 8 via commandSent and
// asserts order + no set_scale. Renderer teardown crash is tolerated (same
// known issue as T6/T13 — see learnings.md).
void StartupSalvoTest::testFullSalvoWithLiveRenderer() {
    const auto path = findRenderer();
    if (!path.has_value())
        QSKIP("renderer binary absent");

    WsServer server;
    QVERIFY2(server.listen(0), "WsServer failed to listen on port 0");
    server.registerToken(0, QStringLiteral("salvotoken"));
    const quint16 port = server.serverPort();

    ProcessManager pm;
    StartupSalvo salvo;

    // Wire the salvo's sender to the live WsServer (production wiring).
    salvo.setCommandSender([&server](const QString& json) {
        server.sendText(0, json);
    });

    // Graceful shutdown path — stop() sends shutdown via this hook (T13/T15).
    pm.setShutdownSender([&server]() {
        const QByteArray json =
            serialize(Protocol::buildShutdown()).toJson(QJsonDocument::Compact);
        server.sendText(0, QString::fromUtf8(json));
    });

    QSignalSpy msgSpy(&server, &WsServer::messageReceived);
    QSignalSpy sentSpy(&salvo, &StartupSalvo::commandSent);
    QSignalSpy exitedSpy(&pm, &ProcessManager::exited);
    QVERIFY(msgSpy.isValid());
    QVERIFY(sentSpy.isValid());
    QVERIFY(exitedSpy.isValid());

    pm.startRenderer(*path, port, 0, QStringLiteral("salvotoken"),
                     QStringLiteral("Hiyori"));

    // ── ready → fire the 7-command salvo ──────────────────────────────────
    QVERIFY2(waitForAction(msgSpy, "ready", 10000),
             "renderer did not send 'ready' within 10s");
    const QStringList sent = salvo.sendSalvo(defaultConfig());
    QCOMPARE(sent.size(), 7);

    // ── model_loaded → fire set_hit_areas ─────────────────────────────────
    QVERIFY2(waitForAction(msgSpy, "model_loaded", 15000),
             "renderer did not send 'model_loaded' within 15s");
    QJsonArray areas;
    areas.append(QStringLiteral("Head"));
    areas.append(QStringLiteral("Body"));
    salvo.sendSetHitAreas(areas);

    // ── Assert the full captured stream ───────────────────────────────────
    // Allow the event loop to flush any in-flight commandSent emissions.
    QTest::qWait(50);
    QCOMPARE(sentSpy.count(), 8);

    const QStringList expectedOrder{
        QStringLiteral("load_model"),
        QStringLiteral("set_position"),
        QStringLiteral("set_size"),
        QStringLiteral("set_opacity"),
        QStringLiteral("set_fps"),
        QStringLiteral("set_volume"),
        QStringLiteral("set_layout"),
        QStringLiteral("set_hit_areas"),
    };
    for (int i = 0; i < 8; ++i) {
        const QString action = sentSpy.at(i).at(0).toString();
        QCOMPARE(action, expectedOrder.at(i));
        const QString json = sentSpy.at(i).at(1).toString();
        QVERIFY2(!json.contains(QStringLiteral("\"set_scale\"")),
                 qPrintable(QStringLiteral("set_scale leaked at slot %1: %2")
                                .arg(i).arg(json)));
    }

    // ── Cleanly shut down (tolerate the known renderer teardown crash) ────
    // S4: stop() is async — completion arrives via stopFinished.
    QSignalSpy stopSpy(&pm, &ProcessManager::stopFinished);
    pm.stop();
    QVERIFY2(stopSpy.wait(7000), "stopFinished not emitted within 7s");
    QVERIFY2(stopSpy.at(0).at(0).toBool(),
             "stop should complete cleanly (renderer exited within 5s)");
    QVERIFY2(exitedSpy.count() >= 1, "exited signal not emitted by stop()");
    QVERIFY2(!pm.isRunning(), "renderer process still running after stop()");

    server.close();
}

// v1.13 scoped e2e (oracle follow-up Should#4, rev2 §3.4): the REAL
// renderer loads a PLUGIN-DOMAIN model. A temp PluginData domain receives
// a copy of the Cubism Hiyori fixture; a scoped config (modelScope +
// resolvedModelPath) makes the salvo's first load_model carry BOTH the
// logical name and the absolute path. Acceptance: model_loaded arrives
// (the renderer's path mode), and the controller-side hit areas parsed
// from the SCOPED copy round-trip via set_hit_areas. Renderer teardown
// crash tolerated (same known issue as the slot above).
void StartupSalvoTest::testScopedSalvoWithLiveRenderer()
{
    const auto path = findRenderer();
    if (!path.has_value())
        QSKIP("renderer binary absent");

    // ── Fixture: copy the real Hiyori model into a temp PluginData domain ──
    // (the production layout <root>/<pluginId>/Models/<Name>/ so the copy
    // is what a plugin-domain install would have produced).
    QTemporaryDir base;
    QVERIFY2(base.isValid(), "temp dir for plugin data domain");
    const QString scopedJson = QDir(base.path()).absoluteFilePath(
        QStringLiteral("PluginData/org.e2e.scope/Models/Hiyori/"
                       "Hiyori.model3.json"));
    {
        const QString src = QStringLiteral(CUBISM_RESOURCES_DIR)
                          + QStringLiteral("/Hiyori");
        QDirIterator it(src, QDir::Files | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            const QString dest = QDir(base.path()).absoluteFilePath(
                QStringLiteral("PluginData/org.e2e.scope/Models/Hiyori/")
                + QDir(src).relativeFilePath(it.filePath()));
            QVERIFY2(QDir().mkpath(QFileInfo(dest).absolutePath()),
                     "mkpath failed for fixture copy");
            QVERIFY2(QFile::copy(it.filePath(), dest),
                     qPrintable(QStringLiteral("copy failed: %1")
                                    .arg(it.filePath())));
        }
    }
    QVERIFY2(QFile::exists(scopedJson), "scoped fixture json missing");
    // Controller-side hit areas from the SCOPED copy (the model_loaded
    // handler's parse source in production).
    const auto scopedInfo = core::parseModelInfo(scopedJson);
    QVERIFY2(scopedInfo.has_value(), "scoped fixture json unparseable");
    QVERIFY2(!scopedInfo->hitAreas.isEmpty(), "fixture has no hit areas");

    // ── Same WsServer + ProcessManager + StartupSalvo harness ─────────────
    WsServer server;
    QVERIFY2(server.listen(0), "WsServer failed to listen on port 0");
    server.registerToken(0, QStringLiteral("scopedtoken"));
    const quint16 port = server.serverPort();

    ProcessManager pm;
    StartupSalvo salvo;
    salvo.setCommandSender([&server](const QString& json) {
        server.sendText(0, json);
    });
    pm.setShutdownSender([&server]() {
        const QByteArray json =
            serialize(Protocol::buildShutdown()).toJson(QJsonDocument::Compact);
        server.sendText(0, QString::fromUtf8(json));
    });

    QSignalSpy msgSpy(&server, &WsServer::messageReceived);
    QSignalSpy sentSpy(&salvo, &StartupSalvo::commandSent);
    QSignalSpy exitedSpy(&pm, &ProcessManager::exited);
    QVERIFY(msgSpy.isValid());
    QVERIFY(sentSpy.isValid());
    QVERIFY(exitedSpy.isValid());

    pm.startRenderer(*path, port, 0, QStringLiteral("scopedtoken"),
                     QStringLiteral("Hiyori"));

    // ── ready → scoped salvo (first command carries the absolute path) ────
    QVERIFY2(waitForAction(msgSpy, "ready", 10000),
             "renderer did not send 'ready' within 10s");
    InstanceConfigLike cfg = defaultConfig();
    cfg.modelScope = QStringLiteral("org.e2e.scope");
    cfg.resolvedModelPath = scopedJson;
    const QStringList sent = salvo.sendSalvo(cfg);
    QCOMPARE(sent.size(), 7);
    QCOMPARE(sent.first(), QStringLiteral("load_model"));
    QVERIFY2(sentSpy.count() >= 1, "commandSent not emitted synchronously");
    {
        const QJsonObject first = QJsonDocument::fromJson(
            sentSpy.at(0).at(1).toString().toUtf8()).object();
        const QJsonObject payload =
            first.value(QStringLiteral("payload")).toObject();
        QCOMPARE(payload.value(QStringLiteral("model_path")).toString(),
                 QStringLiteral("Hiyori"));
        QCOMPARE(payload.value(QStringLiteral("path")).toString(), scopedJson);
    }

    // ── model_loaded via path mode → hit areas from the SCOPED copy ───────
    QVERIFY2(waitForAction(msgSpy, "model_loaded", 15000),
             "renderer did not send 'model_loaded' for the path-mode load "
             "within 15s");
    salvo.sendSetHitAreas(QJsonArray::fromStringList(scopedInfo->hitAreas));

    QTest::qWait(50);
    QCOMPARE(sentSpy.count(), 8);
    QCOMPARE(sentSpy.at(7).at(0).toString(), QStringLiteral("set_hit_areas"));

    // ── Cleanly shut down (tolerate the known renderer teardown crash) ────
    QSignalSpy stopSpy(&pm, &ProcessManager::stopFinished);
    pm.stop();
    QVERIFY2(stopSpy.wait(7000), "stopFinished not emitted within 7s");
    QVERIFY2(stopSpy.at(0).at(0).toBool(),
             "stop should complete cleanly (renderer exited within 5s)");
    QVERIFY2(!pm.isRunning(), "renderer process still running after stop()");

    server.close();
}

// ────────────────────────────────────────────────────────────────────────────
// Helpers
// ────────────────────────────────────────────────────────────────────────────

std::optional<QString> StartupSalvoTest::findRenderer() {
    const QString binDir = QStringLiteral(BIN_OUTPUT_DIR);
    return core::resolveRendererPath(binDir, QStringLiteral("opengl"));
}

bool StartupSalvoTest::waitForAction(QSignalSpy& spy, const char* action,
                                     int timeoutMs) {
    QElapsedTimer t;
    t.start();
    while (!t.hasExpired(timeoutMs)) {
        for (int i = 0; i < spy.count(); ++i) {
            // Phase 5 todo 11: signal is (int instanceId, Envelope) — env at [1].
            const Envelope env = spy.at(i).at(1).value<Envelope>();
            if (env.action == QLatin1String(action))
                return true;
        }
        QTest::qWait(100);
    }
    for (int i = 0; i < spy.count(); ++i) {
        const Envelope env = spy.at(i).at(1).value<Envelope>();
        if (env.action == QLatin1String(action))
            return true;
    }
    return false;
}

QTEST_MAIN(StartupSalvoTest)
#include "StartupSalvoTest.moc"
