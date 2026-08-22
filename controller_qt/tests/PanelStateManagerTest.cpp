// PanelStateManagerTest 鈥?SQLite-backed panel config load/save.
//
// Behaviors locked (SQLite semantics 鈥?same API, no JSON files):
//   1. Fresh install (empty db) 鈫?load() returns defaults.
//   2. save() 鈫?load() round-trip: every PanelConfig field survives.
//   3. A second manager instance over the same db sees the saved state
//      (persistence is real, not in-memory).
//   4. Corrupt panel row 鈫?defaults, no throw.
//
// QTEST_MAIN: QSqlDatabase + QFile are synchronous.

#include "core/PanelStateManager.hpp"
#include "core/DatabaseManager.hpp"

#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

class PanelStateManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void testFreshDbReturnsDefaults();
    void testSaveLoadRoundTrip();
    void testPersistedAcrossManagers();
    void testCorruptRowReturnsDefaults();
};

void PanelStateManagerTest::testFreshDbReturnsDefaults()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    PanelStateManager mgr(base.path());
    QCOMPARE(mgr.load(), defaultPanelConfig());
}

void PanelStateManagerTest::testSaveLoadRoundTrip()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());

    PanelConfig original;
    original.panelX = 42;
    original.panelY = 99;
    original.panelWidth = 800;
    original.panelHeight = 600;
    original.theme = QStringLiteral("缁堢榛戝");
    original.fontSize = 18;
    original.panelOpacity = 0.5;
    original.instanceIds = QStringList{
        QStringLiteral("11111111-1111-1111-1111-111111111111"),
        QStringLiteral("22222222-2222-2222-2222-222222222222"),
    };
    original.autoLaunchSystem = true;
    original.startMinimized = true;
    original.closeAction = QStringLiteral("minimize");
    original.confirmOnExit = true;

    PanelStateManager mgr(base.path());
    QVERIFY2(mgr.save(original), "save() returned false");
    QCOMPARE(mgr.load(), original);
}

void PanelStateManagerTest::testPersistedAcrossManagers()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());

    PanelConfig cfg = defaultPanelConfig();
    cfg.theme = QStringLiteral("星空蓝");
    cfg.panelX = 250;

    PanelStateManager writer(base.path());
    QVERIFY(writer.save(cfg));

    PanelStateManager reader(base.path());
    QCOMPARE(reader.load(), cfg);
}

void PanelStateManagerTest::testCorruptRowReturnsDefaults()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());

    PanelConfig cfg = defaultPanelConfig();
    cfg.theme = QStringLiteral("will-be-corrupted");
    PanelStateManager mgr(base.path());
    QVERIFY(mgr.save(cfg));

    // Corrupt the panel kv row directly through a raw DatabaseManager 鈥?    // plant non-JSON garbage where the blob lives.
    DatabaseManager db;
    QVERIFY(db.open(base.path() + QStringLiteral("/app.db")));
    QVERIFY(db.setValue(QStringLiteral("panel"),
                        QStringLiteral("{ this is not : json !!! ")));

    PanelConfig loaded;
    try {
        loaded = mgr.load();
    } catch (...) {
        QFAIL("PanelStateManager::load() threw on corrupt row");
        return;
    }
    QCOMPARE(loaded, defaultPanelConfig());
}

QTEST_MAIN(PanelStateManagerTest)
#include "PanelStateManagerTest.moc"
