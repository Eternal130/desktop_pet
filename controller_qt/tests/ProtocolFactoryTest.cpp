#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QTest>

#include "network/Envelope.hpp"
#include "network/Protocol.hpp"

// ProtocolFactoryTest (T9) — verifies the typed builders produce payloads that
// match interface.md §5 field-for-field, cross-checked against the T3 fixture
// command_all_20.json. Also covers the three cross-cutting Envelope invariants
// the builders inherit from createCommand/createResponse:
//   1. command JSON carries NO response fields (§2.3);
//   2. response JSON carries success/error_code/error_message at TOP LEVEL and
//      payload is {} (§2.2);
//   3. id is non-empty and ≤ 64 chars (§2.1); timestamp is current ms (§2.1).
//
// PROTOCOL_FIXTURES_DIR is baked at compile time by tests/CMakeLists.txt so the
// fixture is found from any CWD (same pattern as ProtocolFixturesTest/EnvelopeTest).

class ProtocolFactoryTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    // ── Per-builder payload matches interface.md §5 / fixture ───────────
    void testLoadModel();
    void testSetPosition();
    void testSetSize();
    void testSetOpacity();
    void testSetFps();
    void testSetVolume();
    void testSetLayout();
    void testSetHitAreas();
    void testShutdown();

    // ── Runtime command factories (T16) ────────────────────────────────
    void testPlayMotion();
    void testStopMotion();
    void testSetExpression();
    void testPlayMotionExt();
    void testPlayMotionExtOmitsEmpty();
    void testPlayAudio();
    void testStopAudio();
    void testGetStats();
    void testGetLayout();
    void testResetLayout();

    // ── Cross-cutting invariants ────────────────────────────────────────
    void testResponseFieldsAtTopLevel();   // §2.2 response shape
    void testTimestampIsCurrentMs();       // explicit separate check

private:
    // Shared verification for every salvo builder: type/action/payload/id/
    // timestamp/no-response-fields. Centralized so each test slot stays a
    // one-liner while the invariants are asserted on EVERY command (not just
    // a representative one).
    void verifyCommand(const QString& action, const Envelope& env);

    QJsonObject m_fixture; // parsed command_all_20.json root

    QJsonObject fixturePayload(const QString& action) const;
};

void ProtocolFactoryTest::initTestCase() {
    const QString path = QStringLiteral(PROTOCOL_FIXTURES_DIR)
                         + QStringLiteral("/command_all_20.json");
    QFile f(path);
    QVERIFY2(f.open(QIODevice::ReadOnly),
             qPrintable(QStringLiteral("cannot open fixture: %1").arg(path)));
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    QVERIFY2(doc.isObject(), "fixture root is not an object");
    m_fixture = doc.object();
    QVERIFY2(m_fixture.value("cases").isArray(), "fixture has no cases[] array");
}

QJsonObject ProtocolFactoryTest::fixturePayload(const QString& action) const {
    const auto cases = m_fixture.value("cases").toArray();
    for (const QJsonValue& c : cases) {
        const QJsonObject input = c.toObject().value("input").toObject();
        if (input.value("action").toString() == action)
            return input.value("payload").toObject();
    }
    return QJsonObject{}; // caller's QCOMPARE will fail loudly if missing
}

void ProtocolFactoryTest::verifyCommand(const QString& action, const Envelope& env) {
    // type + action (struct + serialized)
    QCOMPARE(env.type, QStringLiteral("command"));
    QCOMPARE(env.action, action);

    const QJsonObject root = serialize(env).object();
    QCOMPARE(root.value("type").toString(), QStringLiteral("command"));
    QCOMPARE(root.value("action").toString(), action);

    // §2.1: id non-empty, ≤ 64 chars
    QVERIFY2(!env.id.isEmpty(), "id must be non-empty");
    QVERIFY2(env.id.length() <= 64,
             qPrintable(QStringLiteral("id exceeds 64 chars (%1): %2")
                            .arg(env.id.length()).arg(env.id)));

    // §2.1: timestamp is a positive Unix-ms value near now (allow 5s skew).
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QVERIFY2(env.timestamp > 0, "timestamp must be positive");
    QVERIFY2(env.timestamp <= now + 1000,
             qPrintable(QStringLiteral("timestamp is in the future: %1 > %2")
                            .arg(env.timestamp).arg(now)));
    QVERIFY2(now - env.timestamp <= 5000,
             qPrintable(QStringLiteral("timestamp stale by %1 ms").arg(now - env.timestamp)));

    // §5: payload matches interface.md / fixture field-for-field
    QCOMPARE(env.payload, fixturePayload(action));

    // §2.3: command JSON must NOT carry response fields
    QVERIFY2(!root.contains(QStringLiteral("success")),
             "command must not carry 'success'");
    QVERIFY2(!root.contains(QStringLiteral("error_code")),
             "command must not carry 'error_code'");
    QVERIFY2(!root.contains(QStringLiteral("error_message")),
             "command must not carry 'error_message'");
}

// ── Per-builder tests ──────────────────────────────────────────────────────

void ProtocolFactoryTest::testLoadModel() {
    verifyCommand(QStringLiteral("load_model"),
                  Protocol::buildLoadModel(QStringLiteral("Hiyori")));
}

void ProtocolFactoryTest::testSetPosition() {
    verifyCommand(QStringLiteral("set_position"),
                  Protocol::buildSetPosition(100, 200));
}

void ProtocolFactoryTest::testSetSize() {
    verifyCommand(QStringLiteral("set_size"),
                  Protocol::buildSetSize(400, 600));
}

void ProtocolFactoryTest::testSetOpacity() {
    verifyCommand(QStringLiteral("set_opacity"),
                  Protocol::buildSetOpacity(0.9));
}

void ProtocolFactoryTest::testSetFps() {
    verifyCommand(QStringLiteral("set_fps"),
                  Protocol::buildSetFps(30));
}

void ProtocolFactoryTest::testSetVolume() {
    verifyCommand(QStringLiteral("set_volume"),
                  Protocol::buildSetVolume(0.8, false));
}

void ProtocolFactoryTest::testSetLayout() {
    verifyCommand(QStringLiteral("set_layout"),
                  Protocol::buildSetLayout(10.0, -5.0, 1.2));
}

void ProtocolFactoryTest::testSetHitAreas() {
    const QJsonArray areas = fixturePayload(QStringLiteral("set_hit_areas"))
                                 .value(QStringLiteral("hit_areas")).toArray();
    verifyCommand(QStringLiteral("set_hit_areas"),
                  Protocol::buildSetHitAreas(areas));
}

void ProtocolFactoryTest::testShutdown() {
    verifyCommand(QStringLiteral("shutdown"),
                  Protocol::buildShutdown());
}

// ── Runtime command factories (T16) ────────────────────────────────────────
//
// Each factory's payload is asserted field-for-field against the corresponding
// command_all_20.json case via verifyCommand(). Fixture values mirror
// interface.md §B/C/D/E/F exactly.

void ProtocolFactoryTest::testPlayMotion() {
    // Fixture: {group:"Idle", index:0, priority:1}
    verifyCommand(QStringLiteral("play_motion"),
                  Protocol::buildPlayMotion(QStringLiteral("Idle"), 0, 1));
}

void ProtocolFactoryTest::testStopMotion() {
    verifyCommand(QStringLiteral("stop_motion"),
                  Protocol::buildStopMotion());
}

void ProtocolFactoryTest::testSetExpression() {
    // Fixture: {expression_id:"default"}
    verifyCommand(QStringLiteral("set_expression"),
                  Protocol::buildSetExpression(QStringLiteral("default")));
}

void ProtocolFactoryTest::testPlayMotionExt() {
    // Fixture: 6 fields populated (motion + fades + audio + lipSync; the
    // subtitle side-channel was removed in the bubble-stream switch).
    verifyCommand(QStringLiteral("play_motion_ext"),
                  Protocol::buildPlayMotionExt(
                      QStringLiteral("C:/pets/motions/wave.motion3.json"),
                      2, 1.0, 1.0,
                      QStringLiteral("C:/pets/audio/wave.ogg"),
                      QStringLiteral("C:/pets/lipsync/wave.txt")));
    // Subtitle fields must NEVER appear in the payload (bubble-stream switch).
    const Envelope env = Protocol::buildPlayMotionExt(
        QStringLiteral("C:/pets/motions/wave.motion3.json"), 2, 1.0, 1.0,
        QStringLiteral("C:/pets/audio/wave.ogg"),
        QStringLiteral("C:/pets/lipsync/wave.txt"));
    QVERIFY(!env.payload.contains(QStringLiteral("subtitle_text")));
    QVERIFY(!env.payload.contains(QStringLiteral("subtitle_duration")));
}

void ProtocolFactoryTest::testPlayMotionExtOmitsEmpty() {
    // When audio/lipSync/subtitle are empty, those keys MUST be absent from
    // the payload (interface.md §B.2 optional fields). Only the 4 always-on
    // keys (motion_path/priority/fade_in/fade_out) appear.
    const Envelope env = Protocol::buildPlayMotionExt(
        QStringLiteral("C:/pets/motions/wave.motion3.json"), 2, 1.0, 1.0);

    QCOMPARE(env.type, QStringLiteral("command"));
    QCOMPARE(env.action, QStringLiteral("play_motion_ext"));

    const QStringList keys = env.payload.keys();
    QCOMPARE(keys.size(), 4);
    QVERIFY(env.payload.contains(QStringLiteral("motion_path")));
    QVERIFY(env.payload.contains(QStringLiteral("priority")));
    QVERIFY(env.payload.contains(QStringLiteral("fade_in")));
    QVERIFY(env.payload.contains(QStringLiteral("fade_out")));
    QVERIFY(!env.payload.contains(QStringLiteral("audio_path")));
    QVERIFY(!env.payload.contains(QStringLiteral("lip_sync_path")));
    QVERIFY(!env.payload.contains(QStringLiteral("subtitle_text")));
    QVERIFY(!env.payload.contains(QStringLiteral("subtitle_duration")));
}

void ProtocolFactoryTest::testPlayAudio() {
    // Fixture: {audio_path, volume}
    verifyCommand(QStringLiteral("play_audio"),
                  Protocol::buildPlayAudio(
                      QStringLiteral("C:/pets/audio/greeting.ogg"), 0.8));
}

void ProtocolFactoryTest::testStopAudio() {
    verifyCommand(QStringLiteral("stop_audio"),
                  Protocol::buildStopAudio());
}

void ProtocolFactoryTest::testGetStats() {
    verifyCommand(QStringLiteral("get_stats"),
                  Protocol::buildGetStats());
}

void ProtocolFactoryTest::testGetLayout() {
    verifyCommand(QStringLiteral("get_layout"),
                  Protocol::buildGetLayout());
}

void ProtocolFactoryTest::testResetLayout() {
    verifyCommand(QStringLiteral("reset_layout"),
                  Protocol::buildResetLayout());
}

// ── Cross-cutting invariants ───────────────────────────────────────────────

void ProtocolFactoryTest::testResponseFieldsAtTopLevel() {
    // §2.2: createResponse emits success/error_code/error_message at the JSON
    // TOP LEVEL (siblings of type/action/id), NOT inside payload. Response
    // payload is always {}.
    const Envelope env = createResponse(
        QStringLiteral("cmd-load-model-0001"), QStringLiteral("load_model"),
        true, 0, QStringLiteral(""));

    QCOMPARE(env.type, QStringLiteral("response"));
    QVERIFY(env.hasResponseFields);
    QVERIFY(env.success);
    QCOMPARE(env.errorCode, 0);
    QCOMPARE(env.errorMessage, QStringLiteral(""));
    QCOMPARE(env.payload, QJsonObject{});

    const QJsonObject root = serialize(env).object();
    QCOMPARE(root.value("success").toBool(), true);
    QCOMPARE(static_cast<int>(root.value("error_code").toInteger()), 0);
    QCOMPARE(root.value("error_message").toString(), QStringLiteral(""));
    QCOMPARE(root.value("payload").toObject(), QJsonObject{});
}

void ProtocolFactoryTest::testTimestampIsCurrentMs() {
    // Explicit independent check (verifyCommand already covers this per-builder,
    // but a dedicated slot documents the invariant for the response factory too).
    const Envelope cmd = Protocol::buildShutdown();
    const Envelope rsp = createResponse(cmd.id, QStringLiteral("shutdown"), true);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QVERIFY(cmd.timestamp > now - 5000 && cmd.timestamp <= now + 1000);
    QVERIFY(rsp.timestamp > now - 5000 && rsp.timestamp <= now + 1000);
}

QTEST_APPLESS_MAIN(ProtocolFactoryTest)
#include "ProtocolFactoryTest.moc"
