// InteractionHandlerTest (Phase 5, todo 9): the hit→play_motion translator.
//
// Six slots locking the 3-tier case-folding lookup (interface.md §F):
//   1. testHitHeadDefault     — empty modelConfig, hit{head} → play_motion
//                               {group:TapHead, index:0, priority:2}.
//   2. testHitBodyDefault     — empty modelConfig, hit{body} → play_motion
//                               {group:TapBody, priority:2}.
//   3. testCaseFoldCapital    — modelConfig.hitActions["Head"]={CustomHead,3},
//                               hit{area_id:"head"} (lowercase) → matches via
//                               the capitalized tier (tier 2): "head"→"Head"
//                               → CustomHead, priority 3.
//   4. testExactMatchWins     — modelConfig.hitActions["head"]={ExactGroup,5},
//                               hit{area_id:"head"} → tier 1 exact match wins
//                               over the default TapHead.
//   5. testUnknownArea        — hit{area_id:"unknown"} → no command sent.
//   6. testEmptyAndNullPayload — hit with missing area_id AND hit with empty
//                               payload {} → both no-op, no crash.
//
// Harness: a capturing messageSender (QStringList) collects every outbound
// JSON. Each slot constructs an Envelope directly (no WsServer needed —
// handleHitEvent takes a const Envelope&), invokes handleHitEvent, then
// asserts on captured[0] (parsed back via QJsonDocument so the assertions
// are on structure, not on the exact JSON formatting).
//
// Recompiles InteractionHandler.cpp + Envelope.cpp directly (same pattern as
// the other core tests — no shared lib yet). Links Qt6::Core + Qt6::Test +
// spdlog (LOG_* macros). QTEST_APPLESS_MAIN — pure logic, no event loop.

#include "core/InteractionHandler.hpp"
#include "network/Envelope.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QSignalSpy>

class InteractionHandlerTest : public QObject {
    Q_OBJECT

private:
    // Build a hit-event Envelope with the given area_id. action="hit" matches
    // the protocol event name; type="event" (renderer→controller direction).
    static Envelope makeHitEvent(const QString& areaId)
    {
        QJsonObject payload;
        if (!areaId.isNull()) {
            payload.insert(QStringLiteral("area_id"), areaId);
        }
        return createEvent(QStringLiteral("hit"), payload);
    }

    // Build a hit-event Envelope with an empty payload {}. Used by the null-
    // payload test to prove handleHitEvent does not crash on `{}`.
    static Envelope makeHitEventEmptyPayload()
    {
        return createEvent(QStringLiteral("hit"), QJsonObject{});
    }

private slots:
    // Default head mapping: no modelConfig set → hit{head} → play_motion
    // {group:TapHead, index:0, priority:2}.
    void testHitHeadDefault()
    {
        QStringList captured;
        InteractionHandler handler([&captured](const QString& json) {
            captured.append(json);
        });

        handler.handleHitEvent(makeHitEvent(QStringLiteral("head")));

        QCOMPARE(captured.size(), 1);
        const auto doc = QJsonDocument::fromJson(captured[0].toUtf8());
        QVERIFY(doc.isObject());
        const QJsonObject obj = doc.object();
        QCOMPARE(obj.value("type").toString(), QStringLiteral("command"));
        QCOMPARE(obj.value("action").toString(), QStringLiteral("play_motion"));
        const QJsonObject payload = obj.value("payload").toObject();
        QCOMPARE(payload.value("group").toString(), QStringLiteral("TapHead"));
        QCOMPARE(payload.value("index").toInt(), 0);
        QCOMPARE(payload.value("priority").toInt(), 2);
    }

    // Default body mapping: no modelConfig set → hit{body} → play_motion
    // {group:TapBody, priority:2}.
    void testHitBodyDefault()
    {
        QStringList captured;
        InteractionHandler handler([&captured](const QString& json) {
            captured.append(json);
        });

        handler.handleHitEvent(makeHitEvent(QStringLiteral("body")));

        QCOMPARE(captured.size(), 1);
        const auto doc = QJsonDocument::fromJson(captured[0].toUtf8());
        QVERIFY(doc.isObject());
        const QJsonObject obj = doc.object();
        QCOMPARE(obj.value("action").toString(), QStringLiteral("play_motion"));
        const QJsonObject payload = obj.value("payload").toObject();
        QCOMPARE(payload.value("group").toString(), QStringLiteral("TapBody"));
        QCOMPARE(payload.value("priority").toInt(), 2);
    }

    // Case-fold tier 2: modelConfig has "Head" (capitalized), renderer sends
    // "head" (lowercase). capitalize("head") = "Head" → match.
    void testCaseFoldCapital()
    {
        QStringList captured;
        InteractionHandler handler([&captured](const QString& json) {
            captured.append(json);
        });

        ModelConfig config;
        config.hitActions.insert(QStringLiteral("Head"),
                                 HitAction{QStringLiteral("CustomHead"), 3});
        handler.setModelConfig(config);

        handler.handleHitEvent(makeHitEvent(QStringLiteral("head")));

        QCOMPARE(captured.size(), 1);
        const auto doc = QJsonDocument::fromJson(captured[0].toUtf8());
        const QJsonObject payload = doc.object().value("payload").toObject();
        QCOMPARE(payload.value("group").toString(), QStringLiteral("CustomHead"));
        QCOMPARE(payload.value("priority").toInt(), 3);
    }

    // Tier 1 (exact) wins over tier 3 (default). modelConfig has "head"
    // (lowercase, same as the wire value) → exact match, NOT the default
    // TapHead. Proves the precedence: exact > capitalized > default.
    void testExactMatchWins()
    {
        QStringList captured;
        InteractionHandler handler([&captured](const QString& json) {
            captured.append(json);
        });

        ModelConfig config;
        config.hitActions.insert(QStringLiteral("head"),
                                 HitAction{QStringLiteral("ExactGroup"), 5});
        handler.setModelConfig(config);

        handler.handleHitEvent(makeHitEvent(QStringLiteral("head")));

        QCOMPARE(captured.size(), 1);
        const auto doc = QJsonDocument::fromJson(captured[0].toUtf8());
        const QJsonObject payload = doc.object().value("payload").toObject();
        QCOMPARE(payload.value("group").toString(), QStringLiteral("ExactGroup"));
        QCOMPARE(payload.value("priority").toInt(), 5);
    }

    // Unknown area_id → no command sent (messageSender never invoked).
    // Debug log, no crash.
    void testUnknownArea()
    {
        QStringList captured;
        InteractionHandler handler([&captured](const QString& json) {
            captured.append(json);
        });

        handler.handleHitEvent(makeHitEvent(QStringLiteral("unknown")));

        QCOMPARE(captured.size(), 0);
    }

    // Missing area_id AND empty payload {} → both no-op, no crash. Two
    // sub-cases in one slot because they exercise the same guard
    // (extractAreaId returning empty) with slightly different payload shapes.
    void testEmptyAndNullPayload()
    {
        QStringList captured;
        InteractionHandler handler([&captured](const QString& json) {
            captured.append(json);
        });

        // Sub-case 1: hit event with a payload but NO area_id field.
        handler.handleHitEvent(makeHitEvent(QString()));
        QCOMPARE(captured.size(), 0);

        // Sub-case 2: hit event with an entirely empty payload {}.
        handler.handleHitEvent(makeHitEventEmptyPayload());
        QCOMPARE(captured.size(), 0);
    }
};

QTEST_APPLESS_MAIN(InteractionHandlerTest)
#include "InteractionHandlerTest.moc"
