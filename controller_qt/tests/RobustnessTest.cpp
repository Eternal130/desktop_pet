// RobustnessTest 鈥?SQLite-backend robustness + serde fuzz.
//
// Test groups locking the "degrade gracefully" contract:
//   1. Corrupt panel kv row 鈫?PanelStateManager::load() returns defaults,
//      no throw.
//   2. Corrupt instance rows 鈫?loadAll skips them, load returns nullopt,
//      no throw.
//   3. Fuzz (1000 iterations): random QJsonObjects fed to
//      instanceConfigFromJson / panelConfigFromJson 鈥?none may throw.
//
// QTEST_MAIN: QSqlDatabase + QJsonObject are synchronous.

#include "core/InstanceConfigManager.hpp"
#include "core/PanelStateManager.hpp"
#include "core/DatabaseManager.hpp"
#include "core/InstanceConfig.hpp"
#include "core/PanelConfig.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QRandomGenerator>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

namespace {

constexpr int kFuzzIterations = 1000;

QJsonValue makeRandomJsonValue(QRandomGenerator* g, int depth)
{
    const int branch = g->bounded(depth > 0 ? 6 : 4);
    switch (branch) {
    case 0:
        return QJsonValue(g->bounded(1000000) - 500000);
    case 1:
        return QJsonValue(QString::number(g->generate(), 16));
    case 2:
        return QJsonValue((g->generate() & 1) != 0);
    case 3:
        return QJsonValue::Null;
    case 4: {
        QJsonObject obj;
        const int n = g->bounded(3) + 1;
        for (int i = 0; i < n; ++i)
            obj.insert(QString::number(g->bounded(100)),
                       makeRandomJsonValue(g, depth - 1));
        return obj;
    }
    default: {
        QJsonArray arr;
        const int n = g->bounded(3) + 1;
        for (int i = 0; i < n; ++i)
            arr.append(makeRandomJsonValue(g, depth - 1));
        return arr;
    }
    }
}

QJsonObject makeRandomJsonObject(QRandomGenerator* g)
{
    QJsonObject obj;
    const int noise = g->bounded(8) + 1;
    for (int i = 0; i < noise; ++i)
        obj.insert(QStringLiteral("noise_") + QString::number(g->bounded(1000)),
                   makeRandomJsonValue(g, 2));
    static const char* kInstanceKeys[] = {
        "id", "label", "renderer_path", "graphics_backend", "model_name",
        "model_scale", "window_x", "window_y", "window_width", "window_height",
        "opacity", "drag_mode", "idle_interval", "target_fps", "auto_start",
        "current_expression", "voice_pack", "volume", "muted", "drag_mode",
        "layout_offset_x", "layout_offset_y", "layout_scale",
        "subtitle_offset_x", "subtitle_offset_y", "subtitle_area_width",
        "subtitle_area_height", "subtitle_font_size", "subtitle_style_preset"
    };
    static const char* kPanelKeys[] = {
        "panel_x", "panel_y", "panel_width", "panel_height", "theme",
        "font_size", "panel_opacity", "instance_ids", "auto_launch_system",
        "start_minimized", "close_action", "confirm_on_exit"
    };
    for (const char* k : kInstanceKeys)
        if (g->bounded(2) == 0)
            obj.insert(QString::fromLatin1(k), makeRandomJsonValue(g, 1));
    for (const char* k : kPanelKeys)
        if (g->bounded(2) == 0)
            obj.insert(QString::fromLatin1(k), makeRandomJsonValue(g, 1));
    return obj;
}

} // namespace

class RobustnessTest : public QObject
{
    Q_OBJECT

private slots:
    void testCorruptPanelRowReturnsDefaults();
    void testCorruptInstanceRowsSkipped();
    void testFuzzRandomJsonNoThrow();
};

void RobustnessTest::testCorruptPanelRowReturnsDefaults()
{
    QTemporaryDir base;
    QVERIFY2(base.isValid(), "temporary directory creation failed");
    PanelStateManager psm(base.path());

    PanelConfig saved = defaultPanelConfig();
    saved.theme = QStringLiteral("robustness-test-theme");
    QVERIFY2(psm.save(saved), "precondition: save must succeed");

    // Corrupt the panel kv row with non-JSON garbage.
    DatabaseManager db;
    QVERIFY(db.open(base.path() + QStringLiteral("/app.db")));
    QVERIFY(db.setValue(QStringLiteral("panel"),
                        QStringLiteral("{ truncated garbage !!! ")));

    PanelConfig loaded;
    try {
        loaded = psm.load();
    } catch (...) {
        QFAIL("PanelStateManager::load() threw on corrupt row");
        return;
    }
    QCOMPARE(loaded, defaultPanelConfig());
    QVERIFY2(loaded.theme != saved.theme,
             "load must not return the corrupted theme 鈥?must be defaults");
}

void RobustnessTest::testCorruptInstanceRowsSkipped()
{
    QTemporaryDir base;
    QVERIFY2(base.isValid(), "temporary directory creation failed");
    InstanceConfigManager mgr(base.path());

    InstanceConfig good = defaultInstanceConfig();
    good.id = QStringLiteral("good");
    good.label = QStringLiteral("好实例");
    QVERIFY2(mgr.save(good), "precondition: save must succeed");

    DatabaseManager db;
    QVERIFY(db.open(base.path() + QStringLiteral("/app.db")));
    QVERIFY(db.execRaw(
        QStringLiteral(
            "INSERT OR REPLACE INTO instance_configs (uuid, json) "
            "VALUES ('bad-1', 'not json at all')")));
    QVERIFY(db.execRaw(
        QStringLiteral(
            "INSERT OR REPLACE INTO instance_configs (uuid, json) "
            "VALUES ('bad-2', '{\"truncated\": ')")));

    QList<InstanceConfig> all;
    try {
        all = mgr.loadAll();
    } catch (...) {
        QFAIL("InstanceConfigManager::loadAll() threw on corrupt rows");
        return;
    }
    QCOMPARE(all.size(), 1);
    QCOMPARE(all[0].id, QStringLiteral("good"));
    QVERIFY(!mgr.load(QStringLiteral("bad-1")).has_value());
    QVERIFY(!mgr.load(QStringLiteral("bad-2")).has_value());
}

void RobustnessTest::testFuzzRandomJsonNoThrow()
{
    QRandomGenerator rng(0xC0FFEEu);
    int exceptions = 0;

    for (int i = 0; i < kFuzzIterations; ++i) {
        const QJsonObject randomObj = makeRandomJsonObject(&rng);
        try {
            instanceConfigFromJson(randomObj);
        } catch (...) {
            ++exceptions;
            QFAIL(qPrintable(QStringLiteral(
                "instanceConfigFromJson threw on iteration %1").arg(i)));
        }
    }
    for (int i = 0; i < kFuzzIterations; ++i) {
        const QJsonObject randomObj = makeRandomJsonObject(&rng);
        try {
            panelConfigFromJson(randomObj);
        } catch (...) {
            ++exceptions;
            QFAIL(qPrintable(QStringLiteral(
                "panelConfigFromJson threw on iteration %1").arg(i)));
        }
    }
    QCOMPARE(exceptions, 0);
}

QTEST_MAIN(RobustnessTest)
#include "RobustnessTest.moc"
