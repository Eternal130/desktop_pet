// VoicePackScannerTest — Phase 5 Wave 8 todo 19 TDD for the voice-pack
// directory scanner.
//
// Six behaviors locked (mirrors ModelScannerTest + the Java reference
// VoicePackScannerTest):
//   1. A fake <tmp>/Resources/VoicePacks/<Pack>/meta.mko qualifies and is
//      reported by name.
//   2. Multiple qualifying packs are returned sorted case-insensitively.
//   3. A subdir WITHOUT meta.mko is filtered out.
//   4. A hidden subdir (starting with '.') is skipped even if it has meta.mko.
//   5. A nonexistent rendererDir → empty list (no throw, no crash).
//   6. Empty rendererDir → empty list.
//
// Uses QTemporaryDir to create the EXACT shipped layout
// (<tmp>/Resources/VoicePacks/<Pack>/meta.mko). No real voice pack fixtures
// ship under Resources/VoicePacks in the current build, so synthesized
// fixtures are the only option — same strategy as ModelScannerTest.
//
// QTEST_APPLESS_MAIN: pure QDir/QFile ops, no event loop needed.

#include "core/VoicePackScanner.hpp"

#include <QDir>
#include <QFile>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

namespace {

// Create a zero-byte file at <dir>/<name> so QFile::exists() returns true.
// The scanner only checks existence, never parses the file — content is
// irrelevant (mirrors ModelScannerTest::touchFile).
bool touchFile(const QString& dir, const QString& name)
{
    const QString path = QDir(dir).absoluteFilePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.close();
    return QFile::exists(path);
}

// Create <root>/Resources/VoicePacks/<packName>/meta.mko — the exact layout
// scanAvailableVoicePacks expects. Returns true on success.
bool createVoicePackDir(const QString& root, const QString& packName)
{
    const QString packDir = QDir(root).absoluteFilePath(
        QStringLiteral("Resources/VoicePacks/") + packName);
    if (!QDir().mkpath(packDir))
        return false;
    return touchFile(packDir, QStringLiteral("meta.mko"));
}

} // namespace

class VoicePackScannerTest : public QObject
{
    Q_OBJECT

private slots:
    void testScanFindsSinglePack();
    void testScanReturnsSortedMultiple();
    void testScanFiltersDirWithoutMetaMko();
    void testScanSkipsHiddenDir();
    void testScanNonexistentDirReturnsEmpty();
    void testScanEmptyRendererDirReturnsEmpty();
};

void VoicePackScannerTest::testScanFindsSinglePack()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");
    QVERIFY2(createVoicePackDir(dir.path(), QStringLiteral("TestPack")),
             "precondition: failed to create TestPack fixture");

    const QStringList result = core::scanAvailableVoicePacks(dir.path());
    QCOMPARE(result.size(), 1);
    QCOMPARE(result.first(),
             QDir(dir.path()).absoluteFilePath(
                 QStringLiteral("Resources/VoicePacks/TestPack")));
}

void VoicePackScannerTest::testScanReturnsSortedMultiple()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");
    // Create in non-sorted order to prove the scanner sorts.
    QVERIFY2(createVoicePackDir(dir.path(), QStringLiteral("zebra")),
             "precondition: failed to create zebra fixture");
    QVERIFY2(createVoicePackDir(dir.path(), QStringLiteral("Alpha")),
             "precondition: failed to create Alpha fixture");
    QVERIFY2(createVoicePackDir(dir.path(), QStringLiteral("mid")),
             "precondition: failed to create mid fixture");

    const QStringList result = core::scanAvailableVoicePacks(dir.path());
    QCOMPARE(result.size(), 3);
    // Case-insensitive sort: Alpha < mid < zebra (entries are absolute
    // pack paths under Resources/VoicePacks/)
    QCOMPARE(result.at(0),
             QDir(dir.path()).absoluteFilePath(
                 QStringLiteral("Resources/VoicePacks/Alpha")));
    QCOMPARE(result.at(1),
             QDir(dir.path()).absoluteFilePath(
                 QStringLiteral("Resources/VoicePacks/mid")));
    QCOMPARE(result.at(2),
             QDir(dir.path()).absoluteFilePath(
                 QStringLiteral("Resources/VoicePacks/zebra")));
}

void VoicePackScannerTest::testScanFiltersDirWithoutMetaMko()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");
    // A qualifying pack (has meta.mko).
    QVERIFY2(createVoicePackDir(dir.path(), QStringLiteral("HasFile")),
             "precondition: failed to create HasFile fixture");

    // A bare subdir without meta.mko — must be filtered out.
    const QString noFileDir = QDir(dir.path()).absoluteFilePath(
        QStringLiteral("Resources/VoicePacks/NoFile"));
    QVERIFY2(QDir().mkpath(noFileDir),
             "precondition: failed to create NoFile subdir");

    const QStringList result = core::scanAvailableVoicePacks(dir.path());
    QCOMPARE(result.size(), 1);
    QCOMPARE(result.first(),
             QDir(dir.path()).absoluteFilePath(
                 QStringLiteral("Resources/VoicePacks/HasFile")));
    QVERIFY2(!result.contains(QStringLiteral("NoFile")),
             "Subdir without meta.mko must NOT be reported");
}

void VoicePackScannerTest::testScanSkipsHiddenDir()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");
    // A qualifying pack.
    QVERIFY2(createVoicePackDir(dir.path(), QStringLiteral("Visible")),
             "precondition: failed to create Visible fixture");
    // A hidden pack (starts with '.') that also has meta.mko — must be
    // filtered out. Mirrors Java's scanHiddenDir_skipsHiddenDir.
    QVERIFY2(createVoicePackDir(dir.path(), QStringLiteral(".hidden")),
             "precondition: failed to create .hidden fixture");

    const QStringList result = core::scanAvailableVoicePacks(dir.path());
    QCOMPARE(result.size(), 1);
    QCOMPARE(result.first(),
             QDir(dir.path()).absoluteFilePath(
                 QStringLiteral("Resources/VoicePacks/Visible")));
    QVERIFY2(!result.contains(QStringLiteral(".hidden")),
             "Hidden subdir must be skipped even if it has meta.mko");
}

void VoicePackScannerTest::testScanNonexistentDirReturnsEmpty()
{
    // A path that definitively does not exist. No throw, no crash, empty list.
    const QString bogus = QStringLiteral("/this/path/does/not/exist/anywhere");
    QVERIFY2(!QFile::exists(bogus), "precondition: bogus path must not exist");

    const QStringList result = core::scanAvailableVoicePacks(bogus);
    QVERIFY2(result.isEmpty(),
             "Nonexistent rendererDir must yield an empty list, not a crash");
}

void VoicePackScannerTest::testScanEmptyRendererDirReturnsEmpty()
{
    const QStringList result = core::scanAvailableVoicePacks(QString());
    QVERIFY2(result.isEmpty(),
             "Empty rendererDir must yield an empty list");
}

QTEST_APPLESS_MAIN(VoicePackScannerTest)
#include "VoicePackScannerTest.moc"
