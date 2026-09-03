// allow: SIZE_OK — single cohesive QTest SUT for the Envelope ser/deser model.
// Splitting QTest private-slots across executables would fragment one logical
// contract (serialize/deserialize invariants); kept as one class by intent.
//
// EnvelopeTest — TDD test for the protocol Envelope model (T4).
//
// Drives serialize()/deserialize() against the four T3 fixtures:
//   - envelope_serialize.json   : serialization invariants per §2.1-2.3
//   - envelope_deserialize.json : valid + malformed cases per §2.4 / §1.5
//   - command_all_20.json       : round-trip identity for all 20 commands
//   - event_all_13.json         : round-trip identity for all 13 events
//
// Plus targeted tests for the two cross-cutting correctness requirements:
//   - int64 timestamp survives serialize -> deserialize without truncation
//     (1710000000000 > INT32_MAX; toInteger() not toInt()).
//   - response success/error_code/error_message live at JSON TOP LEVEL, with
//     payload fixed to {} (§2.2) — never inside payload, never on cmd/event.

#include "network/Envelope.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QObject>
#include <QTest>
#include <limits>

namespace {

// Absolute path to protocol/ baked in at compile time by tests/CMakeLists.txt
// (PROTOCOL_FIXTURES_DIR). Lets ctest find the JSON regardless of CWD.
QString fixturesDir()
{
    return QString::fromUtf8(PROTOCOL_FIXTURES_DIR);
}

// Wrap a JSON value into a QJsonDocument for the document-overload path.
// QJsonDocument can only hold an object or array at root; a scalar/string root
// is not representable and is not an envelope anyway, so it maps to a null doc,
// which deserialize() rejects (returns nullopt) — the intended malformed result.
QJsonDocument valueAsDocument(const QJsonValue& v)
{
    if (v.isObject())
        return QJsonDocument(v.toObject());
    if (v.isArray())
        return QJsonDocument(v.toArray());
    return QJsonDocument();
}

} // namespace

class EnvelopeTest : public QObject
{
    Q_OBJECT

private:
    // Loads <name> from the fixture dir into `out`. Uses QFAIL on a missing or
    // unparseable file (a fixture-infra bug, not an Envelope bug); returns void
    // so QFAIL's `return;` is valid.
    void loadFixture(const QString& name, QJsonObject& out);

private slots:
    void initTestCase();

    // Fixture-driven invariants.
    void testSerializeFromFixtures();
    void testDeserializeFromFixtures();
    void testRoundTripCommands();
    void testRoundTripEvents();

    // Targeted correctness guards.
    void testTimestampInt64Precision();
    void testResponseFieldsAtTopLevel();
    void testObjectOverloadDeserialize();
    void testCreateHelpersAreWellFormed();
};

void EnvelopeTest::loadFixture(const QString& name, QJsonObject& out)
{
    const QString path = QDir(fixturesDir()).filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QFAIL(qPrintable(QStringLiteral("Could not open fixture: %1").arg(path)));
        return;
    }
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        QFAIL(qPrintable(QStringLiteral("Fixture %1 is not valid JSON: %2")
                             .arg(path, err.errorString())));
        return;
    }
    out = doc.object();
}

void EnvelopeTest::initTestCase()
{
    // Fail fast with a clear message if the fixture dir wiring is broken,
    // rather than emitting a misleading stream of per-case QVERIFY failures.
    const QString dir = fixturesDir();
    QVERIFY2(QDir(dir).exists(),
             qPrintable(QStringLiteral("PROTOCOL_FIXTURES_DIR does not exist: %1").arg(dir)));
}

void EnvelopeTest::testSerializeFromFixtures()
{
    QJsonObject root;
    loadFixture(QStringLiteral("envelope_serialize.json"), root);
    const QJsonArray cases = root.value("cases").toArray();
    QVERIFY2(!cases.isEmpty(), "envelope_serialize.json has no cases");

    for (qsizetype i = 0; i < cases.size(); ++i) {
        const QJsonObject c = cases.at(i).toObject();
        const QString name = c.value("name").toString();
        const QByteArray ctx = "[" + QByteArray::number(i) + "] " + name.toUtf8();

        // Each serialize case's input is a valid envelope: deserialize it, then
        // re-serialize and assert the output matches the expected invariants.
        const QJsonObject input = c.value("input").toObject();
        const auto env = deserialize(input);
        QVERIFY2(env.has_value(),
                 ctx + ": input must deserialize (fixture inputs are valid)");

        const QJsonObject out = serialize(*env).object();
        const QJsonObject expected = c.value("expected").toObject();

        // type is always present and preserved.
        QCOMPARE(out.value("type").toString(), env->type);
        QCOMPARE(env->type, expected.value("type").toString());

        // payload is ALWAYS a JSON object, for every message type (§1.5).
        QVERIFY2(out.value("payload").isObject(),
                 ctx + ": serialized payload must be an object");
        QVERIFY2(out.value("payload").toObject() == env->payload,
                 ctx + ": serialized payload must equal the envelope payload");

        // response_fields_at_top_level: success/error_code/error_message are
        // siblings of type/action/id, present iff type == "response" (§2.2-2.3).
        const bool expectResponseFields = expected.value("has_top_level_response_fields").toBool(false);
        QCOMPARE(out.contains("success"), expectResponseFields);
        QCOMPARE(out.contains("error_code"), expectResponseFields);
        QCOMPARE(out.contains("error_message"), expectResponseFields);
        QVERIFY2(!out.value("payload").toObject().contains("success"),
                 ctx + ": response fields must NEVER appear inside payload");

        // When present, response field values match (§2.2).
        if (expectResponseFields) {
            QCOMPARE(out.value("success").toBool(), expected.value("success").toBool());
            QCOMPARE(out.value("error_code").toInteger(), expected.value("error_code").toInteger());
            // error_message present on every response case except the 5003 one,
            // which omits it from expected — only assert when expected states it.
            if (expected.contains("error_message"))
                QCOMPARE(out.value("error_message").toString(),
                         expected.value("error_message").toString());
            QVERIFY2(out.value("payload").toObject().isEmpty(),
                     ctx + ": response payload must be the empty object {} (§2.2)");
        }

        // required_envelope_keys: every key the fixture names must be present.
        const QJsonArray reqKeys = expected.value("required_envelope_keys").toArray();
        for (const QJsonValue& kv : reqKeys) {
            const QString key = kv.toString();
            QVERIFY2(out.contains(key),
                     ctx + ": required key missing from serialized output: " + key.toUtf8());
        }

        // command/event must NEVER carry the three response keys (§2.3).
        if (env->type != QLatin1String("response")) {
            QVERIFY2(!out.contains("success"),
                     ctx + ": non-response must not carry top-level success");
            QVERIFY2(!out.contains("error_code"),
                     ctx + ": non-response must not carry top-level error_code");
            QVERIFY2(!out.contains("error_message"),
                     ctx + ": non-response must not carry top-level error_message");
        }

        // timestamp is preserved as int64 through serialization.
        QCOMPARE(out.value("timestamp").toInteger(), env->timestamp);
    }
}

void EnvelopeTest::testDeserializeFromFixtures()
{
    QJsonObject root;
    loadFixture(QStringLiteral("envelope_deserialize.json"), root);
    const QJsonArray cases = root.value("cases").toArray();
    QVERIFY2(!cases.isEmpty(), "envelope_deserialize.json has no cases");

    for (qsizetype i = 0; i < cases.size(); ++i) {
        const QJsonObject c = cases.at(i).toObject();
        const QString name = c.value("name").toString();
        const QByteArray ctx = "[" + QByteArray::number(i) + "] " + name.toUtf8();

        const QJsonValue input = c.value("input");
        const QJsonObject expected = c.value("expected").toObject();
        const bool expectValid = expected.value("valid").toBool(false);

        // Every case (incl. non-object roots) routes through the document
        // overload — the realistic wire entry point.
        const QJsonDocument doc = valueAsDocument(input);
        const auto env = deserialize(doc);

        if (expectValid) {
            QVERIFY2(env.has_value(),
                     ctx + ": expected valid but deserialize returned nullopt ("
                         + expected.value("reason").toString().toUtf8() + ")");
            QCOMPARE(env->type, expected.value("type").toString());
            if (expected.contains("success"))
                QCOMPARE(env->success, expected.value("success").toBool());
            if (env->type == QLatin1String("response"))
                QVERIFY2(env->hasResponseFields,
                         ctx + ": valid response must expose hasResponseFields");
        } else {
            QVERIFY2(!env.has_value(),
                     ctx + ": expected malformed (nullopt) but parsed successfully — reason: "
                         + expected.value("reason").toString().toUtf8());
        }
    }
}

void EnvelopeTest::testRoundTripCommands()
{
    QJsonObject root;
    loadFixture(QStringLiteral("command_all_20.json"), root);
    const QJsonArray cases = root.value("cases").toArray();
    QCOMPARE(root.value("command_count").toInteger(), cases.size());

    for (qsizetype i = 0; i < cases.size(); ++i) {
        const QJsonObject c = cases.at(i).toObject();
        const QByteArray ctx = "[" + QByteArray::number(i) + "] " + c.value("name").toString().toUtf8();

        const QJsonObject input = c.value("input").toObject();
        const auto first = deserialize(input);
        QVERIFY2(first.has_value(), ctx + ": command must deserialize");
        QVERIFY2(first->type == QLatin1String("command"),
                 ctx + ": round-trip source must be a command");

        // serialize -> deserialize must be identity: the two Envelopes are equal.
        const auto second = deserialize(serialize(*first).object());
        QVERIFY2(second.has_value(), ctx + ": re-deserialized envelope must be valid");
        QVERIFY2(*first == *second,
                 ctx + ": round-trip identity failed (serialize -> deserialize changed the envelope)");
    }
}

void EnvelopeTest::testRoundTripEvents()
{
    QJsonObject root;
    loadFixture(QStringLiteral("event_all_13.json"), root);
    const QJsonArray cases = root.value("cases").toArray();
    // cases.length() > event_count on purpose: motion_finished and stats_state
    // each have two cases (payload variants / platform shapes). Use '>='.
    QVERIFY2(cases.size() >= root.value("event_count").toInteger(),
             "event cases must cover every distinct event at least once");

    for (qsizetype i = 0; i < cases.size(); ++i) {
        const QJsonObject c = cases.at(i).toObject();
        const QByteArray ctx = "[" + QByteArray::number(i) + "] " + c.value("name").toString().toUtf8();

        const QJsonObject input = c.value("input").toObject();
        const auto first = deserialize(input);
        QVERIFY2(first.has_value(), ctx + ": event must deserialize");
        QVERIFY2(first->type == QLatin1String("event"),
                 ctx + ": round-trip source must be an event");

        // Covers both motion_finished variants (group+index / motion_path) and
        // both stats_state shapes (Windows-full / Linux-null) — all must round-trip.
        const auto second = deserialize(serialize(*first).object());
        QVERIFY2(second.has_value(), ctx + ": re-deserialized envelope must be valid");
        QVERIFY2(*first == *second,
                 ctx + ": round-trip identity failed (serialize -> deserialize changed the envelope)");
    }
}

void EnvelopeTest::testTimestampInt64Precision()
{
    // 1710000000000 (2024-03-09 UTC ms) exceeds INT32_MAX (2147483647).
    // toInteger() must preserve it; the forbidden toInt() would truncate.
    const qint64 big = 1710000000000LL;
    QVERIFY2(big > std::numeric_limits<int>::max(),
             "precondition: timestamp must exceed INT32_MAX for this test to mean anything");

    Envelope env;
    env.type = QLatin1String("command");
    env.action = QLatin1String("ping");
    env.id = QStringLiteral("cmd-ts-0001");
    env.payload = QJsonObject{};
    env.timestamp = big;

    const QJsonObject out = serialize(env).object();
    QCOMPARE(out.value("timestamp").toInteger(), big);

    const auto back = deserialize(out);
    QVERIFY(back.has_value());
    QCOMPARE(back->timestamp, big);

    // Negative int64 (dates before 1970, or signed edge) must also survive.
    env.timestamp = -1;
    QCOMPARE(deserialize(serialize(env).object())->timestamp, qint64(-1));
}

void EnvelopeTest::testResponseFieldsAtTopLevel()
{
    // A response: payload must be {} and the three response fields sit at the
    // JSON root, NOT inside payload (interface.md §2.2).
    const auto env = createResponse(QStringLiteral("cmd-x-0001"),
                                    QStringLiteral("set_size"),
                                    /*success=*/false,
                                    /*errorCode=*/4004,
                                    QStringLiteral("width/height must be positive"));
    QCOMPARE(env.type, QStringLiteral("response"));
    QVERIFY(env.hasResponseFields);
    QVERIFY(env.payload.isEmpty());

    const QJsonObject out = serialize(env).object();
    QVERIFY(out.contains("success"));
    QVERIFY(out.contains("error_code"));
    QVERIFY(out.contains("error_message"));
    QCOMPARE(out.value("success").toBool(), false);
    QCOMPARE(out.value("error_code").toInteger(), qint64(4004));
    QCOMPARE(out.value("error_message").toString(),
             QStringLiteral("width/height must be positive"));
    QVERIFY2(out.value("payload").isObject(),
             "response payload must still be a JSON object");
    QVERIFY2(out.value("payload").toObject().isEmpty(),
             "response payload must be {} (§2.2)");
    QVERIFY2(!env.payload.contains("success"),
             "response fields must never live inside the Envelope payload");

    // Symmetric: a command and an event must carry NONE of the three keys.
    const QJsonObject cmdOut = serialize(createCommand(QStringLiteral("stop_motion"))).object();
    QVERIFY(!cmdOut.contains("success"));
    QVERIFY(!cmdOut.contains("error_code"));
    QVERIFY(!cmdOut.contains("error_message"));

    const QJsonObject evtOut = serialize(createEvent(QStringLiteral("ready"))).object();
    QVERIFY(!evtOut.contains("success"));
    QVERIFY(!evtOut.contains("error_code"));
    QVERIFY(!evtOut.contains("error_message"));

    // A response missing any response field must be dropped on deserialize (§2.4).
    QJsonObject stripped = out;
    stripped.remove("error_message");
    QVERIFY2(!deserialize(stripped).has_value(),
             "response missing error_message must deserialize to nullopt");
}

void EnvelopeTest::testObjectOverloadDeserialize()
{
    // The QJsonObject overload is the direct parser entry; exercise it apart
    // from the document overload path used by the fixture loop.
    QJsonObject obj;
    obj.insert("type", QLatin1String("command"));
    obj.insert("action", QLatin1String("shutdown"));
    obj.insert("id", QStringLiteral("cmd-obj-0001"));
    obj.insert("payload", QJsonObject{});
    obj.insert("timestamp", qint64(1710000000000LL));

    const auto env = deserialize(obj);
    QVERIFY(env.has_value());
    QCOMPARE(env->action, QStringLiteral("shutdown"));
    QCOMPARE(env->timestamp, qint64(1710000000000LL));

    // Non-object documents (array / string roots) are silently dropped.
    QVERIFY2(!deserialize(QJsonDocument(QJsonArray{1, 2, 3})).has_value(),
             "array-root JSON must be rejected");
    QVERIFY2(!deserialize(QJsonDocument::fromJson(QByteArrayLiteral(R"("not an object")"))).has_value(),
             "string-root JSON must be rejected");
}

void EnvelopeTest::testCreateHelpersAreWellFormed()
{
    const QJsonObject payload{{"k", QStringLiteral("v")}};
    const Envelope cmd = createCommand(QStringLiteral("load_model"), payload);
    QCOMPARE(cmd.type, QStringLiteral("command"));
    QCOMPARE(cmd.action, QStringLiteral("load_model"));
    QVERIFY2(!cmd.id.isEmpty(), "createCommand must assign a non-empty id");
    QCOMPARE(cmd.payload, payload);
    QVERIFY2(!cmd.hasResponseFields, "commands never carry response fields");
    QVERIFY2(deserialize(serialize(cmd).object()).has_value(),
             "createCommand output must round-trip");

    const Envelope evt = createEvent(QStringLiteral("ready"));
    QCOMPARE(evt.type, QStringLiteral("event"));
    QVERIFY2(!evt.id.isEmpty(), "createEvent must assign a non-empty id");
    QVERIFY(evt.payload.isEmpty());

    const Envelope resp = createResponse(QStringLiteral("orig-id"),
                                         QStringLiteral("load_model"), true);
    QCOMPARE(resp.type, QStringLiteral("response"));
    QCOMPARE(resp.id, QStringLiteral("orig-id")); // response reuses the original id
    QVERIFY(resp.hasResponseFields);
    QVERIFY2(resp.payload.isEmpty(), "createResponse payload must be {}");
}

QTEST_APPLESS_MAIN(EnvelopeTest)
#include "EnvelopeTest.moc"
