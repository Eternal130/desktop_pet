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
#include <QTemporaryFile>
#include <QTest>

class PanelStateManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void testFreshDbReturnsDefaults();
    void testSaveLoadRoundTrip();
    void testPersistedAcrossManagers();
    void testCorruptRowReturnsDefaults();
    // Fix 2 (lost-update hardening): a FAILED query / db open must never be
    // treated as "row absent" — otherwise the next fresh load-modify-save
    // writer persists a defaults-seeded blob over the real state.
    void testQueryFailureRefusesSave();
    void testOwnedOpenFailureRefusesSave();
    void testMissingRowDefaultsSaveAllowed();
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

// Fix 2: injected-but-never-opened DatabaseManager → every query fails
// (closed connection). load() still returns defaults and never throws,
// but save() must REFUSE so a defaults-seeded blob cannot be written over
// the unread on-disk state.
void PanelStateManagerTest::testQueryFailureRefusesSave()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());

    DatabaseManager closed; // never opened — isOpen() == false
    PanelStateManager mgr(base.path());
    mgr.setDatabase(&closed);

    QCOMPARE(mgr.load(), defaultPanelConfig());
    QVERIFY2(!mgr.save(defaultPanelConfig()),
             "save() must refuse after a failed load (query failure)");
}

// Fix 2, owned-db angle: the config root path is blocked by a regular
// FILE, so app.db cannot be created under it → db() open fails → same
// contract (defaults from load(), save() refused).
void PanelStateManagerTest::testOwnedOpenFailureRefusesSave()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());

    QTemporaryFile notADir(base.path() + QStringLiteral("/blockerXXXXXX"));
    QVERIFY2(notADir.open(), "fixture blocker file creation failed");

    PanelStateManager mgr(notADir.fileName());
    QCOMPARE(mgr.load(), defaultPanelConfig());
    QVERIFY2(!mgr.save(defaultPanelConfig()),
             "save() must refuse after a failed load (db open failure)");
}

// Fix 2 baseline: a genuinely MISSING row keeps first-run semantics —
// load() returns defaults AND save() stays allowed (the error-path
// refusal must not bleed into the fresh-install happy path).
void PanelStateManagerTest::testMissingRowDefaultsSaveAllowed()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());

    PanelStateManager mgr(base.path());
    QCOMPARE(mgr.load(), defaultPanelConfig());

    PanelConfig cfg = defaultPanelConfig();
    cfg.theme = QStringLiteral("first-run");
    QVERIFY2(mgr.save(cfg), "first-run save must be allowed");
    QCOMPARE(mgr.load(), cfg);
}

QTEST_MAIN(PanelStateManagerTest)
#include "PanelStateManagerTest.moc"
