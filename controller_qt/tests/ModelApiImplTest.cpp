// ModelApiImplTest (S5, v1.3) — the real pet::IModelApi over the shared
// scan cache (ModelScanner + ModelInfoParser). Locks:
//   1. testScanPopulatesSummaries: a fixture tree with a fully populated
//      .model3.json → availableModels() carries the parsed
//      motionGroups/expressions/hitAreas; modelInfo() hit + miss paths.
//   2. testRefreshScanPicksUpNewModels: a model dropped on disk after the
//      initial scan appears after refreshScan().
//   3. testCorruptModel3JsonDegrades: garbage .model3.json → roster entry
//      survives with empty detail lists (never throws).
//   4. testEmptyRendererDir: no renderer dir → empty roster, modelInfo →
//      NotFound, accessors safe.
//
// Fixture strategy: QTemporaryDir with the exact shipped layout
// <tmp>/Resources/Models/<Name>/<Name>.model3.json (same approach as
// ModelScannerTest/ModelControllerTest).
//
// QTEST_APPLESS_MAIN — QDir/QFile are synchronous, no event loop needed.
// Links pet_panel_core (ModelApiImpl lives there).

#include "api/IModelApi.hpp"
#include "core/PluginContextImpl.hpp"

#include <QDir>
#include <QFile>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

namespace {

// A real-shaped .model3.json: Idle×idleCount + TapBody×tapCount motions,
// 2 expressions, 2 hit areas.
QByteArray model3Json(int idleCount, int tapCount)
{
    const QStringList idle = QStringList(idleCount, QStringLiteral("{\"File\":\"i.motion3.json\"}"));
    const QStringList tap = QStringList(tapCount, QStringLiteral("{\"File\":\"t.motion3.json\"}"));
    return QStringLiteral(
        "{\"FileReferences\":{\"Motions\":{\"Idle\":[%1],\"TapBody\":[%2]},"
        "\"Expressions\":[{\"Name\":\"f01\"},{\"Name\":\"f02\"}]},"
        "\"HitAreas\":[{\"Name\":\"Head\"},{\"Name\":\"Body\"}]}")
        .arg(idle.join(QLatin1Char(',')), tap.join(QLatin1Char(',')))
        .toUtf8();
}

bool writeModel(const QString& rendererRoot, const QString& name,
                const QByteArray& bytes)
{
    const QString dir = QDir(rendererRoot).absoluteFilePath(
        QStringLiteral("Resources/Models/") + name);
    if (!QDir().mkpath(dir))
        return false;
    QFile f(QDir(dir).absoluteFilePath(name + QStringLiteral(".model3.json")));
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(bytes);
    f.close();
    return true;
}

} // namespace

class ModelApiImplTest : public QObject
{
    Q_OBJECT

private slots:
    void testScanPopulatesSummaries();
    void testRefreshScanPicksUpNewModels();
    void testCorruptModel3JsonDegrades();
    void testEmptyRendererDir();
};

void ModelApiImplTest::testScanPopulatesSummaries()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    const QString rendererRoot = base.path();
    QVERIFY(writeModel(rendererRoot, QStringLiteral("Hiyori"),
                       model3Json(3, 1)));

    core::ModelApiImpl api;
    api.setRendererDir(rendererRoot);

    QCOMPARE(api.modelsDir(),
             QDir(rendererRoot).absoluteFilePath(QStringLiteral("Resources/Models")));

    const QVector<pet::ModelSummary> models = api.availableModels();
    QCOMPARE(models.size(), 1);
    QCOMPARE(models.at(0).name, QStringLiteral("Hiyori"));
    QCOMPARE(models.at(0).motionGroups,
             (QStringList{QStringLiteral("Idle"), QStringLiteral("TapBody")}));
    QCOMPARE(models.at(0).expressions,
             (QStringList{QStringLiteral("f01"), QStringLiteral("f02")}));
    QCOMPARE(models.at(0).hitAreas,
             (QStringList{QStringLiteral("Head"), QStringLiteral("Body")}));

    // modelInfo: hit (full copy) + miss (out cleared, NotFound).
    pet::ModelSummary out;
    QCOMPARE(api.modelInfo(QStringLiteral("Hiyori"), &out), pet::PluginError::Ok);
    QCOMPARE(out.name, QStringLiteral("Hiyori"));
    QCOMPARE(out.hitAreas.size(), 2);

    out.name = QStringLiteral("stale");
    QCOMPARE(api.modelInfo(QStringLiteral("Nobody"), &out),
             pet::PluginError::NotFound);
    QVERIFY(out.name.isEmpty()); // cleared on miss, never half-written

    // null out tolerated.
    QCOMPARE(api.modelInfo(QStringLiteral("Hiyori"), nullptr), pet::PluginError::Ok);
}

void ModelApiImplTest::testRefreshScanPicksUpNewModels()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    const QString rendererRoot = base.path();
    QVERIFY(writeModel(rendererRoot, QStringLiteral("Alpha"), model3Json(1, 1)));

    core::ModelApiImpl api;
    api.setRendererDir(rendererRoot);
    QCOMPARE(api.availableModels().size(), 1);

    // A second model lands on disk AFTER the initial scan.
    QVERIFY(writeModel(rendererRoot, QStringLiteral("Beta"), model3Json(2, 2)));
    QCOMPARE(api.availableModels().size(), 1); // cache, not a rescan
    api.refreshScan();
    const QVector<pet::ModelSummary> models = api.availableModels();
    QCOMPARE(models.size(), 2);
    QCOMPARE(models.at(0).name, QStringLiteral("Alpha"));
    QCOMPARE(models.at(1).name, QStringLiteral("Beta"));
}

void ModelApiImplTest::testCorruptModel3JsonDegrades()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    const QString rendererRoot = base.path();
    QVERIFY(writeModel(rendererRoot, QStringLiteral("Broken"),
                       QByteArray("{ not json")));

    core::ModelApiImpl api;
    api.setRendererDir(rendererRoot);

    // The scanner only checks file EXISTENCE — the roster entry survives
    // with empty details (the model still launches; the page shows zeros).
    const QVector<pet::ModelSummary> models = api.availableModels();
    QCOMPARE(models.size(), 1);
    QCOMPARE(models.at(0).name, QStringLiteral("Broken"));
    QVERIFY(models.at(0).motionGroups.isEmpty());
    QVERIFY(models.at(0).expressions.isEmpty());
    QVERIFY(models.at(0).hitAreas.isEmpty());

    pet::ModelSummary out;
    QCOMPARE(api.modelInfo(QStringLiteral("Broken"), &out), pet::PluginError::Ok);
    QVERIFY(out.hitAreas.isEmpty());
}

void ModelApiImplTest::testEmptyRendererDir()
{
    core::ModelApiImpl api; // no setRendererDir
    QVERIFY(api.availableModels().isEmpty());
    QVERIFY(api.modelsDir().isEmpty());
    pet::ModelSummary out;
    QCOMPARE(api.modelInfo(QStringLiteral("Hiyori"), &out),
             pet::PluginError::NotFound);
    api.refreshScan(); // safe no-op
    QVERIFY(api.availableModels().isEmpty());
}

QTEST_APPLESS_MAIN(ModelApiImplTest)
#include "ModelApiImplTest.moc"
