// ProtocolFixturesTest — loads the shared protocol JSON fixtures and asserts the
// structural invariants every later task (T4 Envelope ser/deser, T7 router,
// T8 handshake factory, T9 message factory, T10/T11 event dispatch) depends on.
//
// The fixtures live in controller_qt/tests/protocol/. Their absolute source
// path is baked in at compile time via PROTOCOL_FIXTURES_DIR (see
// tests/CMakeLists.txt), so the binary finds them from any working directory.
//
// This is a pure JSON-parsing test (no GUI), hence QTEST_APPLESS_MAIN.
//
// Authoritative spec: docs/protocol/interface.md
//   §1.3  color format AABBGGRR (uint32 decimal)
//   §1.5  payload is ALWAYS an object (never null, never omitted)
//   §2.1  command/event envelope
//   §2.2  response envelope (success/error_code/error_message at TOP LEVEL)
//   §2.4  deserialization rules (missing field / bad JSON / null payload → drop)
//   §3.1  25 commands   §4.1  13 events
//
// allow: SIZE_OK — one cohesive QTest class (single SUT: protocol fixture
// validation). The isEnvelopeValid helpers below are a temporary reference
// implementation; T4 introduces the real Envelope ser/deser class, after which
// they are deleted. Splitting them into a header now is speculative work that
// T4 immediately obsoletes, so the whole test is kept in this one TU.

#include <QtTest>

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonParseError>
#include <QtCore/QSet>
#include <QtCore/QStringList>

Q_NAMESPACE

namespace {

// Returns the absolute path to the protocol/ fixture directory. The macro is
// defined at compile time (tests/CMakeLists.txt → target_compile_definitions).
QString fixturesDir()
{
    return QString::fromUtf8(PROTOCOL_FIXTURES_DIR);
}

// Loads a JSON fixture file as a top-level object. Fails the calling test if
// the file is missing, unparseable, or not a JSON object.
QJsonObject loadFixtureObject(const QString &fileName)
{
    const QString path = QDir(fixturesDir()).filePath(fileName);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QTest::qFail(qPrintable(QStringLiteral("Could not open fixture: %1").arg(path)),
                     __FILE__, __LINE__);
        return {};
    }
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        QTest::qFail(qPrintable(QStringLiteral("JSON parse error in %1: %2")
                                    .arg(path, parseError.errorString())),
                     __FILE__, __LINE__);
        return {};
    }
    if (!doc.isObject()) {
        QTest::qFail(qPrintable(QStringLiteral("Fixture %1 is not a JSON object").arg(path)),
                     __FILE__, __LINE__);
        return {};
    }
    return doc.object();
}

// Loads a fixture that uses the standard { description, source, cases[] } shape
// and returns the cases array. Fails the calling test if the structure is wrong.
QJsonArray loadFixtureCases(const QString &fileName)
{
    const QJsonObject doc = loadFixtureObject(fileName);
    const QJsonValue casesVal = doc.value("cases");
    if (!casesVal.isArray()) {
        QTest::qFail(qPrintable(QStringLiteral("Fixture %1 missing 'cases' array").arg(fileName)),
                     __FILE__, __LINE__);
        return {};
    }
    return casesVal.toArray();
}

// Validates a single WebSocket envelope against interface.md §2.4.
// Returns false (== "silently drop") for: non-object, missing/wrong type,
// missing any of action/id/payload/timestamp, empty id, payload not an object,
// non-integer timestamp, or response missing success/error_code/error_message.
bool isEnvelopeValid(const QJsonObject &env)
{
    if (env.isEmpty()) {
        return false;
    }
    const QJsonValue typeVal = env.value("type");
    if (!typeVal.isString()) {
        return false;
    }
    const QString type = typeVal.toString();
    if (type != QLatin1String("command")
        && type != QLatin1String("event")
        && type != QLatin1String("response")) {
        return false;
    }
    // Required fields present.
    if (!env.contains("action") || !env.contains("id")
        || !env.contains("payload") || !env.contains("timestamp")) {
        return false;
    }
    // action: non-empty string.
    const QJsonValue actionVal = env.value("action");
    if (!actionVal.isString() || actionVal.toString().isEmpty()) {
        return false;
    }
    // id: non-empty string (≤ 64 chars is a recommendation, not enforced here).
    const QJsonValue idVal = env.value("id");
    if (!idVal.isString() || idVal.toString().isEmpty()) {
        return false;
    }
    // payload: MUST be a JSON object — never null/omitted (§1.5).
    // QJsonValue::isObject() is false for JSON null, so payload:null is rejected.
    if (!env.value("payload").isObject()) {
        return false;
    }
    // timestamp: integer Unix milliseconds. JSON numbers all parse to Double.
    if (!env.value("timestamp").isDouble()) {
        return false;
    }
    // Response-only fields live at the TOP LEVEL, never inside payload (§2.2).
    if (type == QLatin1String("response")) {
        if (!env.contains("success") || !env.contains("error_code")
            || !env.contains("error_message")) {
            return false;
        }
    }
    return true;
}

// Validates an envelope coming from a QJsonValue (handles non-object inputs like
// bare arrays/strings/numbers which must be dropped per §2.4).
bool inputValueIsValidEnvelope(const QJsonValue &input)
{
    if (!input.isObject()) {
        return false;
    }
    return isEnvelopeValid(input.toObject());
}

// Validates the common fixture-document shape: { description, cases[] } where
// each case has name + input. Used by the failure-path test slot.
bool fixtureDocHasValidStructure(const QJsonObject &doc)
{
    if (!doc.contains("description") || !doc.contains("cases")) {
        return false;
    }
    const QJsonValue casesVal = doc.value("cases");
    if (!casesVal.isArray()) {
        return false;
    }
    const QJsonArray cases = casesVal.toArray();
    if (cases.isEmpty()) {
        return false;
    }
    for (const QJsonValue &c : cases) {
        if (!c.isObject()) {
            return false;
        }
        const QJsonObject co = c.toObject();
        if (!co.contains("name") || !co.contains("input")) {
            return false;
        }
    }
    return true;
}

const QSet<QString> kExpectedResponseFields = {
    QStringLiteral("success"),
    QStringLiteral("error_code"),
    QStringLiteral("error_message"),
};

} // namespace

class ProtocolFixturesTest : public QObject
{
    Q_OBJECT

private slots:
    // Each of the 5 fixture files parses as valid JSON with the expected shape.
    void testEnvelopeSerializeFixture();
    void testEnvelopeDeserializeFixture();
    void testHandshakeFlowFixture();
    void testCommandAll20Fixture();
    void testEventAll13Fixture();

    // Failure path: the validators must REJECT deliberately malformed inputs.
    void testValidatorRejectsMalformedFixture();

private:
    // Collects every distinct action across a cases array.
    static QSet<QString> collectActions(const QJsonArray &cases)
    {
        QSet<QString> actions;
        for (const QJsonValue &c : cases) {
            const QJsonObject env = c.toObject().value("input").toObject();
            actions.insert(env.value("action").toString());
        }
        return actions;
    }
};

// envelope_serialize.json — each input must be a valid envelope, and the
// response-fields invariant from §2.2/§2.3 must hold (top-level only on
// response, payload always an object — empty {} for responses).
void ProtocolFixturesTest::testEnvelopeSerializeFixture()
{
    const QJsonObject doc = loadFixtureObject(QStringLiteral("envelope_serialize.json"));
    QVERIFY(!doc.isEmpty());
    QVERIFY(fixtureDocHasValidStructure(doc));

    const QJsonArray cases = doc.value("cases").toArray();
    QVERIFY2(cases.size() >= 3, "serialize fixture should cover command/event/response");

    bool sawCommand = false, sawEvent = false, sawResponse = false;
    for (const QJsonValue &c : cases) {
        const QJsonObject co = c.toObject();
        const QJsonObject env = co.value("input").toObject();
        QVERIFY2(inputValueIsValidEnvelope(env),
                 qPrintable(QStringLiteral("case '%1' input is not a valid envelope")
                                .arg(co.value("name").toString())));
        const QString type = env.value("type").toString();
        // §1.5: payload is always an object; empty {} is valid (commands) and
        // required-empty (responses).
        const QJsonObject payload = env.value("payload").toObject();
        if (type == QLatin1String("response")) {
            sawResponse = true;
            // §2.2: response fields at TOP LEVEL, payload is empty object.
            QVERIFY(env.contains("success"));
            QVERIFY(env.contains("error_code"));
            QVERIFY(env.contains("error_message"));
            QVERIFY2(payload.isEmpty(),
                     "response payload MUST be the empty object {} (§2.2)");
        } else if (type == QLatin1String("command")) {
            sawCommand = true;
            // §2.3: success/error_code/error_message written ONLY on response.
            for (const QString &f : kExpectedResponseFields) {
                QVERIFY2(!env.contains(f),
                         qPrintable(QStringLiteral("command must not carry '%1'").arg(f)));
            }
        } else if (type == QLatin1String("event")) {
            sawEvent = true;
            for (const QString &f : kExpectedResponseFields) {
                QVERIFY2(!env.contains(f),
                         qPrintable(QStringLiteral("event must not carry '%1'").arg(f)));
            }
        }
    }
    QVERIFY2(sawCommand, "no command case present");
    QVERIFY2(sawEvent, "no event case present");
    QVERIFY2(sawResponse, "no response case present");
}

// envelope_deserialize.json — valid:true cases must pass isEnvelopeValid;
// valid:false cases must FAIL it (silently dropped). This ties the fixture to
// the real §2.4 deserialization contract.
void ProtocolFixturesTest::testEnvelopeDeserializeFixture()
{
    const QJsonArray cases = loadFixtureCases(QStringLiteral("envelope_deserialize.json"));
    QVERIFY2(!cases.isEmpty(), "deserialize fixture has no cases");

    int validCount = 0, invalidCount = 0;
    for (const QJsonValue &c : cases) {
        const QJsonObject co = c.toObject();
        const QJsonValue input = co.value("input");
        const QJsonObject expected = co.value("expected").toObject();
        const bool expectValid = expected.value("valid").toBool();
        const bool actuallyValid = inputValueIsValidEnvelope(input);
        if (expectValid) {
            ++validCount;
            QVERIFY2(actuallyValid,
                     qPrintable(QStringLiteral("case '%1' expected valid but validator rejected it")
                                    .arg(co.value("name").toString())));
        } else {
            ++invalidCount;
            QVERIFY2(!actuallyValid,
                     qPrintable(QStringLiteral("case '%1' expected invalid (drop) but validator "
                                               "accepted it — reason in fixture: %2")
                                    .arg(co.value("name").toString(),
                                         expected.value("reason").toString())));
        }
    }
    QVERIFY2(validCount >= 3, "need >=3 valid round-trip cases");
    QVERIFY2(invalidCount >= 4, "need >=4 malformed (drop) cases per §2.4");
}

// handshake_flow.json — events[] is an ordered sequence; each message is a
// valid envelope, the sequence starts with a ready event, contains the 7
// startup-volley commands, and includes a model_loaded event.
void ProtocolFixturesTest::testHandshakeFlowFixture()
{
    const QJsonObject doc = loadFixtureObject(QStringLiteral("handshake_flow.json"));
    QVERIFY(!doc.isEmpty());
    QVERIFY(doc.contains("description"));

    const QJsonValue eventsVal = doc.value("events");
    QVERIFY2(eventsVal.isArray(), "handshake_flow uses an 'events' array, not 'cases'");
    const QJsonArray events = eventsVal.toArray();
    QVERIFY2(events.size() >= 4, "handshake sequence too short");

    // Every message in the sequence must be a valid envelope.
    for (const QJsonValue &e : events) {
        const QJsonObject step = e.toObject();
        const QJsonObject msg = step.value("message").toObject();
        QVERIFY2(isEnvelopeValid(msg),
                 qPrintable(QStringLiteral("step %1 message is not a valid envelope")
                                .arg(step.value("step").toInt())));
    }

    // First renderer→controller message is the ready event.
    const QJsonObject firstMsg = events.first().toObject().value("message").toObject();
    QCOMPARE(firstMsg.value("type").toString(), QStringLiteral("event"));
    QCOMPARE(firstMsg.value("action").toString(), QStringLiteral("ready"));

    // Collect the startup-volley commands in order (controller→renderer, before
    // model_loaded).
    QStringList volley;
    bool seenModelLoaded = false;
    for (const QJsonValue &e : events) {
        const QJsonObject msg = e.toObject().value("message").toObject();
        if (msg.value("type").toString() == QLatin1String("event")
            && msg.value("action").toString() == QLatin1String("model_loaded")) {
            seenModelLoaded = true;
            break;
        }
        if (msg.value("type").toString() == QLatin1String("command")) {
            volley.append(msg.value("action").toString());
        }
    }
    QVERIFY2(seenModelLoaded, "handshake must include a model_loaded event");
    QVERIFY2(volley.size() >= 7, "startup volley should have >=7 commands");
    QCOMPARE(volley.first(), QStringLiteral("load_model"));

    // Cross-check the declared assertions block.
    const QJsonObject assertions = doc.value("assertions").toObject();
    QVERIFY(assertions.value("starts_with_ready_event").toBool());
}

// command_all_20.json — exactly 20 commands (the Qt controller's catalog; the
// 5 subtitle commands were removed in the bubble-stream switch), all
// type==command, all payloads objects, unique actions.
void ProtocolFixturesTest::testCommandAll20Fixture()
{
    const QJsonObject doc = loadFixtureObject(QStringLiteral("command_all_20.json"));
    QVERIFY(!doc.isEmpty());

    QCOMPARE(doc.value("command_count").toInt(), 20);

    const QJsonArray cases = doc.value("cases").toArray();
    QCOMPARE(cases.size(), 20);

    QSet<QString> actions;
    for (const QJsonValue &c : cases) {
        const QJsonObject co = c.toObject();
        const QJsonObject env = co.value("input").toObject();
        QVERIFY2(inputValueIsValidEnvelope(env),
                 qPrintable(QStringLiteral("command case '%1' is not a valid envelope")
                                .arg(co.value("name").toString())));
        QCOMPARE(env.value("type").toString(), QStringLiteral("command"));
        QVERIFY2(env.value("payload").isObject(),
                 qPrintable(QStringLiteral("command '%1' payload is not an object")
                                .arg(co.value("name").toString())));
        const QString action = env.value("action").toString();
        QVERIFY2(!action.isEmpty(), "command has empty action");
        QVERIFY2(!actions.contains(action),
                 qPrintable(QStringLiteral("duplicate command action: %1").arg(action)));
        actions.insert(action);
    }
    QCOMPARE(actions.size(), 20);

    // Cross-check against the fixture's self-declared expected_action_set.
    const QJsonArray declared = doc.value("expected_action_set").toArray();
    QCOMPARE(declared.size(), 20);
    QSet<QString> declaredSet;
    for (const QJsonValue &a : declared) {
        declaredSet.insert(a.toString());
    }
    QCOMPARE(actions, declaredSet);

    // No subtitle command may appear in the Qt controller's catalog.
    for (const QString &a : std::as_const(actions)) {
        QVERIFY2(!a.contains(QLatin1String("subtitle")),
                 qPrintable(QStringLiteral("subtitle command leaked into catalog: %1").arg(a)));
    }

    // Spot-check a few load-bearing details from §5 so a later refactor cannot
    // silently drop fields without a fixture-test failure.
    for (const QJsonValue &c : cases) {
        const QJsonObject co = c.toObject();
        const QJsonObject env = co.value("input").toObject();
        const QString action = env.value("action").toString();
        const QJsonObject payload = env.value("payload").toObject();
        if (action == QLatin1String("load_model")) {
            QVERIFY(payload.contains("model_path"));
        } else if (action == QLatin1String("set_hit_areas")) {
            QVERIFY2(payload.value("hit_areas").isArray(),
                     "set_hit_areas payload must have hit_areas array");
        } else if (action == QLatin1String("play_motion_ext")) {
            QVERIFY(payload.contains("motion_path"));
        }
    }
}

// event_all_13.json — exactly 13 distinct event actions; motion_finished has
// BOTH variants (group/index AND motion_path); stats_state has a case with
// null GPU fields; layout_state demonstrates by-action routing.
void ProtocolFixturesTest::testEventAll13Fixture()
{
    const QJsonObject doc = loadFixtureObject(QStringLiteral("event_all_13.json"));
    QVERIFY(!doc.isEmpty());

    QCOMPARE(doc.value("event_count").toInt(), 13);

    const QJsonArray cases = doc.value("cases").toArray();
    QVERIFY2(cases.size() >= 13, "need at least 13 event cases (motion_finished x2, stats_state x2)");

    // Every case is a valid event envelope with an object payload.
    for (const QJsonValue &c : cases) {
        const QJsonObject co = c.toObject();
        const QJsonObject env = co.value("input").toObject();
        QVERIFY2(inputValueIsValidEnvelope(env),
                 qPrintable(QStringLiteral("event case '%1' is not a valid envelope")
                                .arg(co.value("name").toString())));
        QCOMPARE(env.value("type").toString(), QStringLiteral("event"));
        QVERIFY2(env.value("payload").isObject(),
                 qPrintable(QStringLiteral("event '%1' payload is not an object")
                                .arg(co.value("name").toString())));
    }

    const QSet<QString> actions = collectActions(cases);
    QCOMPARE(actions.size(), 13);

    // motion_finished: exactly 2 cases — variant A (group+index, no motion_path)
    // and variant B (motion_path). Per §4.2 the discriminator is motion_path.
    int motionFinishedGroupVariant = 0;
    int motionFinishedPathVariant = 0;
    int statsStateCases = 0;
    int statsStateNullGpu = 0;
    bool sawLayoutState = false;
    for (const QJsonValue &c : cases) {
        const QJsonObject co = c.toObject();
        const QJsonObject env = co.value("input").toObject();
        const QString action = env.value("action").toString();
        const QJsonObject payload = env.value("payload").toObject();
        if (action == QLatin1String("motion_finished")) {
            if (payload.contains("motion_path")) {
                ++motionFinishedPathVariant;
            } else if (payload.contains("group") && payload.contains("index")) {
                ++motionFinishedGroupVariant;
            }
        } else if (action == QLatin1String("stats_state")) {
            ++statsStateCases;
            // §6 K: gpu_percent/gpu_name/vram_used_bytes/vram_total_bytes are
            // nullable. At least one case must carry explicit nulls (Linux stub).
            const bool hasNullGpu = payload.value("gpu_name").isNull()
                || payload.value("gpu_percent").isNull();
            if (hasNullGpu) {
                ++statsStateNullGpu;
            }
            // Always-present fields.
            QVERIFY(payload.contains("cpu_percent"));
            QVERIFY(payload.contains("rss_bytes"));
            QVERIFY(payload.contains("timestamp_ms"));
        } else if (action == QLatin1String("layout_state")) {
            sawLayoutState = true;
            QVERIFY(payload.contains("offset_x"));
            QVERIFY(payload.contains("offset_y"));
            QVERIFY(payload.contains("scale"));
        }
    }
    QCOMPARE(motionFinishedGroupVariant, 1);
    QCOMPARE(motionFinishedPathVariant, 1);
    QVERIFY2(statsStateCases >= 2, "stats_state needs >=2 cases (windows + linux-stub)");
    QVERIFY2(statsStateNullGpu >= 1, "no stats_state case with null GPU fields (Linux stub)");
    QVERIFY2(sawLayoutState, "no layout_state case (pseudo-response, §7.3)");

    // Cross-check the declared action set.
    const QJsonArray declared = doc.value("expected_action_set").toArray();
    QCOMPARE(declared.size(), 13);
}

// Failure path — the validators must REJECT malformed input. Proves the test
// harness would catch a deliberately corrupted fixture, not just accept
// everything. (Satisfies "deliberate schema violation in a temp copy is flagged".)
void ProtocolFixturesTest::testValidatorRejectsMalformedFixture()
{
    // (1) A fixture document missing the required 'cases' array is rejected.
    QJsonObject badDocMissingCases;
    badDocMissingCases.insert("description", "no cases key");
    QVERIFY(!fixtureDocHasValidStructure(badDocMissingCases));

    // (2) A fixture document whose 'cases' is not an array is rejected.
    QJsonObject badDocCasesNotArray;
    badDocCasesNotArray.insert("description", "cases is a string");
    badDocCasesNotArray.insert("cases", QStringLiteral("not-an-array"));
    QVERIFY(!fixtureDocHasValidStructure(badDocCasesNotArray));

    // (3) A case missing 'name' is rejected.
    QJsonObject badCase;
    badCase.insert("input", QJsonObject{});
    QJsonArray arr;
    arr.append(badCase);
    QJsonObject badDocCaseNoName;
    badDocCaseNoName.insert("description", "case missing name");
    badDocCaseNoName.insert("cases", arr);
    QVERIFY(!fixtureDocHasValidStructure(badDocCaseNoName));

    // (4) Envelope validators reject each §2.4 malformed shape directly.
    QVERIFY(!isEnvelopeValid(QJsonObject{})); // empty
    QVERIFY(!inputValueIsValidEnvelope(QJsonValue(true))); // not an object
    QVERIFY(!inputValueIsValidEnvelope(QJsonArray{1, 2, 3})); // bare array
    QVERIFY(!inputValueIsValidEnvelope(QStringLiteral("a string"))); // bare string

    QJsonObject missingId;
    missingId.insert("type", "command");
    missingId.insert("action", "play_motion");
    missingId.insert("payload", QJsonObject{});
    missingId.insert("timestamp", qint64(1710000000000));
    QVERIFY(!isEnvelopeValid(missingId)); // missing required field 'id'

    QJsonObject nullPayload;
    nullPayload.insert("type", "command");
    nullPayload.insert("action", "play_motion");
    nullPayload.insert("id", "x");
    nullPayload.insert("payload", QJsonValue::Null); // payload:null — §1.5 rejects
    nullPayload.insert("timestamp", qint64(1710000000000));
    QVERIFY(!isEnvelopeValid(nullPayload));

    QJsonObject wrongType;
    wrongType.insert("type", "notification"); // not command/event/response
    wrongType.insert("action", "play_motion");
    wrongType.insert("id", "x");
    wrongType.insert("payload", QJsonObject{});
    wrongType.insert("timestamp", qint64(1710000000000));
    QVERIFY(!isEnvelopeValid(wrongType));

    QJsonObject responseMissingTopLevelFields;
    responseMissingTopLevelFields.insert("type", "response");
    responseMissingTopLevelFields.insert("action", "load_model");
    responseMissingTopLevelFields.insert("id", "x");
    responseMissingTopLevelFields.insert("payload", QJsonObject{});
    responseMissingTopLevelFields.insert("timestamp", qint64(1710000000000));
    // §2.2: success/error_code/error_message MUST be at top level — absent here.
    QVERIFY(!isEnvelopeValid(responseMissingTopLevelFields));

    // (5) Load a real fixture, corrupt a COPY of one envelope, and confirm the
    // validator flags it. This mirrors "a temp copy with a schema violation".
    const QJsonArray cases = loadFixtureCases(QStringLiteral("envelope_serialize.json"));
    QVERIFY(!cases.isEmpty());
    const QJsonObject firstEnv = cases.first().toObject().value("input").toObject();
    QVERIFY(isEnvelopeValid(firstEnv)); // sanity: original is valid
    QJsonObject corrupted = firstEnv;
    corrupted.remove("id"); // introduce a schema violation
    QVERIFY2(!isEnvelopeValid(corrupted),
             "validator failed to flag a corrupted envelope (removed 'id')");
}

QTEST_APPLESS_MAIN(ProtocolFixturesTest)

#include "ProtocolFixturesTest.moc"
