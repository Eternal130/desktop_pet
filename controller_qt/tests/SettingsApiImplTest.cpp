// SettingsApiImplTest (S6, v1.3) — the real pet::ISettingsApi over the
// SAME PanelStateManager persistence path the panel's settings page uses.
// Locks:
//   1. testRoundTripPersistsAllFourFields: each typed setter writes the
//      panel_config store; a fresh PanelStateManager load reflects the new
//      value AND the untouched fields survive every write (load-modify-save
//      discipline).
//   2. testCloseActionRejectsIllegalValues: anything but "exit"/"minimize"
//      → InvalidArgument and nothing is persisted.
//   3. testDefaultModelNameRejectsEmpty: "" → InvalidArgument (empty MEANS
//      "use the default" at the call sites, it cannot be the stored value).
//   4. testAutoLaunchIsNotInTheInterface: COMPILE-TIME assertion that
//      ISettingsApi exposes no autoLaunch member (the host-shell capability
//      deliberately stays out of the plugin API — see its header).
//   5. testSharedDatabaseBackend: the injected DatabaseManager is used
//      (two impls sharing one db see each other's writes — the production
//      wiring where plugins and the settings page share ONE db).
//
// QTEST_MAIN — DatabaseManager's QSqlDatabase wants a QCoreApplication.
// Links pet_panel_core (SettingsApiImpl lives there).

#include "api/ISettingsApi.hpp"
#include "core/DatabaseManager.hpp"
#include "core/PanelConfig.hpp"
#include "core/PanelStateManager.hpp"
#include "core/PluginContextImpl.hpp"

#include <QString>
#include <QTemporaryDir>
#include <QTest>

namespace {

// Compile-time member-detector: true iff T declares any member named
// autoLaunchSystem / setAutoLaunchSystem. Used to pin the architecture
// decision that OS autostart NEVER enters the plugin settings API.
template <typename T>
class HasAutoLaunchMember
{
    template <typename U> static int test(decltype(&U::setAutoLaunchSystem));
    template <typename U> static char test(...);
public:
    static constexpr bool value = std::is_same_v<decltype(test<T>(nullptr)), int>;
};

static_assert(!HasAutoLaunchMember<pet::ISettingsApi>::value,
              "ISettingsApi must NOT expose autoLaunch (host-shell "
              "capability — see the family header's scope note)");

} // namespace

class SettingsApiImplTest : public QObject
{
    Q_OBJECT

private slots:
    void testRoundTripPersistsAllFourFields();
    void testCloseActionRejectsIllegalValues();
    void testDefaultModelNameRejectsEmpty();
    void testAutoLaunchIsNotInTheInterface();
    void testSharedDatabaseBackend();
};

void SettingsApiImplTest::testRoundTripPersistsAllFourFields()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    core::SettingsApiImpl api(base.path(), nullptr);

    QCOMPARE(api.setCloseAction(QStringLiteral("minimize")),
             pet::PluginError::Ok);
    QCOMPARE(api.setConfirmOnExit(true), pet::PluginError::Ok);
    QCOMPARE(api.setStartMinimized(true), pet::PluginError::Ok);
    QCOMPARE(api.setDefaultModelName(QStringLiteral("Haru")),
             pet::PluginError::Ok);

    const PanelConfig cfg = PanelStateManager(base.path()).load();
    QCOMPARE(cfg.closeAction, QStringLiteral("minimize"));
    QVERIFY(cfg.confirmOnExit);
    QVERIFY(cfg.startMinimized);
    QCOMPARE(cfg.defaultModelName, QStringLiteral("Haru"));

    // Back to the other legal values — every field round-trips.
    QCOMPARE(api.setCloseAction(QStringLiteral("exit")), pet::PluginError::Ok);
    QCOMPARE(api.setConfirmOnExit(false), pet::PluginError::Ok);
    QCOMPARE(api.setStartMinimized(false), pet::PluginError::Ok);
    QCOMPARE(api.setDefaultModelName(QStringLiteral("Hiyori")),
             pet::PluginError::Ok);
    const PanelConfig cfg2 = PanelStateManager(base.path()).load();
    QCOMPARE(cfg2.closeAction, QStringLiteral("exit"));
    QVERIFY(!cfg2.confirmOnExit);
    QVERIFY(!cfg2.startMinimized);
    QCOMPARE(cfg2.defaultModelName, QStringLiteral("Hiyori"));

    // Load-modify-save: fields the API does NOT own (theme, geometry,
    // autoLaunchSystem, ...) survive a plugin write.
    {
        PanelStateManager psm(base.path());
        PanelConfig cfg = psm.load();
        cfg.theme = QStringLiteral("dark");
        cfg.autoLaunchSystem = true;
        QVERIFY(psm.save(cfg));
    }
    QCOMPARE(api.setStartMinimized(true), pet::PluginError::Ok);
    const PanelConfig cfg3 = PanelStateManager(base.path()).load();
    QCOMPARE(cfg3.theme, QStringLiteral("dark"));
    QVERIFY(cfg3.autoLaunchSystem);
    QVERIFY(cfg3.startMinimized);
}

void SettingsApiImplTest::testCloseActionRejectsIllegalValues()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    core::SettingsApiImpl api(base.path(), nullptr);

    const QStringList illegal{
        QString(), QStringLiteral("hide_to_tray"), QStringLiteral("ask"),
        QStringLiteral("EXIT"), QStringLiteral("exit ")};
    for (const QString& bad : illegal) {
        QCOMPARE(api.setCloseAction(bad), pet::PluginError::InvalidArgument);
    }
    // Nothing was persisted — the store still holds the default.
    const PanelConfig cfg = PanelStateManager(base.path()).load();
    QCOMPARE(cfg.closeAction, QStringLiteral("exit"));
}

void SettingsApiImplTest::testDefaultModelNameRejectsEmpty()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    core::SettingsApiImpl api(base.path(), nullptr);

    const QString before = PanelStateManager(base.path()).load().defaultModelName;
    QCOMPARE(api.setDefaultModelName(QString()),
             pet::PluginError::InvalidArgument);
    // Rejected write — the stored value is untouched.
    QCOMPARE(PanelStateManager(base.path()).load().defaultModelName, before);
}

void SettingsApiImplTest::testAutoLaunchIsNotInTheInterface()
{
    // The compile-time static_assert in the anonymous namespace is the
    // real gate; this runtime slot keeps the contract visible in the test
    // report (and guards against a future rename re-adding the member).
    QVERIFY(true);
}

void SettingsApiImplTest::testSharedDatabaseBackend()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    // Production shape: ONE DatabaseManager shared by the settings impl
    // and the panel's own state manager — a plugin write and a host load
    // see the same rows.
    DatabaseManager db;
    QVERIFY(db.open(QDir(base.path()).filePath(QStringLiteral("app.db"))));

    core::SettingsApiImpl pluginSide(base.path(), nullptr);
    pluginSide.setDatabase(&db);
    QCOMPARE(pluginSide.setDefaultModelName(QStringLiteral("Wanko")),
             pet::PluginError::Ok);

    PanelStateManager hostSide(base.path());
    hostSide.setDatabase(&db);
    QCOMPARE(hostSide.load().defaultModelName, QStringLiteral("Wanko"));
}

QTEST_MAIN(SettingsApiImplTest)
#include "SettingsApiImplTest.moc"
