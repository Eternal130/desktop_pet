// InstanceConfigManagerTest — TDD for the per-instance JSON CRUD (T20).
//
// Five behaviors locked (matches the task spec MUST DO list):
//   1. save → load round-trip: every field survives a write+read cycle.
//   2. loadAll returns sorted-by-id regardless of filesystem enumeration
//      order (NTFS/ext4/tmpfs differ — the contract must not depend on it).
//   3. deleteInstance removes the file and is idempotent (re-delete succeeds).
//   4. Corrupt JSON files are skipped with a WARN, not fatal: loadAll still
//      returns the valid instances.
//   5. Atomic write guarantee: a stale .tmp file (simulated crash) does not
//      corrupt or replace the pre-existing real file.
//
// QTEST_APPLESS_MAIN: no event loop needed — all operations (QSaveFile,
// QFile, QDir::entryList) are synchronous. The manager never touches the
// network or Qt event-driven I/O.

#include "core/InstanceConfigManager.hpp"
#include "core/ConfigDir.hpp"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

namespace {

// Build a fully-populated InstanceConfig with a distinctive value in every
// field. Reused across round-trip / sort / delete cases so a single missed
// field surfaces immediately. The id is provided by the caller so multiple
// distinct instances can coexist in loadAll tests.
InstanceConfig makeInstance(const QString& id)
{
    InstanceConfig c;
    c.id = id;
    c.label = QStringLiteral("label-") + id;
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
    c.voicePack = QStringLiteral("voice-") + id;
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
    return c;
}

// Create the instances/ subdirectory under the temp dir (mirrors what
// ConfigDir::ensureDirectories(basePath) would do in production, but we don't
// depend on T17 here — InstanceConfigManager itself is what's under test).
QString makeInstancesDir(const QTemporaryDir& base)
{
    const QString inst = base.path() + QStringLiteral("/instances");
    QDir().mkpath(inst);
    return inst + QStringLiteral("/");
}

} // namespace

class InstanceConfigManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void testSaveLoadRoundTrip();
    void testLoadAllSortedById();
    void testDeleteInstance();
    void testCorruptFileSkipped();
    void testAtomicWriteGuarantee();
};

void InstanceConfigManagerTest::testSaveLoadRoundTrip()
{
    // Given: a fresh manager rooted at a temp dir, and a fully-populated
    // instance config with a distinctive value in every field.
    QTemporaryDir base;
    QVERIFY2(base.isValid(), "temporary directory creation failed");
    InstanceConfigManager mgr(base.path());
    const InstanceConfig original = makeInstance(
        QStringLiteral("11111111-2222-3333-4444-555555555555"));

    // When: save the config, then load it back by id.
    QVERIFY2(mgr.save(original), "save must succeed on a writable temp dir");
    const auto loaded = mgr.load(original.id);

    // Then: load returned a value (not nullopt), and every field matches the
    // original — operator== is member-wise (28 fields), so a single missed
    // serialization key fails here.
    QVERIFY2(loaded.has_value(),
             "load must return a value after a successful save");
    QCOMPARE(*loaded, original);
    // Belt-and-suspenders on the trickiest fields that defaults could mask.
    QCOMPARE(loaded->voicePack, QStringLiteral("voice-11111111-2222-3333-4444-555555555555"));
    QCOMPARE(loaded->autoStart, true);
    QCOMPARE(loaded->layoutOffsetX, -12.5);
    QCOMPARE(loaded->subtitleStylePreset, QStringLiteral("终端黑客"));
}

void InstanceConfigManagerTest::testLoadAllSortedById()
{
    // Given: a fresh manager with three instances saved in NON-alphabetical
    // order. loadAll's contract is to return them sorted by id, not in
    // filesystem order — the panel depends on a stable order regardless of
    // the underlying filesystem's enumeration behavior.
    QTemporaryDir base;
    QVERIFY2(base.isValid(), "temporary directory creation failed");
    InstanceConfigManager mgr(base.path());
    QVERIFY2(mgr.save(makeInstance(QStringLiteral("c"))), "save c failed");
    QVERIFY2(mgr.save(makeInstance(QStringLiteral("a"))), "save a failed");
    QVERIFY2(mgr.save(makeInstance(QStringLiteral("b"))), "save b failed");

    // When: loadAll.
    const QList<InstanceConfig> all = mgr.loadAll();

    // Then: exactly three instances, in alphabetical id order. A swap or a
    // missing sort step would put "c" first (filesystem enumeration order
    // on Windows tmpfs often matches insertion order, masking the bug).
    QCOMPARE(all.size(), 3);
    QCOMPARE(all[0].id, QStringLiteral("a"));
    QCOMPARE(all[1].id, QStringLiteral("b"));
    QCOMPARE(all[2].id, QStringLiteral("c"));
}

void InstanceConfigManagerTest::testDeleteInstance()
{
    // Given: a saved instance.
    QTemporaryDir base;
    QVERIFY2(base.isValid(), "temporary directory creation failed");
    InstanceConfigManager mgr(base.path());
    const QString id = QStringLiteral("deadbeef-0000-0000-0000-000000000001");
    QVERIFY2(mgr.save(makeInstance(id)), "save precondition failed");
    QVERIFY2(mgr.load(id).has_value(), "load precondition must see saved file");

    // When: delete the instance.
    const bool removed = mgr.deleteInstance(id);

    // Then: deleteInstance returned true, the file is gone, and load now
    // returns nullopt.
    QVERIFY2(removed, "deleteInstance must return true when it removed a file");

    // And: re-deleting is idempotent — a second call on the now-absent file
    // still succeeds (matches QFile::remove semantics and the spec's
    // "returns true if deleted OR already absent" contract).
    QVERIFY2(mgr.deleteInstance(id),
             "deleteInstance must be idempotent: deleting an absent file is success");
    QVERIFY2(!mgr.load(id).has_value(),
             "load after delete must return nullopt");
    // And the file is physically gone from disk: the instances/ dir under the
    // temp root contains no <id>.json entry. (instanceFilePath is private, so
    // reconstruct the path directly — this also verifies the on-disk layout
    // matches the documented <basePath>/instances/<id>.json shape.)
    const QString expectedPath = makeInstancesDir(base) + id + QStringLiteral(".json");
    QVERIFY2(!QFile::exists(expectedPath),
             "instance file must be physically gone from disk after delete");
}

void InstanceConfigManagerTest::testCorruptFileSkipped()
{
    // Given: a manager with TWO valid instances saved, PLUS a third file in
    // the same instances/ directory whose contents are NOT valid JSON. The
    // corrupt file is written directly (not via mgr.save) because we are
    // simulating an externally-corrupted state (disk corruption, partial
    // write from a different tool, manual edit gone wrong).
    QTemporaryDir base;
    QVERIFY2(base.isValid(), "temporary directory creation failed");
    InstanceConfigManager mgr(base.path());
    QVERIFY2(mgr.save(makeInstance(QStringLiteral("aaa"))), "save aaa failed");
    QVERIFY2(mgr.save(makeInstance(QStringLiteral("bbb"))), "save bbb failed");

    const QString corruptPath = makeInstancesDir(base) +
        QStringLiteral("garbage.json");
    {
        QFile f(corruptPath);
        QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Truncate),
                 "precondition: cannot open corrupt-file fixture for write");
        // Not JSON. Garbage that QJsonDocument::fromJson will reject with a
        // parse error (missing initial '{' or '[').
        f.write("{ this is not :: valid JSON ^^^");
        f.close();
    }
    QVERIFY2(QFile::exists(corruptPath),
             "precondition: corrupt fixture file must exist");

    // When: loadAll.
    const QList<InstanceConfig> all = mgr.loadAll();

    // Then: exactly TWO instances returned — the valid ones — NOT three
    // (which would mean the corrupt file parsed) and NOT zero (which would
    // mean loadAll crashed or aborted on the first corrupt file). The
    // corrupt file was skipped with a WARN, the valid ones survived.
    QCOMPARE(all.size(), 2);
    QCOMPARE(all[0].id, QStringLiteral("aaa"));
    QCOMPARE(all[1].id, QStringLiteral("bbb"));
    // And the corrupt file is still physically on disk (loadAll does not
    // delete corrupt files — only skips them).
    QVERIFY2(QFile::exists(corruptPath),
             "loadAll must NOT delete corrupt files, only skip them");
}

void InstanceConfigManagerTest::testAtomicWriteGuarantee()
{
    // Given: a pre-existing VALID file for some id, plus a stale .tmp artifact
    // that simulates a crash mid-write (the .tmp was created but the rename
    // never happened). The stale .tmp contents are deliberately DIFFERENT
    // from the real file so a miswire (e.g. save() doing a non-atomic
    // overwrite) would show up as the real file changing.
    QTemporaryDir base;
    QVERIFY2(base.isValid(), "temporary directory creation failed");
    InstanceConfigManager mgr(base.path());
    const QString id = QStringLiteral("cafe-0000-0000-0000-000000000001");

    const InstanceConfig original = makeInstance(id);
    QVERIFY2(mgr.save(original), "save precondition failed");
    const QString realPath = makeInstancesDir(base) + id + QStringLiteral(".json");
    QVERIFY2(QFile::exists(realPath), "precondition: real file must exist");
    const QByteArray originalBytes = [&]() {
        QFile f(realPath);
        f.open(QIODevice::ReadOnly);
        return f.readAll();
    }();
    QVERIFY2(!originalBytes.isEmpty(),
             "precondition: original file must be non-empty");

    // Simulate the crash: write the .tmp file that a half-finished atomic
    // write would have left behind. Contents are deliberately garbage so
    // that if the manager ever picks them up, the test fails.
    const QString staleTmp = realPath + QStringLiteral(".tmp");
    {
        QFile f(staleTmp);
        QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Truncate),
                 "precondition: cannot open stale .tmp for write");
        f.write("{\"id\":\"PARTIAL_CRASH_GARBAGE\"");
        f.close();
    }
    QVERIFY2(QFile::exists(staleTmp),
             "precondition: stale .tmp must exist");

    // When: load the instance back.
    const auto loaded = mgr.load(id);

    // Then: the loaded config is the ORIGINAL, not the .tmp garbage. The real
    // file on disk is byte-for-byte identical to what was there before the
    // simulated crash. No half-written file survives.
    QVERIFY2(loaded.has_value(), "load must still see the original file");
    QCOMPARE(*loaded, original);
    QVERIFY2(loaded->id != QStringLiteral("PARTIAL_CRASH_GARBAGE"),
             "load must NOT pick up the stale .tmp contents");

    // And the on-disk real file is unchanged (byte-for-byte).
    QFile rf(realPath);
    QVERIFY2(rf.open(QIODevice::ReadOnly),
             "real file must still be readable after the crash simulation");
    const QByteArray afterBytes = rf.readAll();
    rf.close();
    QCOMPARE(afterBytes, originalBytes);

    // And loadAll does NOT pick up the .tmp as a phantom instance (nameFilter
    // is "*.json", not "*.json*").
    const QList<InstanceConfig> all = mgr.loadAll();
    QCOMPARE(all.size(), 1);
    QCOMPARE(all[0].id, id);

    // Cleanup the .tmp so QTemporaryDir's auto-cleanup is tidy.
    QFile::remove(staleTmp);
}

QTEST_APPLESS_MAIN(InstanceConfigManagerTest)
#include "InstanceConfigManagerTest.moc"
