// ConfigDirTest — TDD for the cross-platform config directory manager (T17).
//
// Four behaviors locked (matches the task spec's MUST DO list):
//   1. First call to ensureDirectories(basePath) creates the full tree
//      (instances/ + logs/) under basePath.
//   2. Second call on the existing tree is a genuine no-op that still
//      returns true (mkpath is idempotent).
//   3. configDir() / instancesDir() / logsDir() ALWAYS use '/' separators,
//      even on Windows where the native separator is '\\'.
//   4. configDir() contains the canonical ".config/desktop-pet" subpath
//      (the legacy config location under the user's home).
//
// QTEST_APPLESS_MAIN: no event loop needed. QTemporaryDir + QDir::exists() +
// QString::contains() are all synchronous. The real (non-injected) configDir()
// is exercised in tests 3 and 4; it touches QDir::homePath() but does not
// require a QCoreApplication.

#include "core/ConfigDir.hpp"

#include <QDir>
#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

class ConfigDirTest : public QObject
{
    Q_OBJECT

private slots:
    void testFirstRunCreatesTree();
    void testSecondCallIsNoop();
    void testPathFormat();
    void testConfigDirContainsSubpath();
};

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
    // configDir() under the user's home).
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
    // Given/When: the three production accessors are queried (no basePath
    // injection — these always reflect the real home-relative paths, which is
    // exactly what Logging / config managers consume in production).
    const QString cfg = ConfigDir::configDir();
    const QString inst = ConfigDir::instancesDir();
    const QString logs = ConfigDir::logsDir();

    // Then: every returned path uses '/' as the separator. On Windows the
    // native separator is '\\', but ConfigDir.cpp builds paths by concatenating
    // string literals that already contain '/' (kConfigSubpath, "instances/",
    // "logs/") and QDir::homePath() returns a '/'-normalized path on Qt — so
    // no '\\' may ever leak through. A backslash in any of the three would
    // break the byte-for-byte compatibility contract with the legacy config
    // format.
    QVERIFY2(cfg.contains(QLatin1Char('/')),
             "configDir() must contain at least one '/' separator");
    QVERIFY2(!cfg.contains(QLatin1Char('\\')),
             "configDir() must NOT contain a backslash on any platform");
    QVERIFY2(!inst.contains(QLatin1Char('\\')),
             "instancesDir() must NOT contain a backslash on any platform");
    QVERIFY2(!logs.contains(QLatin1Char('\\')),
             "logsDir() must NOT contain a backslash on any platform");

    // Structural invariant: instances/ and logs/ are configDir() + suffix.
    // Locking the suffix shape guards against a future rename that forgets to
    // update callers, and confirms no trailing-separator drift.
    QCOMPARE(inst, cfg + QStringLiteral("instances/"));
    QCOMPARE(logs, cfg + QStringLiteral("logs/"));
}

void ConfigDirTest::testConfigDirContainsSubpath()
{
    // Given/When: the production configDir() is queried.
    const QString cfg = ConfigDir::configDir();

    // Then: it MUST contain the canonical ".config/desktop-pet" subpath. This
    // is the byte-for-byte compatibility contract with the legacy config
    // format. Any deviation here would orphan config state written by older
    // builds.
    QVERIFY2(cfg.contains(QStringLiteral("/.config/desktop-pet")),
             "configDir() must contain the canonical '/.config/desktop-pet' "
             "subpath to stay compatible with the legacy config format");
}

QTEST_APPLESS_MAIN(ConfigDirTest)
#include "ConfigDirTest.moc"
