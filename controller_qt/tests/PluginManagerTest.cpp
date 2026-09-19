// PluginManagerTest (P4, §B.6): the QML bridge model + kv-persisted
// enable bits. Disable = config bit + restart (§A.4); persistence goes
// through DatabaseManager's panel_config kv (never-throws; a failed write
// degrades to session-only). Also covers the boot-integration contract:
// a disabled plugin is skipped by PluginHost (accepted at the manager
// level).
#include <QTemporaryDir>
#include <QTest>

#include "core/DatabaseManager.hpp"
#include "core/PluginHost.hpp"
#include "core/PluginManager.hpp"

using core::PluginHost;
using core::PluginManager;
using core::PluginRegistry;
using core::PluginStatus;

namespace {

QString manifestFor(const char* id)
{
    return QStringLiteral(
        R"json({"id":"%1","version":"0.1.0","api_version":"1.0","order":100})json")
        .arg(QLatin1String(id));
}

pet::IPanelPlugin* createNoop()
{
    struct Noop final : pet::IPanelPlugin {
        pet::PluginError initialize(pet::IPluginContext&) override
        { return pet::PluginError::Ok; }
        void shutdown() override {}
    };
    return new Noop;
}

} // namespace

class PluginManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void testDefaultsAndRoles();
    void testEnabledPersistsThroughDatabase();
    void testNewManagerInstanceReadsPersistedBit();
    void testDisabledPluginSkippedAtBoot();
    void testRestartPendingSemantics();
    void testNullDatabaseDegradesToEnabled();
};

void PluginManagerTest::testDefaultsAndRoles()
{
    PluginRegistry reg;
    reg.addStaticPlugin(manifestFor("org.test.a"), &createNoop);
    PluginManager manager(reg, nullptr);

    QCOMPARE(manager.rowCount(), 1);
    QCOMPARE(manager.data(manager.index(0, 0), PluginManager::IdRole).toString(),
             QStringLiteral("org.test.a"));
    QCOMPARE(manager.data(manager.index(0, 0), PluginManager::StatusRole).toString(),
             QStringLiteral("registered"));
    QCOMPARE(manager.data(manager.index(0, 0), PluginManager::EnabledRole).toBool(),
             true);
    QVERIFY(manager.enabledAtStart(QStringLiteral("org.test.a")));
}

void PluginManagerTest::testEnabledPersistsThroughDatabase()
{
    QTemporaryDir tmp;
    DatabaseManager db;
    QVERIFY(db.open(tmp.filePath(QStringLiteral("test.db"))));

    PluginRegistry reg;
    reg.addStaticPlugin(manifestFor("org.test.a"), &createNoop);
    PluginManager manager(reg, &db);

    manager.setEnabled(QStringLiteral("org.test.a"), false);
    QCOMPARE(db.getValue(QStringLiteral("plugin_enabled/org.test.a")),
             QStringLiteral("0"));
    QVERIFY(!manager.enabledAtStart(QStringLiteral("org.test.a")));

    manager.setEnabled(QStringLiteral("org.test.a"), true);
    QCOMPARE(db.getValue(QStringLiteral("plugin_enabled/org.test.a")),
             QStringLiteral("1"));
    QVERIFY(manager.enabledAtStart(QStringLiteral("org.test.a")));
}

void PluginManagerTest::testNewManagerInstanceReadsPersistedBit()
{
    QTemporaryDir tmp;
    DatabaseManager db;
    QVERIFY(db.open(tmp.filePath(QStringLiteral("test.db"))));
    db.setValue(QStringLiteral("plugin_enabled/org.test.a"), QStringLiteral("0"));

    PluginRegistry reg;
    reg.addStaticPlugin(manifestFor("org.test.a"), &createNoop);
    PluginManager manager(reg, &db); // fresh model, same db
    QVERIFY(!manager.enabledAtStart(QStringLiteral("org.test.a")));
}

void PluginManagerTest::testDisabledPluginSkippedAtBoot()
{
    QTemporaryDir tmp;
    DatabaseManager db;
    QVERIFY(db.open(tmp.filePath(QStringLiteral("test.db"))));

    PluginRegistry reg;
    reg.addStaticPlugin(manifestFor("org.test.a"), &createNoop);
    PluginManager manager(reg, &db);
    manager.setEnabled(QStringLiteral("org.test.a"), false);

    PluginHost host(reg);
    host.setEnabledProvider([&manager](const QString& id) {
        return manager.enabledAtStart(id);
    });
    host.initializeAll();
    QCOMPARE(reg.entry(QStringLiteral("org.test.a"))->status,
             PluginStatus::Registered); // never created/initialized
    host.shutdownAll();
}

void PluginManagerTest::testRestartPendingSemantics()
{
    QTemporaryDir tmp;
    DatabaseManager db;
    QVERIFY(db.open(tmp.filePath(QStringLiteral("test.db"))));

    PluginRegistry reg;
    reg.addStaticPlugin(manifestFor("org.test.a"), &createNoop);
    PluginManager manager(reg, &db);
    PluginHost host(reg);
    host.setEnabledProvider([&manager](const QString& id) {
        return manager.enabledAtStart(id);
    });
    host.initializeAll();

    // running + enabled → nothing pending
    QVERIFY(!manager.restartPending(QStringLiteral("org.test.a")));
    // running + now disabled → restart would stop loading it
    manager.setEnabled(QStringLiteral("org.test.a"), false);
    QVERIFY(manager.restartPending(QStringLiteral("org.test.a")));
    host.shutdownAll();
}

void PluginManagerTest::testNullDatabaseDegradesToEnabled()
{
    PluginRegistry reg;
    reg.addStaticPlugin(manifestFor("org.test.a"), &createNoop);
    PluginManager manager(reg, nullptr); // degraded mode
    manager.setEnabled(QStringLiteral("org.test.a"), false); // no crash
    QVERIFY(manager.enabledAtStart(QStringLiteral("org.test.a"))); // stays default
}

QTEST_MAIN(PluginManagerTest)
#include "PluginManagerTest.moc"
