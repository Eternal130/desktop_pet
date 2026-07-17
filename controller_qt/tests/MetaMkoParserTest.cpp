// MetaMkoParserTest — Phase 5 Wave 8 todo 19 TDD for the meta.mko parser.
//
// Six behaviors locked:
//   1. A valid synthesized Bundle (meta + group + action + module) parses
//      into a populated VoicePackInfo with all fields correctly mapped.
//   2. Multiple groups + actions bucket correctly by group code.
//   3. Optional lipSync field: present → populated, absent → empty.
//   4. Missing meta.mko file → std::nullopt (no throw).
//   5. Truncated/garbage bytes → std::nullopt (no throw).
//   6. Empty voicePackDir → std::nullopt.
//
// ── Test fixture synthesis ──────────────────────────────────────────────────
//
// No real voice-pack fixtures ship under Resources/VoicePacks in the current
// build. The task spec requires synthesizing a minimal valid Bundle via the
// generated protobuf API. Since the Qt port uses a hand-rolled reader (not
// generated protobuf), we synthesize via a minimal protobuf SERIALIZER in
// the test's anonymous namespace. This proves the reader handles real
// protobuf wire format — the serialized bytes are byte-for-byte identical
// to what protoc --cpp_out would produce for the same logical message.
//
// The serializer implements the 3 wire types the bundles.proto schema uses:
//   - varint (wire type 0): int32, int64 fields
//   - length-delimited (wire type 2): string fields + embedded messages
// This is the exact subset MetaMkoParser.cpp's ProtoReader consumes, so the
// round-trip is a faithful end-to-end proof.
//
// QTEST_APPLESS_MAIN: pure QFile/QByteArray ops, no event loop needed.

#include "core/MetaMkoParser.hpp"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

namespace {

// ── Minimal protobuf wire-format serializer (test-only) ────────────────────
// Mirrors the encoding rules in MetaMkoParser.cpp's file-level comment.
// Each encode* returns the raw bytes for one field (tag + value), suitable
// for concatenation into a parent message's byte stream.

QByteArray encodeVarint(quint64 value)
{
    QByteArray out;
    while (value >= 0x80) {
        out.append(static_cast<char>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    out.append(static_cast<char>(value));
    return out;
}

QByteArray encodeTag(int fieldNumber, int wireType)
{
    return encodeVarint((static_cast<quint64>(fieldNumber) << 3)
                        | static_cast<quint64>(wireType));
}

// Wire type 0: varint-encoded scalar (int32, int64, bool, enum).
QByteArray encodeVarintField(int fieldNumber, quint64 value)
{
    return encodeTag(fieldNumber, 0) + encodeVarint(value);
}

// Wire type 2: length-delimited string.
QByteArray encodeStringField(int fieldNumber, const QString& value)
{
    const QByteArray utf8 = value.toUtf8();
    return encodeTag(fieldNumber, 2) + encodeVarint(utf8.size()) + utf8;
}

// Wire type 2: embedded message (the message bytes are themselves a
// concatenated sequence of encoded fields).
QByteArray encodeMessageField(int fieldNumber, const QByteArray& message)
{
    return encodeTag(fieldNumber, 2) + encodeVarint(message.size()) + message;
}

// ── Bundle builders ────────────────────────────────────────────────────────

// Build a Meta message: name="Test Voice Pack", code="test_pack".
QByteArray buildMetaMessage()
{
    QByteArray m;
    m += encodeStringField(1, QStringLiteral("Test Voice Pack"));
    m += encodeStringField(2, QStringLiteral("test_pack"));
    return m;
}

// Build an ActionGroup: code="tap_head", priority=5, name="Tap Head".
QByteArray buildTapHeadGroupMessage()
{
    QByteArray g;
    g += encodeStringField(1, QStringLiteral("tap_head"));
    g += encodeVarintField(2, 5);
    g += encodeStringField(3, QStringLiteral("Tap Head"));
    return g;
}

// Build an ActionGroup: code="idle", priority=1, name="Idle".
QByteArray buildIdleGroupMessage()
{
    QByteArray g;
    g += encodeStringField(1, QStringLiteral("idle"));
    g += encodeVarintField(2, 1);
    g += encodeStringField(3, QStringLiteral("Idle"));
    return g;
}

// Build an Action belonging to tap_head: id=42, with motion/audio/doc/fades.
QByteArray buildTapHeadActionMessage()
{
    QByteArray a;
    a += encodeVarintField(1, 42);
    a += encodeStringField(2, QStringLiteral("tap_head"));
    a += encodeStringField(3, QStringLiteral("motions/tap01.motion3.json"));
    a += encodeStringField(4, QStringLiteral("audio/tap_head.wav"));
    // field 5 (lipSync) omitted → hasLipSync=false
    a += encodeStringField(6, QStringLiteral("Tap head response"));
    a += encodeVarintField(7, 300);  // fadeIn
    a += encodeVarintField(8, 500);  // fadeOut
    return a;
}

// Build an Action belonging to tap_head WITH a lipSync path (field 5 present).
QByteArray buildTapHeadActionWithLipSyncMessage()
{
    QByteArray a;
    a += encodeVarintField(1, 43);
    a += encodeStringField(2, QStringLiteral("tap_head"));
    a += encodeStringField(3, QStringLiteral("motions/tap02.motion3.json"));
    a += encodeStringField(4, QStringLiteral("audio/tap_head_02.wav"));
    a += encodeStringField(5, QStringLiteral("lip_sync/tap02.lipsync.json"));
    a += encodeStringField(6, QStringLiteral("Tap head with lip sync"));
    a += encodeVarintField(7, 200);
    a += encodeVarintField(8, 400);
    return a;
}

// Build an Action belonging to idle: id=10, minimal fields.
QByteArray buildIdleActionMessage()
{
    QByteArray a;
    a += encodeVarintField(1, 10);
    a += encodeStringField(2, QStringLiteral("idle"));
    a += encodeStringField(3, QStringLiteral("motions/idle.motion3.json"));
    // audio + lipSync + doc + fades all omitted → empty/zero after parse
    return a;
}

// Build an AiModule: key="core_behavior", priority=10, file="behavior.json".
QByteArray buildAiModuleMessage()
{
    QByteArray m;
    m += encodeStringField(1, QStringLiteral("core_behavior"));
    m += encodeVarintField(2, 10);
    m += encodeStringField(3, QStringLiteral("behavior.json"));
    return m;
}

// Assemble a complete Bundle from the message builders above.
QByteArray buildFullTestBundle()
{
    QByteArray bundle;
    bundle += encodeMessageField(1, buildMetaMessage());              // meta
    bundle += encodeMessageField(3, buildAiModuleMessage());          // modules[0]
    bundle += encodeMessageField(4, buildTapHeadGroupMessage());      // groups[0]
    bundle += encodeMessageField(4, buildIdleGroupMessage());        // groups[1]
    bundle += encodeMessageField(5, buildTapHeadActionMessage());    // actions[0]
    bundle += encodeMessageField(5, buildTapHeadActionWithLipSyncMessage()); // [1]
    bundle += encodeMessageField(5, buildIdleActionMessage());       // actions[2]
    return bundle;
}

// Write bytes to <voicePackDir>/meta.mko. Creates voicePackDir if needed.
bool writeMetaMko(const QString& parentDir, const QString& packName,
                  const QByteArray& bytes)
{
    const QString packDir = QDir(parentDir).absoluteFilePath(packName);
    if (!QDir().mkpath(packDir))
        return false;
    QFile f(QDir(packDir).absoluteFilePath(QStringLiteral("meta.mko")));
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(bytes);
    f.close();
    return f.error() == QFile::NoError;
}

} // namespace

class MetaMkoParserTest : public QObject
{
    Q_OBJECT

private slots:
    void testParseValidBundleReturnsVoicePackInfo();
    void testActionsBucketedByGroupCode();
    void testOptionalLipSyncPresence();
    void testMissingFileReturnsNullopt();
    void testCorruptBytesReturnNullopt();
    void testEmptyVoicePackDirReturnsNullopt();
};

void MetaMkoParserTest::testParseValidBundleReturnsVoicePackInfo()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");
    QVERIFY2(writeMetaMko(dir.path(), QStringLiteral("test_pack"),
                          buildFullTestBundle()),
             "precondition: failed to write synthesized meta.mko");

    const QString packDir = QDir(dir.path()).absoluteFilePath(
        QStringLiteral("test_pack"));
    const auto result = core::parseMetaMko(packDir);

    QVERIFY2(result.has_value(), "Valid Bundle must parse to a VoicePackInfo");

    // Meta → displayName / code
    QCOMPARE(result->dirName, QStringLiteral("test_pack"));
    QCOMPARE(result->displayName, QStringLiteral("Test Voice Pack"));
    QCOMPARE(result->code, QStringLiteral("test_pack"));
    QCOMPARE(result->basePath, packDir);

    // Two groups parsed: tap_head + idle
    QCOMPARE(result->groups.size(), 2);
    QVERIFY2(result->groups.contains(QStringLiteral("tap_head")),
             "tap_head group must be present");
    QVERIFY2(result->groups.contains(QStringLiteral("idle")),
             "idle group must be present");

    // tap_head group metadata
    const auto& tapHead = result->groups.value(QStringLiteral("tap_head"));
    QCOMPARE(tapHead.code, QStringLiteral("tap_head"));
    QCOMPARE(tapHead.name, QStringLiteral("Tap Head"));
    QCOMPARE(tapHead.priority, 5);

    // One module parsed
    QCOMPARE(result->modules.size(), 1);
    QCOMPARE(result->modules.at(0).key, QStringLiteral("core_behavior"));
    QCOMPARE(result->modules.at(0).priority, 10);
    QCOMPARE(result->modules.at(0).filePath, QStringLiteral("behavior.json"));
}

void MetaMkoParserTest::testActionsBucketedByGroupCode()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");
    QVERIFY2(writeMetaMko(dir.path(), QStringLiteral("bucket"),
                          buildFullTestBundle()),
             "precondition: failed to write synthesized meta.mko");

    const QString packDir = QDir(dir.path()).absoluteFilePath(
        QStringLiteral("bucket"));
    const auto result = core::parseMetaMko(packDir);
    QVERIFY2(result.has_value(), "parse must succeed for valid Bundle");

    // tap_head has 2 actions (id=42 without lipSync, id=43 with lipSync);
    // idle has 1 action (id=10).
    const auto& tapHead = result->groups.value(QStringLiteral("tap_head"));
    const auto& idle = result->groups.value(QStringLiteral("idle"));
    QCOMPARE(tapHead.actions.size(), 2);
    QCOMPARE(idle.actions.size(), 1);

    // Verify action field mapping for the rich action (id=42).
    const auto& a = tapHead.actions.at(0);
    QCOMPARE(a.id, 42);
    QCOMPARE(a.motionPath, QStringLiteral("motions/tap01.motion3.json"));
    QCOMPARE(a.audioPath, QStringLiteral("audio/tap_head.wav"));
    QCOMPARE(a.doc, QStringLiteral("Tap head response"));
    QCOMPARE(a.fadeInMs, static_cast<qint64>(300));
    QCOMPARE(a.fadeOutMs, static_cast<qint64>(500));

    // Verify the minimal idle action (missing fields → empty/zero defaults).
    const auto& idleAction = idle.actions.at(0);
    QCOMPARE(idleAction.id, 10);
    QCOMPARE(idleAction.motionPath, QStringLiteral("motions/idle.motion3.json"));
    QVERIFY2(idleAction.audioPath.isEmpty(),
             "Absent audio field must yield empty string");
    QVERIFY2(idleAction.doc.isEmpty(),
             "Absent doc field must yield empty string");
    QCOMPARE(idleAction.fadeInMs, static_cast<qint64>(0));
    QCOMPARE(idleAction.fadeOutMs, static_cast<qint64>(0));
}

void MetaMkoParserTest::testOptionalLipSyncPresence()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");
    QVERIFY2(writeMetaMko(dir.path(), QStringLiteral("lipsync"),
                          buildFullTestBundle()),
             "precondition: failed to write synthesized meta.mko");

    const QString packDir = QDir(dir.path()).absoluteFilePath(
        QStringLiteral("lipsync"));
    const auto result = core::parseMetaMko(packDir);
    QVERIFY2(result.has_value(), "parse must succeed");

    const auto& tapHead = result->groups.value(QStringLiteral("tap_head"));
    QCOMPARE(tapHead.actions.size(), 2);

    // actions[0] (id=42): no lipSync field → empty lipSyncPath.
    QVERIFY2(tapHead.actions.at(0).lipSyncPath.isEmpty(),
             "Absent optional lipSync must yield empty string");
    // actions[1] (id=43): lipSync present → populated.
    QCOMPARE(tapHead.actions.at(1).lipSyncPath,
             QStringLiteral("lip_sync/tap02.lipsync.json"));
}

void MetaMkoParserTest::testMissingFileReturnsNullopt()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");
    // No meta.mko written — the dir exists but is empty.
    const QString packDir = QDir(dir.path()).absoluteFilePath(
        QStringLiteral("ghost"));
    QDir().mkpath(packDir);

    const auto result = core::parseMetaMko(packDir);
    QVERIFY2(!result.has_value(),
             "Missing meta.mko must yield std::nullopt, not a crash");
}

void MetaMkoParserTest::testCorruptBytesReturnNullopt()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");
    // 5 bytes of garbage: tag 0x01 = field 0, wire type 1 (64-bit) → tries to
    // read 8 bytes, only 4 remain → ParseError → std::nullopt.
    const QByteArray garbage = QByteArray::fromRawData("\x01\x02\x03\x04\x05", 5);
    QVERIFY2(writeMetaMko(dir.path(), QStringLiteral("bad"), garbage),
             "precondition: failed to write garbage meta.mko");

    const QString packDir = QDir(dir.path()).absoluteFilePath(
        QStringLiteral("bad"));
    const auto result = core::parseMetaMko(packDir);
    QVERIFY2(!result.has_value(),
             "Truncated/garbage bytes must yield std::nullopt, no throw");
}

void MetaMkoParserTest::testEmptyVoicePackDirReturnsNullopt()
{
    const auto result = core::parseMetaMko(QString());
    QVERIFY2(!result.has_value(),
             "Empty voicePackDir must yield std::nullopt");
}

QTEST_APPLESS_MAIN(MetaMkoParserTest)
#include "MetaMkoParserTest.moc"
