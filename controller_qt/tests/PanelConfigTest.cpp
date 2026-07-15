// allow: SIZE_OK — single cohesive QTest SUT for the PanelConfig ser/deser
// model. Splitting the private slots across executables would fragment one
// logical contract (the 12-field round-trip + defaults + instance_ids array +
// snake_case + garbage invariants); kept as one class by intent.
//
// PanelConfigTest — TDD for the PanelConfig data model (T19).
//
// Six behaviors locked (mirrors the InstanceConfigTest structure from T18):
//   1. Round-trip identity: toJson -> fromJson preserves all 12 fields,
//      including the instanceIds QStringList.
//   2. Empty {} -> all defaults (blueprint §5.2).
//   3. instance_ids JSON array -> QStringList, order preserved.
//   4. snake_case key mapping (close_action, auto_launch_system, ...).
//   5. Garbage / empty / mistyped JSON -> default-constructed PanelConfig,
//      no throw.
//   6. toJson emits exactly 12 keys (one per field).
//
// QTEST_APPLESS_MAIN: pure JSON struct manipulation, no event loop needed.
//
// NOTE on field count: the task narrative says "11 fields" in places, but the
// task's own struct definition, blueprint §5.2, and the Java PanelConfig record
// each define exactly 12 fields. The key-count test therefore asserts 12 — the
// real, ground-truth count.

#include "core/PanelConfig.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QTest>

namespace {

// Number of fields / serialized keys. Every ground-truth source (blueprint
// §5.2, the Java record, this struct's own definition) agrees on 12.
constexpr int kFieldCount = 12;

// Build a config where EVERY field carries a distinctive, non-default value.
// Used by the round-trip test so a single missed field surfaces immediately.
// Values are deliberately different from the struct defaults AND from each
// other so a swap/mis-wire in toJson/fromJson cannot silently pass.
PanelConfig makeFullyPopulated()
{
    PanelConfig c;
    c.panelX = 50;
    c.panelY = 100;
    c.panelWidth = 800;
    c.panelHeight = 600;
    c.theme = QStringLiteral("赛博霓虹");
    c.fontSize = 16;
    c.panelOpacity = 0.85;
    c.instanceIds = QStringList{QStringLiteral("uuid-aaa"), QStringLiteral("uuid-bbb")};
    c.autoLaunchSystem = true;
    c.startMinimized = true;
    c.closeAction = QStringLiteral("minimize");
    c.confirmOnExit = true;
    return c;
}

} // namespace

class PanelConfigTest : public QObject
{
    Q_OBJECT

private slots:
    void testRoundTripAllFields();
    void testEmptyObjectReturnsDefaults();
    void testInstanceIdsArrayToStringList();
    void testSnakeCaseMapping();
    void testGarbageJsonReturnsDefaults();
    void testKeyCount();
};

void PanelConfigTest::testRoundTripAllFields()
{
    // Given: a config with a distinctive value in every single field.
    const PanelConfig original = makeFullyPopulated();

    // When: serialize to JSON, then deserialize back.
    const QJsonObject json = panelConfigToJson(original);
    const PanelConfig roundTripped = panelConfigFromJson(json);

    // Then: every field survived intact. operator== is member-wise (12 fields,
    // incl. QStringList content comparison), so a single dropped or mis-typed
    // field fails here.
    QCOMPARE(roundTripped, original);
    // Belt-and-suspenders: explicitly check the fields that defaults could
    // mask (instanceIds non-empty, booleans true, negative-default geometry).
    QCOMPARE(roundTripped.instanceIds.size(), 2);
    QCOMPARE(roundTripped.instanceIds.first(), QStringLiteral("uuid-aaa"));
    QCOMPARE(roundTripped.autoLaunchSystem, true);
    QCOMPARE(roundTripped.closeAction, QStringLiteral("minimize"));
}

void PanelConfigTest::testEmptyObjectReturnsDefaults()
{
    // Given: an empty JSON object — every field missing. fromJson must return
    // a default-constructed config (all blueprint §5.2 defaults) and never
    // throw.
    const PanelConfig cfg = panelConfigFromJson(QJsonObject{});

    // Then: every field matches the blueprint §5.2 defaults.
    QCOMPARE(cfg.panelX, -1);
    QCOMPARE(cfg.panelY, -1);
    QCOMPARE(cfg.panelWidth, 1200);
    QCOMPARE(cfg.panelHeight, 760);
    QCOMPARE(cfg.theme, QStringLiteral("深紫梦幻"));
    QCOMPARE(cfg.fontSize, 13);
    QCOMPARE(cfg.panelOpacity, 1.0);
    QCOMPARE(cfg.instanceIds.size(), 0);
    QVERIFY(cfg.instanceIds.isEmpty());
    QCOMPARE(cfg.autoLaunchSystem, false);
    QCOMPARE(cfg.startMinimized, false);
    QCOMPARE(cfg.closeAction, QStringLiteral("exit"));
    QCOMPARE(cfg.confirmOnExit, false);
}

void PanelConfigTest::testInstanceIdsArrayToStringList()
{
    // Given: a JSON object whose instance_ids is a 2-element string array.
    QJsonObject json;
    QJsonArray ids;
    ids.append(QStringLiteral("uuid-1"));
    ids.append(QStringLiteral("uuid-2"));
    ids.append(QStringLiteral("uuid-3"));
    json.insert(QStringLiteral("instance_ids"), ids);

    // When: deserialize.
    const PanelConfig cfg = panelConfigFromJson(json);

    // Then: instanceIds is a QStringList with the same elements in the same
    // order (sidebar ordering is significant — blueprint §5.2 "顺序即侧边栏
    // 顺序").
    QCOMPARE(cfg.instanceIds.size(), 3);
    QCOMPARE(cfg.instanceIds.at(0), QStringLiteral("uuid-1"));
    QCOMPARE(cfg.instanceIds.at(1), QStringLiteral("uuid-2"));
    QCOMPARE(cfg.instanceIds.at(2), QStringLiteral("uuid-3"));

    // And round-trips back to the same JSON array.
    const QJsonObject out = panelConfigToJson(cfg);
    const QJsonValue outIds = out.value(QStringLiteral("instance_ids"));
    QVERIFY(outIds.isArray());
    QCOMPARE(outIds.toArray().size(), 3);

    // Given: a mixed array with a non-string element (tolerated, not fatal).
    QJsonObject mixed;
    QJsonArray mixedIds;
    mixedIds.append(QStringLiteral("good"));
    mixedIds.append(42);  // not a string — skipped
    mixedIds.append(QStringLiteral("also-good"));
    mixed.insert(QStringLiteral("instance_ids"), mixedIds);

    // When: deserialize.
    const PanelConfig mixedCfg = panelConfigFromJson(mixed);

    // Then: non-string element dropped, the two strings survive in order.
    QCOMPARE(mixedCfg.instanceIds.size(), 2);
    QCOMPARE(mixedCfg.instanceIds.at(0), QStringLiteral("good"));
    QCOMPARE(mixedCfg.instanceIds.at(1), QStringLiteral("also-good"));
}

void PanelConfigTest::testSnakeCaseMapping()
{
    // Given: a config whose closeAction / autoLaunchSystem differ from
    // defaults so a missed snake_case key would surface as the default value.
    PanelConfig c;
    c.closeAction = QStringLiteral("minimize");
    c.autoLaunchSystem = true;

    // When: serialize.
    const QJsonObject json = panelConfigToJson(c);

    // Then: the JSON uses snake_case keys (NOT camelCase). These are the exact
    // keys the Java Gson mapper writes, so a Java-written panel.json round-
    // trips and vice versa.
    QVERIFY2(json.contains(QStringLiteral("close_action")),
             "closeAction must serialize as snake_case 'close_action'");
    QVERIFY2(json.contains(QStringLiteral("auto_launch_system")),
             "autoLaunchSystem must serialize as 'auto_launch_system'");
    QVERIFY2(json.contains(QStringLiteral("instance_ids")),
             "instanceIds must serialize as 'instance_ids'");
    QVERIFY2(json.contains(QStringLiteral("panel_opacity")),
             "panelOpacity must serialize as 'panel_opacity'");
    // And the camelCase forms must NOT be present.
    QVERIFY2(!json.contains(QStringLiteral("closeAction")),
             "camelCase 'closeAction' must never appear in JSON output");
    QVERIFY2(!json.contains(QStringLiteral("autoLaunchSystem")),
             "camelCase 'autoLaunchSystem' must never appear in JSON output");
    QVERIFY2(!json.contains(QStringLiteral("instanceIds")),
             "camelCase 'instanceIds' must never appear in JSON output");
    QCOMPARE(json.value(QStringLiteral("close_action")).toString(), QStringLiteral("minimize"));

    // And fromJson reads the snake_case key back (round-trip via raw JSON).
    QJsonObject input;
    input.insert(QStringLiteral("close_action"), QStringLiteral("minimize"));
    input.insert(QStringLiteral("auto_launch_system"), true);
    const PanelConfig parsed = panelConfigFromJson(input);
    QCOMPARE(parsed.closeAction, QStringLiteral("minimize"));
    QCOMPARE(parsed.autoLaunchSystem, true);
}

void PanelConfigTest::testGarbageJsonReturnsDefaults()
{
    // Given: wrong-typed values — every key present but with a type that does
    // not match the field. Each must fall back to its default rather than
    // producing garbage (e.g. panelOpacity must NOT become 0 from a string).
    QJsonObject wrongTypes;
    wrongTypes.insert(QStringLiteral("panel_x"), QStringLiteral("hundred"));
    wrongTypes.insert(QStringLiteral("panel_opacity"), QStringLiteral("not-a-number"));
    wrongTypes.insert(QStringLiteral("font_size"), QStringLiteral("thirteen"));
    wrongTypes.insert(QStringLiteral("auto_launch_system"), QStringLiteral("yes"));
    wrongTypes.insert(QStringLiteral("close_action"), 42);
    wrongTypes.insert(QStringLiteral("instance_ids"), QStringLiteral("not-an-array"));
    wrongTypes.insert(QStringLiteral("confirm_on_exit"), QStringLiteral("true"));

    // When: deserialize — must succeed without throwing.
    const PanelConfig fromWrong = panelConfigFromJson(wrongTypes);

    // Then: mistyped values are rejected in favor of defaults.
    QCOMPARE(fromWrong.panelX, -1);
    QCOMPARE(fromWrong.panelOpacity, 1.0);
    QCOMPARE(fromWrong.fontSize, 13);
    QCOMPARE(fromWrong.autoLaunchSystem, false);
    QCOMPARE(fromWrong.closeAction, QStringLiteral("exit"));
    QVERIFY(fromWrong.instanceIds.isEmpty());
    QCOMPARE(fromWrong.confirmOnExit, false);

    // Given: instance_ids as a non-array (number). Must fall back to empty.
    QJsonObject idsWrong;
    idsWrong.insert(QStringLiteral("instance_ids"), 7);
    QCOMPARE(panelConfigFromJson(idsWrong).instanceIds.size(), 0);
}

void PanelConfigTest::testKeyCount()
{
    // Given: any config (default-constructed is fine — every field is written
    // regardless of value).
    const PanelConfig cfg;
    // When: serialize.
    const QJsonObject json = panelConfigToJson(cfg);
    // Then: exactly one JSON key per field. blueprint §5.2, the Java record,
    // and the struct definition each define 12 fields, so 12 keys — not 11.
    // (The task narrative's "11" is an off-by-one; the struct/blueprint/Java
    // ground truth is 12.)
    QCOMPARE(json.size(), kFieldCount);
}

QTEST_APPLESS_MAIN(PanelConfigTest)
#include "PanelConfigTest.moc"
