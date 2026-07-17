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
//     - testSalvoOrderAndCount: sendSalvo → exactly 9 commands in order
//       (load_model, set_position, set_size, set_opacity, set_fps, set_volume,
//        set_layout, set_subtitle_layout, set_subtitle_style)
//     - testNoSetScaleEverSent: PRIMARY acceptance criterion — the captured
//       JSON stream MUST NOT contain "set_scale" anywhere
//     - testSubtitleStyleHasAll16Fields: set_subtitle_style payload carries
//       the full 16-field style block from interface.md §D.1
//     - testSalvoCommandsInProtocolCatalog: every sent action exists in
//       command_all_25.json's expected_action_set (cross-check against T3)
//
//   Integration tier [REQUIRES_RENDERER]:
//     - testFullSalvoWithLiveRenderer: WsServer + ProcessManager + StartupSalvo
//       wired together. After `ready` → sendSalvo, after `model_loaded` →
//       sendSetHitAreas. Asserts 9 + 1 = 10 commands captured, correct order,
//       no set_scale.
//
// QTEST_MAIN (NOT APPLESS) — the integration tier uses QSignalSpy::wait /
// QTest::qWait to pump the event loop for the WS handshake + model loading.

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
#include <QTest>

#include <optional>

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
    void testSubtitleStyleHasAll16Fields();
    void testSalvoCommandsInProtocolCatalog();

    // ── Integration tier [REQUIRES_RENDERER] ───────────────────────────────
    void testFullSalvoWithLiveRenderer();

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
    // populated, layout/subtitle offsets at 0/0/1.0/0/0/0/0).
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
                         + QStringLiteral("/command_all_25.json");
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

// sendSalvo fires exactly 9 commands in the documented order, and each one
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
        QStringLiteral("set_subtitle_layout"),
        QStringLiteral("set_subtitle_style"),
    };

    QCOMPARE(sent.size(), 9);
    QCOMPARE(captured.size(), 9);
    QCOMPARE(spy.count(), 9);
    QCOMPARE(sent, expected);

    // The commandSent signal carries the same action list — verify the signal
    // path matches the sender path slot-for-slot (anti-drift guard).
    for (int i = 0; i < 9; ++i) {
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

// set_subtitle_style carries all 16 named style fields from interface.md §D.1.
// Defaults reproduce the fixture payload in command_all_25.json exactly (the
// T9 factory already guarantees this; here we lock the field NAME set too).
void StartupSalvoTest::testSubtitleStyleHasAll16Fields() {
    StartupSalvo salvo;

    QString styleJson;
    salvo.setCommandSender([&styleJson](const QString& json) {
        // Capture the last-sent command (set_subtitle_style is salvo slot 9).
        styleJson = json;
    });

    salvo.sendSalvo(defaultConfig());

    const QJsonObject root =
        QJsonDocument::fromJson(styleJson.toUtf8()).object();
    QCOMPARE(root.value("action").toString(), QStringLiteral("set_subtitle_style"));
    const QJsonObject p = root.value("payload").toObject();

    const QStringList expectedFields{
        QStringLiteral("font_name"),
        QStringLiteral("font_size"),
        QStringLiteral("primary_color"),
        QStringLiteral("outline_color"),
        QStringLiteral("outline_width"),
        QStringLiteral("shadow_color"),
        QStringLiteral("shadow_depth"),
        QStringLiteral("alignment"),
        QStringLiteral("margin_v"),
        QStringLiteral("edge_blur"),
        QStringLiteral("font_weight"),
        QStringLiteral("letter_spacing"),
        QStringLiteral("bg_box_enabled"),
        QStringLiteral("bg_box_color"),
        QStringLiteral("bg_box_padding_x"),
        QStringLiteral("bg_box_padding_y"),
    };
    QCOMPARE(expectedFields.size(), 16);

    for (const QString& f : expectedFields) {
        QVERIFY2(p.contains(f),
                 qPrintable(QStringLiteral("set_subtitle_style missing field: %1")
                                .arg(f)));
    }
    QCOMPARE(p.size(), 16); // exactly 16 fields — no extras, no missing
}

// Every sent action is a member of command_all_25.json's expected_action_set.
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
// Integration tier [REQUIRES_RENDERER]
// ────────────────────────────────────────────────────────────────────────────

// Full bootstrap against the REAL renderer: ready → sendSalvo (9 cmds) →
// model_loaded → sendSetHitAreas (1 cmd). Captures all 10 via commandSent and
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

    // ── ready → fire the 9-command salvo ──────────────────────────────────
    QVERIFY2(waitForAction(msgSpy, "ready", 10000),
             "renderer did not send 'ready' within 10s");
    const QStringList sent = salvo.sendSalvo(defaultConfig());
    QCOMPARE(sent.size(), 9);

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
    QCOMPARE(sentSpy.count(), 10);

    const QStringList expectedOrder{
        QStringLiteral("load_model"),
        QStringLiteral("set_position"),
        QStringLiteral("set_size"),
        QStringLiteral("set_opacity"),
        QStringLiteral("set_fps"),
        QStringLiteral("set_volume"),
        QStringLiteral("set_layout"),
        QStringLiteral("set_subtitle_layout"),
        QStringLiteral("set_subtitle_style"),
        QStringLiteral("set_hit_areas"),
    };
    for (int i = 0; i < 10; ++i) {
        const QString action = sentSpy.at(i).at(0).toString();
        QCOMPARE(action, expectedOrder.at(i));
        const QString json = sentSpy.at(i).at(1).toString();
        QVERIFY2(!json.contains(QStringLiteral("\"set_scale\"")),
                 qPrintable(QStringLiteral("set_scale leaked at slot %1: %2")
                                .arg(i).arg(json)));
    }

    // ── Cleanly shut down (tolerate the known renderer teardown crash) ────
    QVERIFY2(pm.stop(), "stop() should return true (renderer exited within 5s)");
    QVERIFY2(exitedSpy.count() >= 1, "exited signal not emitted by stop()");
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
