// PanelStateManagerTest — TDD for panel.json load/save + legacy migration (T21).
//
// Five behaviors locked (matches the task spec MUST DO list):
//   1. Fresh install (empty config dir) → load() returns defaults AND writes
//      panel.json so the next load() hits the parse branch.
//   2. Existing panel.json with non-default values → load() parses and returns
//      them (T19 serde round-trip through the manager).
//   3. Legacy panel-state.json (Java nested format) → migrated to panel.json +
//      instances/<uuid>.json, old file renamed to .bak, instanceIds populated.
//   4. Unparseable legacy panel-state.json → defaults returned, old file
//      PRESERVED (not renamed) so the user can recover it manually.
//   5. save() → load() round-trip: every PanelConfig field survives.
//
// QTEST_APPLESS_MAIN: no event loop needed — all operations (QSaveFile, QFile,
// QDir) are synchronous. The manager never touches the network or Qt
// event-driven I/O.

#include "core/PanelStateManager.hpp"
#include "core/InstanceConfig.hpp"
#include "core/InstanceConfigManager.hpp"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

namespace {

// Write `data` to `<base>/<name>`, creating parent dirs as needed. Used to
// plant fixtures (panel.json, panel-state.json) directly into the temp dir.
void writeFile(const QString& base, const QString& name, const QByteArray& data)
{
    QDir().mkpath(base);
    QFile f(base + QLatin1Char('/') + name);
    QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Truncate),
             qPrintable(QStringLiteral("Failed to write %1: %2")
                            .arg(f.fileName(), f.errorString())));
    f.write(data);
    f.close();
}

} // namespace

class PanelStateManagerTest : public QObject
{
    Q_OBJECT

private slots:

    // 1. Fresh install → defaults written + returned.
    void testFreshWritesDefaults()
    {
        QTemporaryDir base;
        QVERIFY(base.isValid());
        PanelStateManager mgr(base.path());

        const PanelConfig cfg = mgr.load();

        const PanelConfig defaults = defaultPanelConfig();
        QCOMPARE(cfg, defaults);
        // panel.json MUST have been written so the next load() parses it.
        QVERIFY2(QFile::exists(base.path() + QStringLiteral("/panel.json")),
                 "panel.json not created on fresh install");
    }

    // 2. Existing panel.json with non-default values → parsed & returned.
    void testLoadsExistingPanelJson()
    {
        QTemporaryDir base;
        QVERIFY(base.isValid());

        // Plant a panel.json with a distinctive theme + geometry that differs
        // from the defaults (theme default = "深紫梦幻", panelX/Y default = -1).
        QJsonObject obj;
        obj.insert(QStringLiteral("panel_x"), 250);
        obj.insert(QStringLiteral("panel_y"), 350);
        obj.insert(QStringLiteral("panel_width"), 1100);
        obj.insert(QStringLiteral("panel_height"), 700);
        obj.insert(QStringLiteral("theme"), QStringLiteral("星空蓝"));
        obj.insert(QStringLiteral("font_size"), 16);
        obj.insert(QStringLiteral("panel_opacity"), 0.9);
        obj.insert(QStringLiteral("instance_ids"), QJsonArray{});
        obj.insert(QStringLiteral("auto_launch_system"), true);
        obj.insert(QStringLiteral("start_minimized"), true);
        obj.insert(QStringLiteral("close_action"), QStringLiteral("minimize"));
        obj.insert(QStringLiteral("confirm_on_exit"), true);
        const QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
        writeFile(base.path(), QStringLiteral("panel.json"), data);

        PanelStateManager mgr(base.path());
        const PanelConfig cfg = mgr.load();

        QCOMPARE(cfg.panelX, 250);
        QCOMPARE(cfg.panelY, 350);
        QCOMPARE(cfg.panelWidth, 1100);
        QCOMPARE(cfg.panelHeight, 700);
        QCOMPARE(cfg.theme, QStringLiteral("星空蓝"));
        QCOMPARE(cfg.fontSize, 16);
        QCOMPARE(cfg.panelOpacity, 0.9);
        QCOMPARE(cfg.autoLaunchSystem, true);
        QCOMPARE(cfg.startMinimized, true);
        QCOMPARE(cfg.closeAction, QStringLiteral("minimize"));
        QCOMPARE(cfg.confirmOnExit, true);
        QVERIFY(cfg.instanceIds.isEmpty());
    }

    // 3. Legacy panel-state.json → migrated to panel.json + instances/*.json,
    //    old file renamed to .bak.
    void testLegacyMigration()
    {
        QTemporaryDir base;
        QVERIFY(base.isValid());

        // Plant the LEGACY format (Java nested layout):
        //   {"panel": {"x":..., "theme":...}, "instances": [{...}]}
        QJsonObject panel;
        panel.insert(QStringLiteral("x"), 100);
        panel.insert(QStringLiteral("y"), 200);
        panel.insert(QStringLiteral("width"), 1024);
        panel.insert(QStringLiteral("height"), 640);
        panel.insert(QStringLiteral("theme"), QStringLiteral("极光绿"));

        QJsonObject inst;
        inst.insert(QStringLiteral("label"), QStringLiteral("锦瑟"));
        inst.insert(QStringLiteral("model"), QStringLiteral("Hiyori"));
        inst.insert(QStringLiteral("renderer_path"), QStringLiteral("/opt/renderer"));
        inst.insert(QStringLiteral("pos_x"), 100);
        inst.insert(QStringLiteral("pos_y"), 200);
        inst.insert(QStringLiteral("window_width"), 300);
        inst.insert(QStringLiteral("window_height"), 400);
        inst.insert(QStringLiteral("opacity"), 0.8);
        inst.insert(QStringLiteral("drag_mode"), QStringLiteral("physics"));
        inst.insert(QStringLiteral("idle_interval"), 20);
        inst.insert(QStringLiteral("target_fps"), 60);
        inst.insert(QStringLiteral("auto_start"), true);
        inst.insert(QStringLiteral("current_expression"), QStringLiteral("F05"));

        QJsonObject root;
        root.insert(QStringLiteral("panel"), panel);
        root.insert(QStringLiteral("instances"), QJsonArray{inst});
        writeFile(base.path(), QStringLiteral("panel-state.json"),
                  QJsonDocument(root).toJson(QJsonDocument::Compact));

        // Precondition: panel.json must NOT exist (else load() takes the
        // parse-existing branch instead of migrating).
        QVERIFY(!QFile::exists(base.path() + QStringLiteral("/panel.json")));

        PanelStateManager mgr(base.path());
        const PanelConfig cfg = mgr.load();

        // Panel-level fields migrated.
        QCOMPARE(cfg.panelX, 100);
        QCOMPARE(cfg.panelY, 200);
        QCOMPARE(cfg.panelWidth, 1024);
        QCOMPARE(cfg.panelHeight, 640);
        QCOMPARE(cfg.theme, QStringLiteral("极光绿"));

        // Exactly one instance migrated, with a fresh UUID.
        QCOMPARE(cfg.instanceIds.size(), 1);
        const QString uuid = cfg.instanceIds.first();
        QVERIFY2(!uuid.isEmpty(), "migrated instance id is empty");
        // UUIDs should look like xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx (no braces).
        QVERIFY2(!uuid.contains(QLatin1Char('{')),
                 "UUID should not contain braces");

        // panel.json created with the migrated config.
        QVERIFY2(QFile::exists(base.path() + QStringLiteral("/panel.json")),
                 "panel.json not created after migration");

        // Legacy file renamed to .bak (NOT deleted).
        QVERIFY2(!QFile::exists(base.path()
                                + QStringLiteral("/panel-state.json")),
                 "legacy panel-state.json still present (should be renamed)");
        QVERIFY2(QFile::exists(base.path()
                               + QStringLiteral("/panel-state.json.bak")),
                 "panel-state.json.bak backup not created");

        // The migrated instance file exists and round-trips via
        // InstanceConfigManager (T20), proving the split is real.
        InstanceConfigManager imgr(base.path());
        const auto opt = imgr.load(uuid);
        QVERIFY2(opt.has_value(), "migrated instance file not loadable");
        QCOMPARE(opt->label, QStringLiteral("锦瑟"));
        QCOMPARE(opt->modelName, QStringLiteral("Hiyori"));
        QCOMPARE(opt->windowX, 100);
        QCOMPARE(opt->windowY, 200);
        QCOMPARE(opt->dragMode, QStringLiteral("physics"));
        QCOMPARE(opt->idleInterval, 20);
        QCOMPARE(opt->targetFps, 60);
        QCOMPARE(opt->autoStart, true);
        QCOMPARE(opt->currentExpression, QStringLiteral("F05"));
    }

    // 4. Unparseable legacy panel-state.json → defaults + old file PRESERVED.
    void testUnparseableLegacyReturnsDefaultsAndPreserves()
    {
        QTemporaryDir base;
        QVERIFY(base.isValid());

        // Garbage that is not valid JSON.
        writeFile(base.path(), QStringLiteral("panel-state.json"),
                  QByteArrayLiteral("{ this is not : json !!! "));
        QVERIFY(!QFile::exists(base.path() + QStringLiteral("/panel.json")));

        PanelStateManager mgr(base.path());
        const PanelConfig cfg = mgr.load();

        // Defaults returned.
        QCOMPARE(cfg, defaultPanelConfig());

        // Old file PRESERVED — not renamed to .bak.
        QVERIFY2(QFile::exists(base.path()
                               + QStringLiteral("/panel-state.json")),
                 "unparseable legacy file was renamed (should be preserved)");
        QVERIFY2(!QFile::exists(base.path()
                                + QStringLiteral("/panel-state.json.bak")),
                 ".bak created despite unparseable legacy (should not exist)");
    }

    // 5. save() → load() round-trip.
    void testSaveLoadRoundTrip()
    {
        QTemporaryDir base;
        QVERIFY(base.isValid());

        // Use distinctive non-default values in every field.
        PanelConfig original;
        original.panelX = 42;
        original.panelY = 99;
        original.panelWidth = 800;
        original.panelHeight = 600;
        original.theme = QStringLiteral("终端黑客");
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

        // Re-load via a fresh manager to ensure no in-memory state leaks.
        PanelStateManager mgr2(base.path());
        const PanelConfig loaded = mgr2.load();

        QCOMPARE(loaded, original);
    }
};

QTEST_APPLESS_MAIN(PanelStateManagerTest)
#include "PanelStateManagerTest.moc"
