// DatabaseManagerTest 鈥?SQLite config backend: schema creation, kv
// round-trip, instance config round-trip, asset insert + duplicate-sha
// reject, ref attach/detach, inUse.
//
// QTEST_MAIN: QSqlDatabase + QFile are synchronous, no event loop.

#include "core/DatabaseManager.hpp"
#include "core/InstanceConfig.hpp"

#include <QFile>
#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

class DatabaseManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void testOpenCreatesSchema();
    void testKvRoundTrip();
    void testInstanceConfigRoundTrip();
    void testAssetInsertAndDuplicateReject();
    void testRefsAttachDetachInUse();
    void testClosedDbIsSafeNoOp();

private:
    QString dbPath(const QTemporaryDir& base) const
    {
        return base.path() + QStringLiteral("/app.db");
    }
};

void DatabaseManagerTest::testOpenCreatesSchema()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    DatabaseManager db;
    QVERIFY2(db.open(dbPath(base)), "open() must succeed on a fresh temp dir");
    QVERIFY(db.isOpen());
    QVERIFY2(QFile::exists(dbPath(base)), "app.db file must exist after open");
    // Re-open on an already-open db is rejected.
    QVERIFY(!db.open(dbPath(base)));
}

void DatabaseManagerTest::testKvRoundTrip()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    DatabaseManager db;
    QVERIFY(db.open(dbPath(base)));

    // Absent key 鈫?fallback.
    QCOMPARE(db.getValue(QStringLiteral("missing"), QStringLiteral("fb")),
             QStringLiteral("fb"));
    // Set 鈫?get round-trip; overwrite wins.
    QVERIFY(db.setValue(QStringLiteral("k"), QStringLiteral("v1")));
    QCOMPARE(db.getValue(QStringLiteral("k")), QStringLiteral("v1"));
    QVERIFY(db.setValue(QStringLiteral("k"), QStringLiteral("v2")));
    QCOMPARE(db.getValue(QStringLiteral("k")), QStringLiteral("v2"));
}

void DatabaseManagerTest::testInstanceConfigRoundTrip()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    DatabaseManager db;
    QVERIFY(db.open(dbPath(base)));

    InstanceConfig cfg = defaultInstanceConfig();
    cfg.id = QStringLiteral("11111111-2222-3333-4444-555555555555");
    cfg.label = QStringLiteral("閿︾憻");
    cfg.modelName = QStringLiteral("Hiyori");
    cfg.autoStart = true;
    cfg.opacity = 0.75;

    QVERIFY(db.saveInstance(cfg));
    const auto loaded = db.loadInstance(cfg.id);
    QVERIFY2(loaded.has_value(), "loadInstance must return the saved row");
    QCOMPARE(*loaded, cfg);

    // Absent uuid 鈫?nullopt.
    QVERIFY(!db.loadInstance(QStringLiteral("nope")).has_value());

    // loadAll sorted by uuid.
    InstanceConfig other = defaultInstanceConfig();
    other.id = QStringLiteral("00000000-0000-0000-0000-000000000000");
    other.label = QStringLiteral("aaa");
    QVERIFY(db.saveInstance(other));
    const QList<InstanceConfig> all = db.loadAllInstances();
    QCOMPARE(all.size(), 2);
    QCOMPARE(all[0].id, other.id); // "0000..." sorts before "1111..."

    // deleteInstance idempotent.
    QVERIFY(db.deleteInstance(cfg.id));
    QVERIFY(db.deleteInstance(cfg.id));
    QVERIFY(!db.loadInstance(cfg.id).has_value());
    QCOMPARE(db.loadAllInstances().size(), 1);
}

void DatabaseManagerTest::testAssetInsertAndDuplicateReject()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    DatabaseManager db;
    QVERIFY(db.open(dbPath(base)));

    const qint64 id = db.insertAsset(
        QStringLiteral("aa"), QStringLiteral("cat.png"), 256, 256, 1234,
        QStringLiteral("2026-08-20T10:00:00"));
    QVERIFY2(id > 0, "first insert must return a positive id");

    // Duplicate sha256 鈫?-1 (reject).
    QVERIFY2(db.insertAsset(QStringLiteral("aa"), QStringLiteral("dup.png"),
                            10, 10, 5, QStringLiteral("2026-08-21")) < 0,
             "duplicate sha256 must be rejected");

    // Row content round-trips.
    const QVariantMap row = db.assetById(id);
    QVERIFY(!row.isEmpty());
    QCOMPARE(row.value(QStringLiteral("sha256")).toString(),
             QStringLiteral("aa"));
    QCOMPARE(row.value(QStringLiteral("originalName")).toString(),
             QStringLiteral("cat.png"));
    QCOMPARE(row.value(QStringLiteral("width")).toInt(), 256);
    QCOMPARE(row.value(QStringLiteral("sizeBytes")).toLongLong(),
             qint64(1234));

    // By-sha lookup + absent lookups.
    QVERIFY(!db.assetBySha256(QStringLiteral("aa")).isEmpty());
    QVERIFY(db.assetBySha256(QStringLiteral("zz")).isEmpty());
    QVERIFY(db.assetById(9999).isEmpty());

    // allAssets returns the one row; deleteAssetRow removes it.
    QCOMPARE(db.allAssets().size(), 1);
    QVERIFY(db.deleteAssetRow(id));
    QVERIFY(db.assetById(id).isEmpty());
}

void DatabaseManagerTest::testRefsAttachDetachInUse()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    DatabaseManager db;
    QVERIFY(db.open(dbPath(base)));

    const qint64 a1 = db.insertAsset(
        QStringLiteral("h1"), QStringLiteral("a.png"), 16, 16, 10,
        QStringLiteral("t1"));
    const qint64 a2 = db.insertAsset(
        QStringLiteral("h2"), QStringLiteral("b.png"), 16, 16, 10,
        QStringLiteral("t2"));
    QVERIFY(a1 > 0 && a2 > 0);

    QVERIFY(!db.inUse(a1));
    QVERIFY(db.attachRef(a1, QStringLiteral("logo"), QString()));
    QVERIFY(db.inUse(a1));
    QVERIFY(db.attachRef(a2, QStringLiteral("instance_icon"),
                         QStringLiteral("uuid-1")));
    QVERIFY(db.inUse(a2));

    // refsOf: logo maps to "logo", icon maps to the uuid.
    QCOMPARE(db.refsOf(a1), QStringList{QStringLiteral("logo")});
    QCOMPARE(db.refsOf(a2),
             QStringList{QStringLiteral("uuid-1")});

    // detachRefsOf clears every ref with that (type, key).
    QVERIFY(db.attachRef(a2, QStringLiteral("instance_icon"),
                         QStringLiteral("uuid-2")));
    QVERIFY(db.detachRefsOf(QStringLiteral("instance_icon"),
                            QStringLiteral("uuid-1")));
    QVERIFY(db.inUse(a2)); // uuid-2 ref still holds it
    QVERIFY(db.detachRef(a2, QStringLiteral("instance_icon"),
                         QStringLiteral("uuid-2")));
    QVERIFY(!db.inUse(a2));

    // detachRef on a non-existent ref is idempotent success.
    QVERIFY(db.detachRef(a1, QStringLiteral("logo"), QString()));
    QVERIFY(!db.inUse(a1));
}

void DatabaseManagerTest::testClosedDbIsSafeNoOp()
{
    DatabaseManager db; // never opened
    QVERIFY(!db.isOpen());
    QCOMPARE(db.getValue(QStringLiteral("k"), QStringLiteral("fb")),
             QStringLiteral("fb"));
    QVERIFY(!db.setValue(QStringLiteral("k"), QStringLiteral("v")));
    QVERIFY(!db.loadInstance(QStringLiteral("x")).has_value());
    QVERIFY(db.loadAllInstances().isEmpty());
    QVERIFY(db.insertAsset(QStringLiteral("h"), QStringLiteral("n"), 1, 1, 1,
                           QStringLiteral("t")) < 0);
    QVERIFY(db.assetById(1).isEmpty());
    QVERIFY(db.allAssets().isEmpty());
    QVERIFY(!db.inUse(1));
    QVERIFY(db.refsOf(1).isEmpty());
    QVERIFY(!db.deleteInstance(QStringLiteral("x")));
    QVERIFY(!db.deleteAssetRow(1));
    QVERIFY(!db.attachRef(1, QStringLiteral("logo"), QString()));
    QVERIFY(!db.detachRef(1, QStringLiteral("logo"), QString()));
    QVERIFY(!db.detachRefsOf(QStringLiteral("logo"), QString()));
}

QTEST_MAIN(DatabaseManagerTest)
#include "DatabaseManagerTest.moc"
