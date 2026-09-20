// ConfigDirTest — TDD for the cross-platform config/data directory manager
// (T17 + storage-layout revision 2026-09: QStandardPaths-based per-user
// layout on Windows).
//
// Behaviors locked:
//   1. First call to ensureDirectories(basePath) creates the full tree
//      (instances/ + logs/) under basePath.
//   2. Second call on the existing tree is a genuine no-op that still
//      returns true (mkpath is idempotent).
//   3. configDir() / dataDir() ALWAYS use '/' separators with a trailing
//      '/', even on Windows where the native separator is '\\'.
//   4. configDir() == QStandardPaths::writableLocation(AppConfigLocation)
//      + '/' (and the analogous equalities for dataDir /
//      userVoicePacksDir / downloadsDir) — with app name "desktop-pet" and
//      an EMPTY organization name, so every location stays a flat
//      <root>/desktop-pet.
//   5. On Linux the config path is byte-identical to the legacy layout
//      ~/.config/desktop-pet/ (the "Linux stays unchanged" contract).
//   6. migrateLegacyIfNeeded: acting case (copy + "-migrated-backup"
//      rename) is Windows-only; the no-op gates (populated target / missing
//      legacy) and the non-Windows "false, zero side effects" contract.
//
// Test-mode isolation: initTestCase() enables QStandardPaths test mode
// (locations move under ~/.qttest) and sets the app name. QTEST_APPLESS_MAIN
// creates no QCoreApplication instance, and the statics take effect
// immediately — that is the test's "main" hook (no main() to edit).
//
// testLinuxConfigPathIsByteIdentical temporarily DISABLES test mode to
// compare against the real (read-only) home-relative location —
// writableLocation is computed per call, nothing is written.

#include "core/ConfigDir.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QObject>
#include <QStandardPaths>
#include <QString>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

namespace {

// Create a zero-byte file at <dir>/<rel> (parent dirs created as needed).
bool touchFile(const QString& dir, const QString& rel)
{
    const QString path = QDir(dir).absoluteFilePath(rel);
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.close();
    return QFile::exists(path);
}

} // namespace

class ConfigDirTest : public QObject
{
    Q_OBJECT

private slots:
    // Order matters: the byte-identity check toggles QStandardPaths test
    // mode off/on; declared last so every other slot observes steady state.
    void initTestCase();
    void testFirstRunCreatesTree();
    void testSecondCallIsNoop();
    void testPathFormat();
    void testDirsMatchStandardLocations();
    void testMigrateLegacyCopiesTreeAndRenames();
    void testMigrateLegacyNoopWhenTargetPopulated();
    void testMigrateLegacyNoopWhenLegacyMissing();
    void testLinuxConfigPathIsByteIdentical();
};

void ConfigDirTest::initTestCase()
{
    // App identity mirrors main.cpp: name "desktop-pet", NO org name →
    // flat <root>/desktop-pet locations. QTEST_APPLESS_MAIN has no main()
    // body to hook; the statics apply process-wide before any query.
    QCoreApplication::setApplicationName(QStringLiteral("desktop-pet"));
    QCoreApplication::setOrganizationName(QString());
    // Keep every QStandardPaths query (and thus every accessor test) inside
    // the sandboxed ~/.qttest tree — the real user config is never touched.
    QStandardPaths::setTestModeEnabled(true);
}

void ConfigDirTest::testFirstRunCreatesTree()
{
    // Given: a fresh empty temp directory that does NOT yet contain a config
    // tree (QTemporaryDir::path() returns a path with no trailing separator,
    // exactly the shape ensureDirectories() normalizes internally).
    QTemporaryDir base;
    QVERIFY2(base.isValid(), "temporary directory creation failed");
    const QString root = base.path();

    // When: ensureDirectories is called with the temp dir as the injectable
    // root (production calls use the empty default, which resolves to the real
    // configDir()).
    const bool ok = ConfigDir::ensureDirectories(root);

    // Then: the call succeeded AND the two expected subdirectories exist.
    // We do NOT assert the root itself exists separately — mkpath() creating
    // instances/ already proves the root was created (mkpath creates parents).
    QVERIFY2(ok, "ensureDirectories must return true on a writable temp dir");
    QVERIFY2(QDir(root).exists(QStringLiteral("instances")),
             "expected <base>/instances/ to be created by ensureDirectories");
    QVERIFY2(QDir(root).exists(QStringLiteral("logs")),
             "expected <base>/logs/ to be created by ensureDirectories");
}

void ConfigDirTest::testSecondCallIsNoop()
{
    // Given: a config tree already exists under the temp dir (created by the
    // first call below — mirrors the steady-state of a real second launch).
    QTemporaryDir base;
    QVERIFY2(base.isValid(), "temporary directory creation failed");
    const QString root = base.path();
    QVERIFY2(ConfigDir::ensureDirectories(root),
             "precondition: first ensureDirectories call must succeed");

    // When: ensureDirectories is called again on the SAME existing tree.
    const bool ok = ConfigDir::ensureDirectories(root);

    // Then: it returns true (idempotent — mkpath on an existing dir is OK)
    // and both subdirectories are still present (not deleted/recreated).
    QVERIFY2(ok,
             "second ensureDirectories call on an existing tree must return "
             "true (mkpath is idempotent, not a destructive recreate)");
    QVERIFY2(QDir(root).exists(QStringLiteral("instances")),
             "instances/ must still exist after the no-op second call");
    QVERIFY2(QDir(root).exists(QStringLiteral("logs")),
             "logs/ must still exist after the no-op second call");
}

void ConfigDirTest::testPathFormat()
{
    // Given/When: the production accessors are queried.
    const QString cfg = ConfigDir::configDir();
    const QString data = ConfigDir::dataDir();
    const QString inst = ConfigDir::instancesDir();
    const QString logs = ConfigDir::logsDir();
    const QString packs = ConfigDir::userVoicePacksDir();
    const QString dl = ConfigDir::downloadsDir();

    // Then: every returned path uses '/' as the separator AND ends with one
    // (the "<root>subdir" string-concatenation contract). On Windows the
    // native separator is '\\', but QStandardPaths returns '/'-separated
    // paths and ConfigDir appends '/' explicitly — no '\\' and no
    // trailing-separator drift may leak through.
    for (const QString& p : {cfg, data, inst, logs, packs, dl}) {
        QVERIFY2(p.contains(QLatin1Char('/')),
                 "every accessor must contain at least one '/' separator");
        QVERIFY2(!p.contains(QLatin1Char('\\')),
                 "no accessor may contain a backslash on any platform");
        QVERIFY2(p.endsWith(QLatin1Char('/')),
                 "every accessor must end with a trailing '/'");
    }

    // Structural invariants: subdirectories are their parent + suffix.
    // Locking the suffix shape guards against a future rename that forgets
    // to update callers.
    QCOMPARE(inst, cfg + QStringLiteral("instances/"));
    QCOMPARE(logs, cfg + QStringLiteral("logs/"));
    QCOMPARE(packs, data + QStringLiteral("VoicePacks/"));
    QCOMPARE(dl, data + QStringLiteral("downloads/"));
}

void ConfigDirTest::testDirsMatchStandardLocations()
{
    // The storage-layout contract: every accessor is exactly its
    // QStandardPaths location + '/'. App name "desktop-pet" with an empty
    // org name keeps each location a flat <root>/desktop-pet (an org name
    // would insert an extra path segment and break this equality).
    QCOMPARE(ConfigDir::configDir(),
             QStandardPaths::writableLocation(
                 QStandardPaths::AppConfigLocation) + QLatin1Char('/'));
    // dataDir must be the LOCAL (non-roaming) location — %LOCALAPPDATA% on
    // Windows, ~/.local/share on Linux.
    QCOMPARE(ConfigDir::dataDir(),
             QStandardPaths::writableLocation(
                 QStandardPaths::AppLocalDataLocation) + QLatin1Char('/'));

    // Flat-layout guard: both locations end in "/desktop-pet" (no org-name
    // segment, no nested <org>/<app> tree).
    QVERIFY2(ConfigDir::configDir().endsWith(QStringLiteral("/desktop-pet/")),
             "configDir() must be a flat <root>/desktop-pet/ "
             "(empty organization name)");
    QVERIFY2(ConfigDir::dataDir().endsWith(QStringLiteral("/desktop-pet/")),
             "dataDir() must be a flat <root>/desktop-pet/ "
             "(empty organization name)");
    QVERIFY2(ConfigDir::configDir() != ConfigDir::dataDir(),
             "config (Roaming) and data (Local) must be distinct roots");
}

void ConfigDirTest::testMigrateLegacyCopiesTreeAndRenames()
{
    // Given: a populated legacy tree (nested dirs + files, mirroring the
    // legacy layout's instances/ + logs/ + top-level state files) and an
    // EMPTY new base.
    QTemporaryDir legacy;
    QTemporaryDir target;
    QVERIFY2(legacy.isValid() && target.isValid(),
             "temporary directory creation failed");
    QVERIFY2(touchFile(legacy.path(), QStringLiteral("app.db")),
             "precondition: failed to create legacy app.db");
    QVERIFY2(touchFile(legacy.path(),
                       QStringLiteral("instances/{uuid-1}.json")),
             "precondition: failed to create legacy instance config");
    QVERIFY2(touchFile(legacy.path(),
                       QStringLiteral("instances/nested/deep.json")),
             "precondition: failed to create nested legacy file");
    QVERIFY2(touchFile(legacy.path(), QStringLiteral("logs/panel.log")),
             "precondition: failed to create legacy log file");

    // When: the migration runs with both paths injected.
    const bool migrated = ConfigDir::migrateLegacyIfNeeded(legacy.path(),
                                                           target.path());
#ifdef Q_OS_WIN
    // Then: the copy ran, every file (incl. nested) exists at the new base
    // with relative paths preserved, and the legacy dir was renamed to
    // "<legacy>-migrated-backup".
    QVERIFY2(migrated, "migration of a legacy tree into an empty target "
                       "must return true on Windows");
    QVERIFY2(QFile::exists(QDir(target.path()).absoluteFilePath(
                 QStringLiteral("app.db"))),
             "legacy app.db must exist at the new base after migration");
    QVERIFY2(QFile::exists(QDir(target.path()).absoluteFilePath(
                 QStringLiteral("instances/{uuid-1}.json"))),
             "legacy instance config must exist at the new base");
    QVERIFY2(QFile::exists(QDir(target.path()).absoluteFilePath(
                 QStringLiteral("instances/nested/deep.json"))),
             "nested legacy files must keep their relative paths");
    QVERIFY2(QFile::exists(QDir(target.path()).absoluteFilePath(
                 QStringLiteral("logs/panel.log"))),
             "legacy log file must exist at the new base");
    QVERIFY2(!QDir(legacy.path()).exists(),
             "the legacy dir itself must be renamed away");
    QVERIFY2(QDir(legacy.path() + QStringLiteral("-migrated-backup")).exists(),
             "the legacy dir must survive as <legacy>-migrated-backup");
#else
    // Non-Windows: compile-time no-op — false, zero side effects (the Linux
    // config path is unchanged, so there is nothing to migrate).
    QVERIFY2(!migrated,
             "non-Windows migrateLegacyIfNeeded must return false");
    QVERIFY2(QFile::exists(QDir(legacy.path()).absoluteFilePath(
                 QStringLiteral("app.db"))),
             "non-Windows migration must not touch the legacy tree");
    QVERIFY2(!QFile::exists(QDir(target.path()).absoluteFilePath(
                 QStringLiteral("app.db"))),
             "non-Windows migration must not copy anything");
#endif
}

void ConfigDirTest::testMigrateLegacyNoopWhenTargetPopulated()
{
    // Given: a legacy tree AND a target that already has content (the app
    // ran once with the new layout — copying over it could clobber newer
    // files).
    QTemporaryDir legacy;
    QTemporaryDir target;
    QVERIFY2(legacy.isValid() && target.isValid(),
             "temporary directory creation failed");
    QVERIFY2(touchFile(legacy.path(), QStringLiteral("app.db")),
             "precondition: failed to create legacy app.db");
    QVERIFY2(touchFile(target.path(), QStringLiteral("sentinel.json")),
             "precondition: failed to create target sentinel");

    // When: the migration runs.
    const bool migrated = ConfigDir::migrateLegacyIfNeeded(legacy.path(),
                                                           target.path());
    // Then: no-op in both directions — returns false, target untouched.
    QVERIFY2(!migrated, "a populated target must gate the migration off");
#ifdef Q_OS_WIN
    QVERIFY2(!QFile::exists(QDir(target.path()).absoluteFilePath(
                 QStringLiteral("app.db"))),
             "nothing may be copied into a populated target");
    QVERIFY2(QDir(legacy.path()).exists(),
             "the legacy dir must be left in place when migration is gated");
#endif
}

void ConfigDirTest::testMigrateLegacyNoopWhenLegacyMissing()
{
    // Given: NO legacy dir (fresh install) — QTemporaryDir yields a valid
    // path, then we delete the tree to obtain a non-existent path.
    QTemporaryDir legacy;
    QVERIFY2(legacy.isValid(), "temporary directory creation failed");
    const QString legacyPath = legacy.path();
    QVERIFY2(legacy.remove(), "precondition: failed to remove temp tree");
    QTemporaryDir target;
    QVERIFY2(target.isValid(), "temporary directory creation failed");

    // When/Then: false on every platform; on Windows nothing is created.
    const bool migrated = ConfigDir::migrateLegacyIfNeeded(legacyPath,
                                                           target.path());
    QVERIFY2(!migrated, "a missing legacy dir must yield a no-op (false)");
#ifdef Q_OS_WIN
    QVERIFY2(QDir(target.path()).entryList(
                 QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty(),
             "a missing legacy dir must leave the target untouched");
    QVERIFY2(!QDir(legacyPath + QStringLiteral("-migrated-backup")).exists(),
             "no backup dir may be created when nothing was migrated");
#endif
}

void ConfigDirTest::testLinuxConfigPathIsByteIdentical()
{
    // The "Linux stays BYTE-IDENTICAL" contract: outside test mode,
    // configDir() must equal the legacy ~/.config/desktop-pet/ exactly.
    // Toggle test mode OFF for the query (read-only — nothing is created),
    // then restore it for any code that runs later.
    QStandardPaths::setTestModeEnabled(false);
#ifdef Q_OS_LINUX
    QCOMPARE(ConfigDir::configDir(),
             QDir::homePath() + QStringLiteral("/.config/desktop-pet/"));
#else
    // Windows: no legacy-path equality — the new Roaming location is locked
    // by testDirsMatchStandardLocations instead. Just re-assert sanity.
    QVERIFY(ConfigDir::configDir().endsWith(QLatin1Char('/')));
#endif
    QStandardPaths::setTestModeEnabled(true);
}

QTEST_APPLESS_MAIN(ConfigDirTest)
#include "ConfigDirTest.moc"
