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
// QTEST_MAIN (NOT APPLESS): the serde slots are pure JSON manipulation, but
// testDefaultModelNameControllerRoundTrip drives PanelConfigController →
// PanelStateManager → QSqlDatabase, which requires a QCoreApplication (same
// as PanelStateManagerTest / DatabaseManagerTest).
//
// NOTE on field count: the task narrative says "11 fields" in places, but the
// task's own struct definition, blueprint §5.2, and the Java PanelConfig record
// each define exactly 12 fields. The key-count test therefore asserts 12 — the
// real, ground-truth count.

#include "core/DatabaseManager.hpp"
#include "core/PanelConfig.hpp"
#include "core/PanelConfigController.hpp"

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "core/PanelConfig.hpp"

namespace {

// Number of fields / serialized keys. Every ground-truth source (blueprint
// §5.2, the Java record, this struct's own definition) agrees on 12 core
// fields; the notification-bubble stream adds 2 (notifications_enabled,
// notification_duration_ms) and the model library adds 1 (default_model_name)
// → 15.
constexpr int kFieldCount = 15;

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
    c.notificationsEnabled = false;
    c.notificationDurationMs = 35000;
    c.defaultModelName = QStringLiteral("Haru");
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
    void testDefaultModelNameControllerRoundTrip();
    void testPluginWriteEnabledKvRoundTrip();
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
    QCOMPARE(cfg.notificationsEnabled, true);
    QCOMPARE(cfg.notificationDurationMs, 20000);
    QCOMPARE(cfg.defaultModelName, QStringLiteral("Hiyori"));
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
    wrongTypes.insert(QStringLiteral("notifications_enabled"), QStringLiteral("yes"));
    wrongTypes.insert(QStringLiteral("notification_duration_ms"), QStringLiteral("slow"));

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
    QCOMPARE(fromWrong.notificationsEnabled, true);
    QCOMPARE(fromWrong.notificationDurationMs, 20000);
    // default_model_name as a non-string (number) → default "Hiyori".
    QJsonObject wrongModel;
    wrongModel.insert(QStringLiteral("default_model_name"), 12345);
    QCOMPARE(panelConfigFromJson(wrongModel).defaultModelName,
             QStringLiteral("Hiyori"));

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
    // and the struct definition each define 12 core fields, plus the 2
    // notification-stream fields added by the bubble-stream phase and the 1
    // model-library field (default_model_name) → 15 keys.
    QCOMPARE(json.size(), kFieldCount);
}

// 模型库 B 档: the PanelConfigController bridge's defaultModelName property
// must read the persisted value at construction, write through the atomic
// load-modify-save (PanelStateManager) on set, and survive a controller
// re-construction — the read/write/persist round-trip contract the
// model-library settings UI binds to.
void PanelConfigTest::testDefaultModelNameControllerRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");

    // Fresh dir → controller seeds the PanelConfig default.
    PanelConfigController fresh(dir.path());
    QCOMPARE(fresh.defaultModelName(), QStringLiteral("Hiyori"));

    // Write through the property setter → NOTIFY fires, cache updates.
    QSignalSpy changedSpy(&fresh, &PanelConfigController::defaultModelNameChanged);
    QVERIFY(changedSpy.isValid());
    fresh.setDefaultModelName(QStringLiteral("Mao"));
    QCOMPARE(fresh.defaultModelName(), QStringLiteral("Mao"));
    QCOMPARE(changedSpy.count(), 1);

    // Same-value write is a no-op (no spurious NOTIFY).
    fresh.setDefaultModelName(QStringLiteral("Mao"));
    QCOMPARE(changedSpy.count(), 1);

    // Empty write is rejected (the create-instance flow treats empty as
    // "use the default" — an empty persisted value would be ambiguous).
    fresh.setDefaultModelName(QString());
    QCOMPARE(fresh.defaultModelName(), QStringLiteral("Mao"));
    QCOMPARE(changedSpy.count(), 1);

    // Persisted: a second controller over the same dir reads the written
    // value (proves the load-modify-save hit disk, not just the cache).
    PanelConfigController reloaded(dir.path());
    QCOMPARE(reloaded.defaultModelName(), QStringLiteral("Mao"));

    // And the OTHER PanelConfig fields survived the write (the setter's
    // load-modify-save must not clobber them).
    PanelConfigController writer(dir.path());
    writer.setStartMinimized(true);
    PanelConfigController survivor(dir.path());
    QCOMPARE(survivor.defaultModelName(), QStringLiteral("Mao"));
    QCOMPARE(survivor.startMinimized(), true);
}

// S6 (v1.3): the plugin_write_enabled kill-switch mirror — reads the
// DEDICATED kv row the PanelApplication provider consults live (absent =
// enabled), writes flip that exact row (never the panel_config blob), and
// a same-value write is a silent no-op.
void PanelConfigTest::testPluginWriteEnabledKvRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");

    DatabaseManager db;
    QVERIFY(db.open(QDir(dir.path()).filePath(QStringLiteral("app.db"))));

    // Fresh db → enabled (the documented kv default).
    PanelConfigController ctl(dir.path());
    ctl.setDatabase(&db);
    QCOMPARE(ctl.pluginWriteEnabled(), true);

    // Flip off → the DEDICATED kv row flips (what the live provider
    // reads), NOTIFY fires once.
    QSignalSpy spy(&ctl, &PanelConfigController::pluginWriteEnabledChanged);
    ctl.setPluginWriteEnabled(false);
    QCOMPARE(ctl.pluginWriteEnabled(), false);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(db.getValue(QStringLiteral("plugin_write_enabled"),
                         QStringLiteral("1")),
             QStringLiteral("0"));

    // Same-value write is a no-op (no row write observable via NOTIFY).
    ctl.setPluginWriteEnabled(false);
    QCOMPARE(spy.count(), 1);

    // Flip back on → row restored; a fresh controller (the provider's
    // next boot read) sees enabled.
    ctl.setPluginWriteEnabled(true);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(db.getValue(QStringLiteral("plugin_write_enabled"),
                         QStringLiteral("1")),
             QStringLiteral("1"));
    PanelConfigController reloaded(dir.path());
    reloaded.setDatabase(&db);
    QCOMPARE(reloaded.pluginWriteEnabled(), true);
}

QTEST_MAIN(PanelConfigTest)
#include "PanelConfigTest.moc"
