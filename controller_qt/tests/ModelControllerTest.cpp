// ModelControllerTest — 模型库 B 档 (model-library backend). Locks the QML
// bridge contract over the existing ModelScanner + ModelInfoParser utilities:
//   1. testScanPopulatesCountsAndDetails: a fixture tree with one fully-
//      populated model → modelCount / modelDirName / the three counts / the
//      three by-name detail lists all agree with the fixture's
//      .model3.json.
//   2. testRescanBumpsRevisionAndEmits: every rescan() bumps `revision` and
//      emits modelsChanged (the revision counter is what re-drives QML
//      bindings that call Q_INVOKABLE accessors).
//   3. testModelsDirPointsAtResourcesModels: modelsDir() is the conventional
//      <rendererDir>/Resources/Models path (header hint + empty-state guide).
//   4. testCorruptModel3JsonDegradesGracefully: a model dir whose
//      .model3.json is garbage still appears in the roster (the scanner only
//      checks existence) but reports 0/empty details — never throws.
//   5. testEmptyRendererDirAllAccessorsSafe: without setRendererDir the
//      roster is empty and EVERY accessor (counts, by-index, by-name,
//      modelsDir, openModelDir) is safe returning empty/0.
//   6. testOutOfRangeAndUnknownNameSafe: negative/oversized indexes and
//      unknown names → empty/0.
//
// Fixture strategy: QTemporaryDir with the exact shipped layout
// <tmp>/Resources/Models/<Name>/<Name>.model3.json (same approach as
// ModelScannerTest — the real Cubism Samples tree lacks the "Models"
// segment).
//
// QTEST_APPLESS_MAIN: QDir/QFile + synchronous QObject signals, no event
// loop needed.

#include "ui/ModelController.hpp"

#include <QDir>
#include <QFile>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

namespace {

// Write a .model3.json with 2 motion groups (Idle=1, TapBody=2 motions),
// 2 expressions, 1 hit area — the minimal real-shaped Cubism document the
// ModelInfoParser exercises (FileReferences.Motions / Expressions, HitAreas).
bool createModel(const QString& root, const QString& name,
                 const QByteArray& model3Json)
{
    const QString modelDir = QDir(root).absoluteFilePath(
        QStringLiteral("Resources/Models/") + name);
    if (!QDir().mkpath(modelDir))
        return false;
    const QString path = QDir(modelDir).absoluteFilePath(
        name + QStringLiteral(".model3.json"));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(model3Json);
    f.close();
    return QFile::exists(path);
}

const QByteArray kFullModel3Json = R"json({
  "Version": 3,
  "FileReferences": {
    "Motions": {
      "Idle": [ { "File": "m1.motion3.json" } ],
      "TapBody": [ { "File": "m2.motion3.json" }, { "File": "m3.motion3.json" } ]
    },
    "Expressions": [ { "Name": "f01" }, { "Name": "f02" } ]
  },
  "HitAreas": [ { "Id": "HitAreaBody", "Name": "Body" } ]
})json";

} // namespace

class ModelControllerTest : public QObject
{
    Q_OBJECT

private slots:
    void testScanPopulatesCountsAndDetails();
    void testRescanBumpsRevisionAndEmits();
    void testModelsDirPointsAtResourcesModels();
    void testCorruptModel3JsonDegradesGracefully();
    void testEmptyRendererDirAllAccessorsSafe();
    void testOutOfRangeAndUnknownNameSafe();
};

void ModelControllerTest::testScanPopulatesCountsAndDetails()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");
    QVERIFY2(createModel(dir.path(), QStringLiteral("Alpha"), kFullModel3Json),
             "precondition: failed to create Alpha fixture");

    ModelController controller;
    controller.setRendererDir(dir.path());

    QCOMPARE(controller.modelCount(), 1);
    QCOMPARE(controller.modelDirName(0), QStringLiteral("Alpha"));

    // The three counts mirror the fixture document.
    QCOMPARE(controller.modelMotionGroupCount(0), 2);
    QCOMPARE(controller.modelExpressionCount(0), 2);
    QCOMPARE(controller.modelHitAreaCount(0), 1);

    // By-name detail lists: motion groups in QMap key order (sorted:
    // Idle < TapBody), expressions + hit areas in document order.
    const QStringList expectedGroups{QStringLiteral("Idle"),
                                     QStringLiteral("TapBody")};
    const QStringList expectedExpressions{QStringLiteral("f01"),
                                          QStringLiteral("f02")};
    const QStringList expectedHitAreas{QStringLiteral("Body")};
    QCOMPARE(controller.modelMotionGroups(QStringLiteral("Alpha")), expectedGroups);
    QCOMPARE(controller.modelExpressions(QStringLiteral("Alpha")), expectedExpressions);
    QCOMPARE(controller.modelHitAreas(QStringLiteral("Alpha")), expectedHitAreas);
}

void ModelControllerTest::testRescanBumpsRevisionAndEmits()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");
    QVERIFY2(createModel(dir.path(), QStringLiteral("Alpha"), kFullModel3Json),
             "precondition: failed to create Alpha fixture");

    ModelController controller;
    controller.setRendererDir(dir.path());
    const int revisionAfterInject = controller.revision();
    QVERIFY(revisionAfterInject > 0); // ctor rescan + setRendererDir rescan

    QSignalSpy spy(&controller, &ModelController::modelsChanged);
    QVERIFY(spy.isValid());

    controller.rescan();
    QCOMPARE(controller.revision(), revisionAfterInject + 1);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(controller.modelCount(), 1); // roster still intact after rescan

    // A rescan that ADDS a model updates modelCount too.
    QVERIFY2(createModel(dir.path(), QStringLiteral("Beta"), kFullModel3Json),
             "precondition: failed to create Beta fixture");
    controller.rescan();
    QCOMPARE(controller.revision(), revisionAfterInject + 2);
    QCOMPARE(controller.modelCount(), 2);
    QCOMPARE(controller.modelDirName(1), QStringLiteral("Beta")); // sorted: Alpha < Beta
}

void ModelControllerTest::testModelsDirPointsAtResourcesModels()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");

    ModelController controller;
    controller.setRendererDir(dir.path());

    const QString expected = QDir(dir.path()).absoluteFilePath(
        QStringLiteral("Resources/Models"));
    QCOMPARE(controller.modelsDir(), expected);
}

void ModelControllerTest::testCorruptModel3JsonDegradesGracefully()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");
    // The scanner only checks .model3.json EXISTENCE — a garbage file still
    // qualifies as a model; the parser fails → empty details, entry survives.
    QVERIFY2(createModel(dir.path(), QStringLiteral("Broken"),
                         QByteArrayLiteral("{ not valid json !!")),
             "precondition: failed to create Broken fixture");

    ModelController controller;
    controller.setRendererDir(dir.path());

    QCOMPARE(controller.modelCount(), 1);
    QCOMPARE(controller.modelDirName(0), QStringLiteral("Broken"));
    QCOMPARE(controller.modelMotionGroupCount(0), 0);
    QCOMPARE(controller.modelExpressionCount(0), 0);
    QCOMPARE(controller.modelHitAreaCount(0), 0);
    QVERIFY(controller.modelMotionGroups(QStringLiteral("Broken")).isEmpty());
    QVERIFY(controller.modelExpressions(QStringLiteral("Broken")).isEmpty());
    QVERIFY(controller.modelHitAreas(QStringLiteral("Broken")).isEmpty());
}

void ModelControllerTest::testEmptyRendererDirAllAccessorsSafe()
{
    // No setRendererDir call — the controller must be safe with an empty
    // renderer dir (never-throws contract at the QML boundary).
    ModelController controller;
    controller.rescan(); // explicit rescan on the empty dir

    QCOMPARE(controller.modelCount(), 0);
    QCOMPARE(controller.revision(), 2); // ctor rescan + this explicit one
    QVERIFY(controller.modelsDir().isEmpty());
    QCOMPARE(controller.modelDirName(0), QString());
    QCOMPARE(controller.modelMotionGroupCount(0), 0);
    QCOMPARE(controller.modelExpressionCount(0), 0);
    QCOMPARE(controller.modelHitAreaCount(0), 0);
    QVERIFY(controller.modelMotionGroups(QStringLiteral("Hiyori")).isEmpty());
    QVERIFY(controller.modelExpressions(QStringLiteral("Hiyori")).isEmpty());
    QVERIFY(controller.modelHitAreas(QStringLiteral("Hiyori")).isEmpty());
    // openModelDir on an empty dir is a guarded no-op — must not crash.
    controller.openModelDir();
}

void ModelControllerTest::testOutOfRangeAndUnknownNameSafe()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");
    QVERIFY2(createModel(dir.path(), QStringLiteral("Alpha"), kFullModel3Json),
             "precondition: failed to create Alpha fixture");

    ModelController controller;
    controller.setRendererDir(dir.path());

    QCOMPARE(controller.modelDirName(-1), QString());
    QCOMPARE(controller.modelDirName(99), QString());
    QCOMPARE(controller.modelMotionGroupCount(-5), 0);
    QCOMPARE(controller.modelExpressionCount(99), 0);
    QCOMPARE(controller.modelHitAreaCount(-1), 0);
    QVERIFY(controller.modelMotionGroups(QStringLiteral("Nope")).isEmpty());
    QVERIFY(controller.modelExpressions(QStringLiteral("Nope")).isEmpty());
    QVERIFY(controller.modelHitAreas(QStringLiteral("Nope")).isEmpty());
    QVERIFY(controller.modelMotionGroups(QString()).isEmpty());
}

QTEST_APPLESS_MAIN(ModelControllerTest)
#include "ModelControllerTest.moc"
