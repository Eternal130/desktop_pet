// ModelScannerTest — Phase 5 todo 4 TDD for the model-directory scanner.
//
// Five behaviors locked (matches the task spec's MUST DO list):
//   1. A fake <tmp>/Resources/Models/<Name>/<Name>.model3.json qualifies and
//      is reported by name.
//   2. Multiple qualifying subdirs are returned sorted case-insensitively.
//   3. A subdir WITHOUT the <name>.model3.json file is filtered out.
//   4. A nonexistent rendererDir → empty list (no throw, no crash).
//   5. Empty rendererDir → empty list.
//
// Why the test uses QTemporaryDir instead of the real Cubism Samples dir:
//   scanAvailableModels scans <rendererDir>/Resources/Models/*/ (note the
//   "Models" segment). The Cubism Samples layout is
//     third_party/.../Samples/Resources/<Model>/
//   (NO "Models" segment — the renderer's shipped layout differs from the
//   upstream Cubism Samples layout by exactly that one directory level).
//   So to test scanAvailableModels against real fixtures we would have to
//   pass a rendererDir such that <rendererDir>/Resources/Models resolves to
//   the Cubism Samples/Resources dir — fragile and misleading. A temp dir
//   with the exact shipped layout is cleaner and tests the actual contract.
//
// QTEST_APPLESS_MAIN: pure QDir/QFile ops, no event loop needed.

#include "core/ModelScanner.hpp"

#include <QDir>
#include <QFile>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

namespace {

// Create a zero-byte file at <dir>/<name> so QFile::exists() returns true.
// The scanner only checks existence, never parses the file — so the content
// is irrelevant (mirrors PathResolveTest::touchFile).
bool touchFile(const QString& dir, const QString& name)
{
    const QString path = QDir(dir).absoluteFilePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.close();
    return QFile::exists(path);
}

// Create <root>/Resources/Models/<modelName>/<modelName>.model3.json — the
// exact layout scanAvailableModels expects. Returns true on success.
bool createModelDir(const QString& root, const QString& modelName)
{
    const QString modelDir = QDir(root).absoluteFilePath(
        QStringLiteral("Resources/Models/") + modelName);
    if (!QDir().mkpath(modelDir))
        return false;
    return touchFile(modelDir, modelName + QStringLiteral(".model3.json"));
}

} // namespace

class ModelScannerTest : public QObject
{
    Q_OBJECT

private slots:
    void testScanFindsSingleModel();
    void testScanReturnsSortedMultiple();
    void testScanFiltersDirWithoutModel3Json();
    void testScanNonexistentDirReturnsEmpty();
    void testScanEmptyRendererDirReturnsEmpty();
};

void ModelScannerTest::testScanFindsSingleModel()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");
    QVERIFY2(createModelDir(dir.path(), QStringLiteral("TestModel")),
             "precondition: failed to create TestModel fixture");

    const QStringList result = core::scanAvailableModels(dir.path());
    QCOMPARE(result.size(), 1);
    QCOMPARE(result.first(), QStringLiteral("TestModel"));
}

void ModelScannerTest::testScanReturnsSortedMultiple()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");
    // Create in non-sorted order to prove the scanner sorts.
    QVERIFY2(createModelDir(dir.path(), QStringLiteral("zebra")),
             "precondition: failed to create zebra fixture");
    QVERIFY2(createModelDir(dir.path(), QStringLiteral("Alpha")),
             "precondition: failed to create Alpha fixture");
    QVERIFY2(createModelDir(dir.path(), QStringLiteral("mid")),
             "precondition: failed to create mid fixture");

    const QStringList result = core::scanAvailableModels(dir.path());
    QCOMPARE(result.size(), 3);
    // Case-insensitive sort: Alpha < mid < zebra
    QCOMPARE(result.at(0), QStringLiteral("Alpha"));
    QCOMPARE(result.at(1), QStringLiteral("mid"));
    QCOMPARE(result.at(2), QStringLiteral("zebra"));
}

void ModelScannerTest::testScanFiltersDirWithoutModel3Json()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");
    // A qualifying model (has the correctly-named .model3.json).
    QVERIFY2(createModelDir(dir.path(), QStringLiteral("HasFile")),
             "precondition: failed to create HasFile fixture");

    // A bare subdir without the .model3.json — must be filtered out.
    const QString noFileDir = QDir(dir.path()).absoluteFilePath(
        QStringLiteral("Resources/Models/NoFile"));
    QVERIFY2(QDir().mkpath(noFileDir),
             "precondition: failed to create NoFile subdir");

    const QStringList result = core::scanAvailableModels(dir.path());
    QCOMPARE(result.size(), 1);
    QCOMPARE(result.first(), QStringLiteral("HasFile"));
    QVERIFY2(!result.contains(QStringLiteral("NoFile")),
             "Subdir without <name>.model3.json must NOT be reported");
}

void ModelScannerTest::testScanNonexistentDirReturnsEmpty()
{
    // A path that definitively does not exist. No throw, no crash, empty list.
    const QString bogus = QStringLiteral("/this/path/does/not/exist/anywhere");
    QVERIFY2(!QFile::exists(bogus), "precondition: bogus path must not exist");

    const QStringList result = core::scanAvailableModels(bogus);
    QVERIFY2(result.isEmpty(),
             "Nonexistent rendererDir must yield an empty list, not a crash");
}

void ModelScannerTest::testScanEmptyRendererDirReturnsEmpty()
{
    const QStringList result = core::scanAvailableModels(QString());
    QVERIFY2(result.isEmpty(),
             "Empty rendererDir must yield an empty list");
}

QTEST_APPLESS_MAIN(ModelScannerTest)
#include "ModelScannerTest.moc"
