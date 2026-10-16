// VoicePackScannerTest — Phase 5 Wave 8 todo 19 TDD for the voice-pack
// directory scanner (+ storage-layout revision: dual-source overload).
//
// Behaviors locked (mirrors ModelScannerTest + the Java reference
// VoicePackScannerTest):
//   1. A fake <tmp>/Resources/VoicePacks/<Pack>/meta.mko qualifies and is
//      reported by name.
//   2. Multiple qualifying packs are returned sorted case-insensitively.
//   3. A subdir WITHOUT meta.mko is filtered out.
//   4. A hidden subdir (starting with '.') is skipped even if it has meta.mko.
//   5. A nonexistent rendererDir → empty list (no throw, no crash).
//   6. Empty rendererDir → empty list.
//   7. Dual-source overload: built-in + user packs merge (user dir IS the
//      VoicePacks dir itself, not a Resources parent).
//   8. Dual-source name collision → the USER pack wins.
//   9. A missing user packs dir → built-in only (graceful fallback).
//  10. Single-arg overload == dual overload with an empty userPacksDir.
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
// scanAvailableVoicePacks expects in its BUILT-IN source. Returns true on
// success.
bool createVoicePackDir(const QString& root, const QString& packName)
{
    const QString packDir = QDir(root).absoluteFilePath(
        QStringLiteral("Resources/VoicePacks/") + packName);
    if (!QDir().mkpath(packDir))
        return false;
    return touchFile(packDir, QStringLiteral("meta.mko"));
}

// Create <packsDir>/<packName>/meta.mko for the USER source. userPacksDir is
// the VoicePacks directory ITSELF (e.g. ConfigDir::userVoicePacksDir()), so
// the fixture has NO Resources/VoicePacks prefix.
bool createUserVoicePack(const QString& packsDir, const QString& packName)
{
    const QString packDir = QDir(packsDir).absoluteFilePath(packName);
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
    void testDualDirScanMergesBothSources();
    void testDualDirUserWinsCollision();
    void testDualDirMissingUserDirFallsBackToBuiltin();
    void testSingleArgOverloadMatchesDualWithEmptyUserDir();

    // ── v1.13 scoped scan (resource isolation, rev2 §3.2a) ─────────────
    void testScopedScanOwnerNameKey();
    void testScopedScanMissingRootHostOnly();
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

void VoicePackScannerTest::testDualDirScanMergesBothSources()
{
    // Given: one BUILT-IN pack (under <renderer>/Resources/VoicePacks/) and
    // a separate USER packs dir holding a different pack. The user dir IS
    // the VoicePacks dir itself — no Resources prefix (the shape
    // ConfigDir::userVoicePacksDir() hands over).
    QTemporaryDir rendererDir;
    QTemporaryDir userPacksDir;
    QVERIFY2(rendererDir.isValid() && userPacksDir.isValid(),
             "temporary directory creation failed");
    QVERIFY2(createVoicePackDir(rendererDir.path(),
                                QStringLiteral("BuiltinPack")),
             "precondition: failed to create BuiltinPack fixture");
    QVERIFY2(createUserVoicePack(userPacksDir.path(),
                                 QStringLiteral("UserPack")),
             "precondition: failed to create UserPack fixture");

    // When: the dual-source overload scans both.
    const QStringList result = core::scanAvailableVoicePacks(
        rendererDir.path(), userPacksDir.path());

    // Then: both packs appear as ABSOLUTE paths inside their own source
    // tree. Membership (not order) is asserted — the case-insensitive sort
    // keys on the FULL path, and the two QTemporaryDir roots have random
    // names, so the cross-source order is not deterministic.
    QCOMPARE(result.size(), 2);
    QVERIFY2(result.contains(QDir(rendererDir.path()).absoluteFilePath(
                 QStringLiteral("Resources/VoicePacks/BuiltinPack"))),
             "the built-in pack must be reported");
    QVERIFY2(result.contains(QDir(userPacksDir.path()).absoluteFilePath(
                 QStringLiteral("UserPack"))),
             "the user pack must be reported");
}

void VoicePackScannerTest::testDualDirUserWinsCollision()
{
    // Given: the SAME pack dir basename in both sources.
    QTemporaryDir rendererDir;
    QTemporaryDir userPacksDir;
    QVERIFY2(rendererDir.isValid() && userPacksDir.isValid(),
             "temporary directory creation failed");
    QVERIFY2(createVoicePackDir(rendererDir.path(),
                                QStringLiteral("SameName")),
             "precondition: failed to create built-in SameName fixture");
    QVERIFY2(createUserVoicePack(userPacksDir.path(),
                                 QStringLiteral("SameName")),
             "precondition: failed to create user SameName fixture");

    // When: the dual-source overload scans both.
    const QStringList result = core::scanAvailableVoicePacks(
        rendererDir.path(), userPacksDir.path());

    // Then: exactly ONE entry survives and it is the USER pack (a downloaded
    // pack supersedes a same-named bundled one — storage-layout rule).
    QCOMPARE(result.size(), 1);
    QCOMPARE(result.first(),
             QDir(userPacksDir.path()).absoluteFilePath(
                 QStringLiteral("SameName")));
}

void VoicePackScannerTest::testDualDirMissingUserDirFallsBackToBuiltin()
{
    // Given: a built-in pack and a user packs dir that does not exist
    // (fresh install before the first download — the owner creates the dir
    // lazily).
    QTemporaryDir rendererDir;
    QVERIFY2(rendererDir.isValid(), "temporary directory creation failed");
    QVERIFY2(createVoicePackDir(rendererDir.path(),
                                QStringLiteral("OnlyBuiltin")),
             "precondition: failed to create OnlyBuiltin fixture");
    const QString missingUserDir =
        rendererDir.path() + QStringLiteral("/no/such/user-packs");

    // When: the dual-source overload scans with the missing user dir.
    const QStringList result = core::scanAvailableVoicePacks(
        rendererDir.path(), missingUserDir);

    // Then: graceful fallback — the built-in pack is still reported.
    QCOMPARE(result.size(), 1);
    QCOMPARE(result.first(),
             QDir(rendererDir.path()).absoluteFilePath(
                 QStringLiteral("Resources/VoicePacks/OnlyBuiltin")));
}

void VoicePackScannerTest::testSingleArgOverloadMatchesDualWithEmptyUserDir()
{
    // Given: a built-in pack.
    QTemporaryDir rendererDir;
    QVERIFY2(rendererDir.isValid(), "temporary directory creation failed");
    QVERIFY2(createVoicePackDir(rendererDir.path(),
                                QStringLiteral("CompatPack")),
             "precondition: failed to create CompatPack fixture");

    // When: both overloads scan it (single-arg vs empty userPacksDir).
    const QStringList single =
        core::scanAvailableVoicePacks(rendererDir.path());
    const QStringList dual =
        core::scanAvailableVoicePacks(rendererDir.path(), QString());

    // Then: identical results — the single-arg overload is pure sugar over
    // the dual one with an empty user source.
    QCOMPARE(single, dual);
    QCOMPARE(single.size(), 1);
}

// ── v1.13 scoped scan (resource isolation, rev2 §3.2a) ──────────────────

namespace {

// Create <pluginDataRoot>/<owner>/VoicePacks/<packName>/meta.mko — the
// plugin-domain layout (rev2 §2).
bool createPluginVoicePack(const QString& pluginDataRoot,
                           const QString& owner, const QString& packName)
{
    const QString packDir = QDir(pluginDataRoot).absoluteFilePath(
        QStringLiteral("%1/VoicePacks/%2").arg(owner, packName));
    if (!QDir().mkpath(packDir))
        return false;
    return touchFile(packDir, QStringLiteral("meta.mko"));
}

} // namespace

// The (owner, name) KEY rule: the SAME pack name in TWO plugin domains is
// returned TWICE (distinct dirPaths), and a plugin pack never shadows — nor
// is shadowed by — a host pack. The host dual-source user-wins rule keeps
// applying WITHIN the host domain only.
void VoicePackScannerTest::testScopedScanOwnerNameKey()
{
    QTemporaryDir rendererDir;
    QTemporaryDir userPacksDir;
    QTemporaryDir pluginRoot;
    QVERIFY2(rendererDir.isValid() && userPacksDir.isValid()
                 && pluginRoot.isValid(),
             "temporary directory creation failed");

    // Host: a built-in "Shared" pack that the USER copy shadows (user-wins
    // within host), plus a plain host pack.
    QVERIFY2(createVoicePackDir(rendererDir.path(),
                                QStringLiteral("Shared")),
             "precondition: failed to create builtin Shared fixture");
    QVERIFY2(createUserVoicePack(userPacksDir.path(),
                                 QStringLiteral("Shared")),
             "precondition: failed to create user Shared fixture");
    QVERIFY2(createUserVoicePack(userPacksDir.path(),
                                QStringLiteral("HostOnly")),
             "precondition: failed to create HostOnly fixture");

    // Two plugin domains, BOTH carrying a pack named "Shared" (same name
    // as the host pack).
    QVERIFY2(createPluginVoicePack(pluginRoot.path(), QStringLiteral("plugA"),
                                   QStringLiteral("Shared")),
             "precondition: failed to create plugA Shared fixture");
    QVERIFY2(createPluginVoicePack(pluginRoot.path(), QStringLiteral("plugB"),
                                   QStringLiteral("Shared")),
             "precondition: failed to create plugB Shared fixture");

    const QVector<core::ScannedPack> result = core::scanVoicePacksScoped(
        rendererDir.path(), userPacksDir.path(), pluginRoot.path());

    // 1 host "Shared" (the USER twin — user-wins) + 1 HostOnly + 2 plugin
    // "Shared" entries = 4 total. The plugin entries NEVER replace the host
    // one or each other (rev2 §3.2a).
    QCOMPARE(result.size(), 4);

    const QString hostShared =
        QDir(userPacksDir.path()).absoluteFilePath(QStringLiteral("Shared"));
    int hostSharedCount = 0, hostOnlyCount = 0;
    int plugASharedCount = 0, plugBSharedCount = 0;
    for (const core::ScannedPack& p : result) {
        if (p.owner.isEmpty()) {
            if (p.dirPath == hostShared)
                ++hostSharedCount;
            else if (p.dirPath.endsWith(QLatin1String("/HostOnly")))
                ++hostOnlyCount;
        } else if (p.owner == QLatin1String("plugA")) {
            ++plugASharedCount;
            QVERIFY2(p.dirPath.endsWith(QStringLiteral("/plugA/VoicePacks/Shared")),
                     qPrintable(QStringLiteral("plugA dirPath wrong: %1").arg(p.dirPath)));
        } else if (p.owner == QLatin1String("plugB")) {
            ++plugBSharedCount;
            QVERIFY2(p.dirPath.endsWith(QStringLiteral("/plugB/VoicePacks/Shared")),
                     qPrintable(QStringLiteral("plugB dirPath wrong: %1").arg(p.dirPath)));
        }
    }
    QCOMPARE(hostSharedCount, 1); // user twin won the WITHIN-host collision
    QCOMPARE(hostOnlyCount, 1);
    QCOMPARE(plugASharedCount, 1);
    QCOMPARE(plugBSharedCount, 1);

    // Host block first (owner "" sorts before non-empty owners).
    QCOMPARE(result.first().owner, QString());
}

// Missing pluginDataRoot → host-only contribution; empty root is the
// explicit host-only form. Both never throw.
void VoicePackScannerTest::testScopedScanMissingRootHostOnly()
{
    QTemporaryDir rendererDir;
    QVERIFY2(rendererDir.isValid(), "temporary directory creation failed");
    QVERIFY2(createVoicePackDir(rendererDir.path(),
                                QStringLiteral("Solo")),
             "precondition: failed to create Solo fixture");

    const QVector<core::ScannedPack> missing = core::scanVoicePacksScoped(
        rendererDir.path(), QString(),
        QDir(rendererDir.path()).absoluteFilePath(QStringLiteral("NoSuchRoot")));
    QCOMPARE(missing.size(), 1);
    QCOMPARE(missing.at(0).owner, QString());

    const QVector<core::ScannedPack> emptyRoot = core::scanVoicePacksScoped(
        rendererDir.path(), QString(), QString());
    QCOMPARE(emptyRoot.size(), 1);
    QCOMPARE(emptyRoot.at(0).owner, QString());
}

QTEST_APPLESS_MAIN(VoicePackScannerTest)
#include "VoicePackScannerTest.moc"
