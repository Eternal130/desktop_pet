// InstanceConfigManagerTest 鈥?SQLite-backed per-instance config CRUD.
//
// Behaviors locked (SQLite semantics 鈥?same API, no JSON files):
//   1. save 鈫?load round-trip: every field survives.
//   2. loadAll returns sorted-by-uuid regardless of insertion order.
//   3. deleteInstance removes the row and is idempotent.
//   4. Corrupt rows are skipped by loadAll / load returns nullopt.
//
// QTEST_MAIN: QSqlDatabase is synchronous.

#include "core/InstanceConfigManager.hpp"
#include "core/DatabaseManager.hpp"

#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

namespace {

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
    c.subtitleStylePreset = QStringLiteral("缁堢榛戝");
    return c;
}

} // namespace

class InstanceConfigManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void testSaveLoadRoundTrip();
    void testLoadAllSortedById();
    void testDeleteInstance();
    void testCorruptRowSkipped();
};

void InstanceConfigManagerTest::testSaveLoadRoundTrip()
{
    QTemporaryDir base;
    QVERIFY2(base.isValid(), "temporary directory creation failed");
    InstanceConfigManager mgr(base.path());
    const InstanceConfig original = makeInstance(
        QStringLiteral("11111111-2222-3333-4444-555555555555"));

    QVERIFY2(mgr.save(original), "save must succeed on a writable temp dir");
    const auto loaded = mgr.load(original.id);

    QVERIFY2(loaded.has_value(),
             "load must return a value after a successful save");
    QCOMPARE(*loaded, original);
    QCOMPARE(loaded->voicePack,
             QStringLiteral("voice-11111111-2222-3333-4444-555555555555"));
    QCOMPARE(loaded->autoStart, true);
    QCOMPARE(loaded->layoutOffsetX, -12.5);
    QCOMPARE(loaded->subtitleStylePreset, QStringLiteral("缁堢榛戝"));

    // Absent id 鈫?nullopt.
    QVERIFY(!mgr.load(QStringLiteral("nope")).has_value());
}

void InstanceConfigManagerTest::testLoadAllSortedById()
{
    QTemporaryDir base;
    QVERIFY2(base.isValid(), "temporary directory creation failed");
    InstanceConfigManager mgr(base.path());
    QVERIFY2(mgr.save(makeInstance(QStringLiteral("c"))), "save c failed");
    QVERIFY2(mgr.save(makeInstance(QStringLiteral("a"))), "save a failed");
    QVERIFY2(mgr.save(makeInstance(QStringLiteral("b"))), "save b failed");

    const QList<InstanceConfig> all = mgr.loadAll();

    QCOMPARE(all.size(), 3);
    QCOMPARE(all[0].id, QStringLiteral("a"));
    QCOMPARE(all[1].id, QStringLiteral("b"));
    QCOMPARE(all[2].id, QStringLiteral("c"));
}

void InstanceConfigManagerTest::testDeleteInstance()
{
    QTemporaryDir base;
    QVERIFY2(base.isValid(), "temporary directory creation failed");
    InstanceConfigManager mgr(base.path());
    const QString id = QStringLiteral("deadbeef-0000-0000-0000-000000000001");
    QVERIFY2(mgr.save(makeInstance(id)), "save precondition failed");
    QVERIFY2(mgr.load(id).has_value(), "load precondition must see the row");

    QVERIFY2(mgr.deleteInstance(id), "deleteInstance must return true");
    QVERIFY2(mgr.deleteInstance(id),
             "deleteInstance must be idempotent on an absent row");
    QVERIFY2(!mgr.load(id).has_value(),
             "load after delete must return nullopt");
    QCOMPARE(mgr.loadAll().size(), 0);
}

void InstanceConfigManagerTest::testCorruptRowSkipped()
{
    QTemporaryDir base;
    QVERIFY2(base.isValid(), "temporary directory creation failed");
    InstanceConfigManager mgr(base.path());
    QVERIFY2(mgr.save(makeInstance(QStringLiteral("aaa"))), "save aaa failed");
    QVERIFY2(mgr.save(makeInstance(QStringLiteral("bbb"))), "save bbb failed");

    // Plant a corrupt json row directly (simulates external db corruption).
    DatabaseManager db;
    QVERIFY(db.open(base.path() + QStringLiteral("/app.db")));
    QVERIFY(db.setValue(
        QStringLiteral("corrupt-marker"), QStringLiteral("x")));
    QVERIFY(db.execRaw(
        QStringLiteral(
            "INSERT OR REPLACE INTO instance_configs (uuid, json) "
            "VALUES ('garbage', '{ this is not :: valid JSON ^^^')")));

    const QList<InstanceConfig> all = mgr.loadAll();

    QCOMPARE(all.size(), 2);
    QCOMPARE(all[0].id, QStringLiteral("aaa"));
    QCOMPARE(all[1].id, QStringLiteral("bbb"));
    QVERIFY(!mgr.load(QStringLiteral("garbage")).has_value());
}

QTEST_MAIN(InstanceConfigManagerTest)
#include "InstanceConfigManagerTest.moc"
