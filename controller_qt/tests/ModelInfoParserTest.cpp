// ModelInfoParserTest — Phase 5 todo 4 TDD for the .model3.json parser.
//
// Four behaviors locked (matches the task spec's MUST DO list, with
// assertions corrected against the REAL Hiyori fixture):
//   1. parse Hiyori.model3.json → motionGroups has Idle=9, TapBody=1;
//      expressions empty (Hiyori defines none); hitAreas=["Body"].
//   2. missing file → std::nullopt, no throw.
//   3. corrupt/truncated JSON → std::nullopt, no throw.
//   4. structurally-thin but valid JSON (root object with no FileReferences)
//      → a populated ModelInfo with empty containers (NOT std::nullopt).
//
// The Hiyori fixture is the real Cubism Samples file at
//   third_party/CubismSdkForNative/Samples/Resources/Hiyori/Hiyori.model3.json
// Its actual content (verified at port time):
//   FileReferences.Motions.Idle   = 9 entries (m01,m02,m03,m05-m10)
//   FileReferences.Motions.TapBody = 1 entry  (m04)
//   FileReferences.Expressions    = ABSENT (Hiyori has no expressions)
//   HitAreas                       = [{Id:"HitArea", Name:"Body"}]
// The task spec mentioned "TapHead" and "Head" — those do NOT exist in the
// real Hiyori fixture, so the assertions below check only what is actually
// there. (Other Cubism samples like Haru/Mao do define TapHead/Head; Hiyori
// is intentionally minimal.)
//
// QTEST_APPLESS_MAIN: pure file+JSON parsing, no event loop needed.
// CUBISM_RESOURCES_DIR is baked at compile time by tests/CMakeLists.txt so
// the test finds the fixture regardless of CWD (same pattern as
// PROTOCOL_FIXTURES_DIR / BIN_OUTPUT_DIR).

#include "core/ModelInfoParser.hpp"

#include <QDir>
#include <QFile>
#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTest>

namespace {

// The Hiyori fixture's real motion counts (from the file inspected at port
// time). Pinning these makes the test a regression net: if the parser starts
// miscounting arrays (e.g. off-by-one, or treating the FileReferences.Motions
// OBJECT as an array), the count assertion fails loudly.
constexpr int kHiyoriIdleMotionCount = 9;
constexpr int kHiyoriTapBodyMotionCount = 1;

} // namespace

class ModelInfoParserTest : public QObject
{
    Q_OBJECT

private slots:
    void testParseHiyori();
    void testMissingFile();
    void testCorruptJson();
    void testThinButValidJson();
};

void ModelInfoParserTest::testParseHiyori()
{
    const QString path = QStringLiteral(CUBISM_RESOURCES_DIR)
                       + QStringLiteral("/Hiyori/Hiyori.model3.json");

    const auto result = core::parseModelInfo(path);
    QVERIFY2(result.has_value(),
             "parseModelInfo must return a value for the real Hiyori fixture");

    // Motion groups: Hiyori defines exactly Idle (9) and TapBody (1).
    // Use .contains() per the spec; pin the exact counts too.
    QCOMPARE(result->motionGroups.size(), 2);
    QVERIFY2(result->motionGroups.contains(QStringLiteral("Idle")),
             "Idle motion group must be present");
    QVERIFY2(result->motionGroups.contains(QStringLiteral("TapBody")),
             "TapBody motion group must be present");
    QCOMPARE(result->motionGroups.value(QStringLiteral("Idle")),
             kHiyoriIdleMotionCount);
    QCOMPARE(result->motionGroups.value(QStringLiteral("TapBody")),
             kHiyoriTapBodyMotionCount);

    // Expressions: Hiyori defines NONE. The task spec assumed F01/F02/...;
    // the real fixture has no Expressions field, so the parser must yield an
    // empty list (NOT std::nullopt — missing subfield, not missing file).
    QCOMPARE(result->expressions.size(), 0);

    // Hit areas: Hiyori defines exactly Body. The task spec mentioned Head;
    // the real fixture has only Body.
    QCOMPARE(result->hitAreas.size(), 1);
    QCOMPARE(result->hitAreas.first(), QStringLiteral("Body"));
}

void ModelInfoParserTest::testMissingFile()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");
    const QString bogus = QDir(dir.path()).absoluteFilePath(
        QStringLiteral("does_not_exist.model3.json"));
    QVERIFY2(!QFile::exists(bogus), "precondition: bogus path must not exist");

    const auto result = core::parseModelInfo(bogus);
    QVERIFY2(!result.has_value(),
             "Missing file must yield std::nullopt, not a default ModelInfo");
}

void ModelInfoParserTest::testCorruptJson()
{
    QTemporaryFile file;
    QVERIFY2(file.open(), "precondition: temp file must open");
    // Truncated mid-key + unbalanced brace — QJsonDocument::fromJson must
    // reject this, and parseModelInfo must translate that to std::nullopt
    // rather than throwing or returning a partial ModelInfo.
    file.write("{\"FileReferences\":{\"Motions\":{\"Idle\":[");
    file.close();

    const auto result = core::parseModelInfo(file.fileName());
    QVERIFY2(!result.has_value(),
             "Corrupt JSON must yield std::nullopt, not a partial parse");
}

void ModelInfoParserTest::testThinButValidJson()
{
    // A valid JSON object root that omits FileReferences / HitAreas entirely.
    // Per the Java reference and the task's "never throws" contract: this is
    // NOT a corrupt file — the root is an object — so the parser returns a
    // populated ModelInfo with empty containers, NOT std::nullopt. This is
    // the path that lets the controller gracefully handle a minimal model.
    QTemporaryFile file;
    QVERIFY2(file.open(), "precondition: temp file must open");
    file.write("{\"Version\":3}");
    file.close();

    const auto result = core::parseModelInfo(file.fileName());
    QVERIFY2(result.has_value(),
             "Valid JSON object root must yield a ModelInfo, even with no "
             "FileReferences/HitAreas subfields");
    QCOMPARE(result->motionGroups.size(), 0);
    QCOMPARE(result->expressions.size(), 0);
    QCOMPARE(result->hitAreas.size(), 0);
}

QTEST_APPLESS_MAIN(ModelInfoParserTest)
#include "ModelInfoParserTest.moc"
