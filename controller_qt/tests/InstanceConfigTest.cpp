// allow: SIZE_OK — single cohesive QTest SUT for the InstanceConfig ser/deser
// model. Splitting the private slots across executables would fragment one
// logical contract (the 28-field round-trip + merge + snake_case + garbage
// invariants); kept as one class by intent.
//
// InstanceConfigTest — TDD for the InstanceConfig data model (T18).
//
// Six behaviors locked (matches the task spec MUST DO list):
//   1. Round-trip identity: toJson -> fromJson preserves all 28 fields.
//   2. Missing fields merge from defaults (only id+label present).
//   3. snake_case key mapping (graphics_backend, current_model_name, ...).
//   4. Unknown fields are silently ignored (interface.md §1.5).
//   5. Garbage / empty JSON -> default-constructed InstanceConfig, no throw.
//   6. toJson emits exactly 28 keys (one per field).
//
// QTEST_APPLESS_MAIN: pure JSON struct manipulation, no event loop needed.
//
// NOTE on field count: the task narrative says "29 fields" in several places,
// but the task's own struct definition, blueprint §5.1, and the Java
// InstanceConfig record each define exactly 28 fields. The key-count test
// therefore asserts 28 — the real, ground-truth count.

#include "core/InstanceConfig.hpp"

#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QTest>

namespace {

// Number of fields / serialized keys. Blueprint §5.1 + the Java record each
// define 28 fields; the Qt struct added a 29th (subtitle_adjust_mode) in
// Phase 5 Wave 8 todo 21 — the Java reference kept it as runtime-only state,
// but the Qt port persists it so the user's adjust-mode preference survives
// restarts. Every other ground-truth source agrees on the original 28.
constexpr int kFieldCount = 29;

// Build a config where EVERY field carries a distinctive, non-default value.
// Used by the round-trip test so a single missed field surfaces immediately.
// Values are deliberately different from the struct defaults AND from each
// other so a swap/mis-wire in toJson/fromJson cannot silently pass.
InstanceConfig makeFullyPopulated()
{
    InstanceConfig c;
    c.id = QStringLiteral("11111111-2222-3333-4444-555555555555");
    c.label = QStringLiteral("锦瑟");
    c.rendererPath = QStringLiteral("/opt/desktop-pet/renderer.exe");
    c.graphicsBackend = QStringLiteral("vulkan");
    c.modelName = QStringLiteral("giwa-idol2023");
    c.modelScale = 1.5;
    c.windowX = 100;
    c.windowY = 200;
    c.windowWidth = 320;
    c.windowHeight = 480;
    c.opacity = 0.75;
    c.dragMode = QStringLiteral("physics");
    c.idleInterval = 25;
    c.targetFps = 60;
    c.autoStart = true;
    c.currentExpression = QStringLiteral("F05");
    c.voicePack = QStringLiteral("锦瑟-锦瑟-中文-voice");
    c.volume = 0.4;
    c.muted = true;
    c.layoutOffsetX = -12.5;
    c.layoutOffsetY = 7.25;
    c.layoutScale = 0.9;
    c.subtitleOffsetX = 3.0;
    c.subtitleOffsetY = -2.0;
    c.subtitleAreaWidth = 800;
    c.subtitleAreaHeight = 120;
    c.subtitleFontSize = 32.0;
    c.subtitleStylePreset = QStringLiteral("终端黑客");
    c.subtitleAdjustMode = true;
    return c;
}

} // namespace

class InstanceConfigTest : public QObject
{
    Q_OBJECT

private slots:
    void testRoundTripAllFields();
    void testMissingFieldsMergeFromDefaults();
    void testSnakeCaseMapping();
    void testUnknownFieldsIgnored();
    void testGarbageJsonReturnsDefaults();
    void testKeyCount();
};

void InstanceConfigTest::testRoundTripAllFields()
{
    // Given: a config with a distinctive value in every single field.
    const InstanceConfig original = makeFullyPopulated();

    // When: serialize to JSON, then deserialize back.
    const QJsonObject json = instanceConfigToJson(original);
    const InstanceConfig roundTripped = instanceConfigFromJson(json);

    // Then: every field survived intact. operator== is member-wise (28 fields),
    // so a single dropped or mis-typed field fails here.
    QCOMPARE(roundTripped, original);
    // Belt-and-suspenders: explicitly check the trickiest fields that defaults
    // could mask (voicePack non-empty, autoStart true, negative offsets).
    QCOMPARE(roundTripped.voicePack, QStringLiteral("锦瑟-锦瑟-中文-voice"));
    QCOMPARE(roundTripped.autoStart, true);
    QCOMPARE(roundTripped.layoutOffsetX, -12.5);
}

void InstanceConfigTest::testMissingFieldsMergeFromDefaults()
{
    // Given: a JSON object carrying ONLY id and label — every other field is
    // absent, so fromJson must fill each from the struct defaults.
    QJsonObject json;
    json.insert(QStringLiteral("id"), QStringLiteral("deadbeef-0000-1111-2222-333333333333"));
    json.insert(QStringLiteral("label"), QStringLiteral("OnlyName"));

    // When: deserialize.
    const InstanceConfig cfg = instanceConfigFromJson(json);

    // Then: the two provided fields are honored, and the rest take defaults
    // matching blueprint §5.1 (a sample of representative fields across each
    // group: backend, model, window, behavior, audio, layout, subtitle).
    QCOMPARE(cfg.id, QStringLiteral("deadbeef-0000-1111-2222-333333333333"));
    QCOMPARE(cfg.label, QStringLiteral("OnlyName"));
    QCOMPARE(cfg.graphicsBackend, QStringLiteral("opengl"));
    QCOMPARE(cfg.modelName, QStringLiteral("Hiyori"));
    QCOMPARE(cfg.modelScale, 1.0);
    QCOMPARE(cfg.windowX, 1200);
    QCOMPARE(cfg.windowY, 600);
    QCOMPARE(cfg.windowWidth, 400);
    QCOMPARE(cfg.windowHeight, 500);
    QCOMPARE(cfg.opacity, 1.0);
    QCOMPARE(cfg.dragMode, QStringLiteral("direct"));
    QCOMPARE(cfg.idleInterval, 10);
    QCOMPARE(cfg.targetFps, 0);
    QCOMPARE(cfg.autoStart, false);
    QCOMPARE(cfg.currentExpression, QStringLiteral("F01"));
    QCOMPARE(cfg.voicePack, QString());          // empty = not mounted
    QCOMPARE(cfg.volume, 1.0);
    QCOMPARE(cfg.muted, false);
    QCOMPARE(cfg.layoutScale, 1.0);
    QCOMPARE(cfg.subtitleFontSize, 48.0);
    QCOMPARE(cfg.subtitleStylePreset, QStringLiteral("默认"));
}

void InstanceConfigTest::testSnakeCaseMapping()
{
    // Given: a config whose graphicsBackend / modelName differ from defaults
    // so a missed snake_case key would show up as the default value.
    InstanceConfig c;
    c.graphicsBackend = QStringLiteral("vulkan");
    c.modelName = QStringLiteral("Haru");

    // When: serialize.
    const QJsonObject json = instanceConfigToJson(c);

    // Then: the JSON uses snake_case keys (NOT camelCase). These are the exact
    // keys the Java Gson mapper writes, so a Java-written file round-trips and
    // vice versa. current_model_name is the non-obvious one (modelName, not
    // model_name — matches configuration.md §5 + the Java record's Gson policy).
    QVERIFY2(json.contains(QStringLiteral("graphics_backend")),
             "graphicsBackend must serialize as snake_case 'graphics_backend'");
    QVERIFY2(json.contains(QStringLiteral("current_model_name")),
             "modelName must serialize as 'current_model_name' (not model_name)");
    QVERIFY2(json.contains(QStringLiteral("window_x")),
             "windowX must serialize as 'window_x'");
    QVERIFY2(json.contains(QStringLiteral("voice_pack")),
             "voicePack must serialize as 'voice_pack'");
    // And the camelCase forms must NOT be present.
    QVERIFY2(!json.contains(QStringLiteral("graphicsBackend")),
             "camelCase 'graphicsBackend' must never appear in JSON output");
    QVERIFY2(!json.contains(QStringLiteral("modelName")),
             "camelCase 'modelName' must never appear in JSON output");
    QCOMPARE(json.value(QStringLiteral("graphics_backend")).toString(), QStringLiteral("vulkan"));

    // And fromJson reads the snake_case key back (round-trip via raw JSON).
    QJsonObject input;
    input.insert(QStringLiteral("graphics_backend"), QStringLiteral("vulkan"));
    input.insert(QStringLiteral("current_model_name"), QStringLiteral("Haru"));
    const InstanceConfig parsed = instanceConfigFromJson(input);
    QCOMPARE(parsed.graphicsBackend, QStringLiteral("vulkan"));
    QCOMPARE(parsed.modelName, QStringLiteral("Haru"));
}

void InstanceConfigTest::testUnknownFieldsIgnored()
{
    // Given: a valid config JSON plus several unrecognized keys (forward-
    // compatibility: interface.md §1.5 mandates unknown fields are tolerated,
    // not errors). One unknown shadows a known type with a wrong type too.
    QJsonObject json;
    json.insert(QStringLiteral("id"), QStringLiteral("known-id"));
    json.insert(QStringLiteral("unknown_field"), 42);
    json.insert(QStringLiteral("future_setting"), QStringLiteral("xyz"));
    json.insert(QStringLiteral("nested"), QJsonObject{{QStringLiteral("a"), 1}});

    // When: deserialize — must succeed without throwing.
    const InstanceConfig cfg = instanceConfigFromJson(json);

    // Then: the known field is read, unknown fields are dropped (no crash, no
    // mis-routing), and defaults still apply for everything else.
    QCOMPARE(cfg.id, QStringLiteral("known-id"));
    QCOMPARE(cfg.graphicsBackend, QStringLiteral("opengl")); // default preserved
    QCOMPARE(cfg.volume, 1.0);
}

void InstanceConfigTest::testGarbageJsonReturnsDefaults()
{
    // Given: an empty object — every field missing. This is the "corrupt /
    // stripped" JSON case. The function must return a default-constructed
    // config (empty id + all field defaults) and never throw.
    const InstanceConfig fromEmpty = instanceConfigFromJson(QJsonObject{});

    // Then: id is empty (no id key was present), and representative defaults
    // are in place. Crucially: the call returned normally — no exception.
    QCOMPARE(fromEmpty.id, QString());
    QCOMPARE(fromEmpty.label, QString());
    QCOMPARE(fromEmpty.graphicsBackend, QStringLiteral("opengl"));
    QCOMPARE(fromEmpty.opacity, 1.0);
    QCOMPARE(fromEmpty.autoStart, false);

    // Given: wrong-typed values — every key present but with a type that does
    // not match the field. Each must fall back to its default rather than
    // producing garbage (e.g. opacity must NOT become 0 from a string).
    QJsonObject wrongTypes;
    wrongTypes.insert(QStringLiteral("opacity"), QStringLiteral("not-a-number"));
    wrongTypes.insert(QStringLiteral("auto_start"), QStringLiteral("yes"));
    wrongTypes.insert(QStringLiteral("window_x"), QStringLiteral("hundred"));
    wrongTypes.insert(QStringLiteral("target_fps"), true);
    const InstanceConfig fromWrong = instanceConfigFromJson(wrongTypes);

    // Then: mistyped values are rejected in favor of defaults.
    QCOMPARE(fromWrong.opacity, 1.0);
    QCOMPARE(fromWrong.autoStart, false);
    QCOMPARE(fromWrong.windowX, 1200);
    QCOMPARE(fromWrong.targetFps, 0);

    // voice_pack accepts BOTH JSON null (Java's default) and a real string.
    QJsonObject nullVoice;
    nullVoice.insert(QStringLiteral("voice_pack"), QJsonValue(QJsonValue::Null));
    QCOMPARE(instanceConfigFromJson(nullVoice).voicePack, QString());
    QJsonObject strVoice;
    strVoice.insert(QStringLiteral("voice_pack"), QStringLiteral("pack-a"));
    QCOMPARE(instanceConfigFromJson(strVoice).voicePack, QStringLiteral("pack-a"));
}

void InstanceConfigTest::testKeyCount()
{
    // Given: any config (default-constructed is fine — every field is written
    // regardless of value).
    const InstanceConfig cfg;
    // When: serialize.
    const QJsonObject json = instanceConfigToJson(cfg);
    // Then: exactly one JSON key per field. blueprint §5.1, the Java record,
    // and the struct definition each define 28 fields, so 28 keys — not 29.
    // (The task narrative's "29" is an off-by-one; the struct/blueprint/Java
    // ground truth is 28.)
    QCOMPARE(json.size(), kFieldCount);
}

QTEST_APPLESS_MAIN(InstanceConfigTest)
#include "InstanceConfigTest.moc"
