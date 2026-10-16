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

    // ── v1.13 scoped scan (resource isolation, rev2 §3) ────────────────
    void testScopedScanPluginDomainEntries();
    void testScopedScanSameNameCoexistsAcrossDomains();
    void testScopedScanMissingRootHostOnly();
    void testResolvePluginModelPathStrictDomain();
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

// ── v1.13 scoped scan (resource isolation, rev2 §3) ─────────────────────

// Plugin-domain entries appear with owner + ABSOLUTE modelJsonPath; host
// entries keep owner="" and the same absolute path shape as before.
void ModelScannerTest::testScopedScanPluginDomainEntries()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");
    QVERIFY2(createModelDir(dir.path(), QStringLiteral("HostModel")),
             "precondition: failed to create HostModel fixture");

    // Plugin data domain: <tmp>/PluginData/<owner>/Models/<Name>/<Name>
    // .model3.json — the exact rev2 §2 layout.
    const QString owner = QStringLiteral("com.example.pet");
    const QString pluginModel = QStringLiteral("PluginModel");
    const QString modelDir =
        QDir(dir.path()).absoluteFilePath(
            QStringLiteral("PluginData/%1/Models/%2").arg(owner, pluginModel));
    QVERIFY2(QDir().mkpath(modelDir),
             "precondition: failed to create plugin-domain model dir");
    QVERIFY2(touchFile(modelDir, pluginModel + QStringLiteral(".model3.json")),
             "precondition: failed to create plugin model3.json");

    const QVector<core::ScannedModel> result =
        core::scanModelsScoped(dir.path(),
                               QDir(dir.path()).absoluteFilePath(
                                   QStringLiteral("PluginData")));

    QCOMPARE(result.size(), 2);
    // Host first (owner="" sorts before non-empty owners).
    QCOMPARE(result.at(0).name, QStringLiteral("HostModel"));
    QCOMPARE(result.at(0).owner, QString());
    QCOMPARE(result.at(0).modelJsonPath,
             QDir(dir.path()).absoluteFilePath(
                 QStringLiteral("Resources/Models/HostModel/HostModel.model3.json")));
    // Plugin entry: owner = the immediate dir name, absolute json path.
    QCOMPARE(result.at(1).name, QStringLiteral("PluginModel"));
    QCOMPARE(result.at(1).owner, owner);
    QCOMPARE(result.at(1).modelJsonPath,
             QDir(modelDir).absoluteFilePath(
                 QStringLiteral("PluginModel.model3.json")));
}

// NO cross-domain dedup: the SAME model name in the host domain and a
// plugin domain (and in a SECOND plugin domain) yields distinct entries.
void ModelScannerTest::testScopedScanSameNameCoexistsAcrossDomains()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");
    QVERIFY2(createModelDir(dir.path(), QStringLiteral("Same")),
             "precondition: failed to create host Same fixture");

    const QString root = QDir(dir.path()).absoluteFilePath(
        QStringLiteral("PluginData"));
    for (const QString& owner : {QStringLiteral("plugA"),
                                 QStringLiteral("plugB")}) {
        const QString modelDir = QDir(root).absoluteFilePath(
            QStringLiteral("%1/Models/Same").arg(owner));
        QVERIFY2(QDir().mkpath(modelDir),
                 "precondition: failed to create plugin Same fixture");
        QVERIFY2(touchFile(modelDir, QStringLiteral("Same.model3.json")),
                 "precondition: failed to create plugin model3.json");
    }

    const QVector<core::ScannedModel> result =
        core::scanModelsScoped(dir.path(), root);
    QCOMPARE(result.size(), 3);
    int hostCount = 0, plugACount = 0, plugBCount = 0;
    for (const core::ScannedModel& m : result) {
        if (m.owner.isEmpty())
            ++hostCount;
        else if (m.owner == QLatin1String("plugA"))
            ++plugACount;
        else if (m.owner == QLatin1String("plugB"))
            ++plugBCount;
        QCOMPARE(m.name, QStringLiteral("Same"));
    }
    QCOMPARE(hostCount, 1);
    QCOMPARE(plugACount, 1);
    QCOMPARE(plugBCount, 1);
}

// A missing pluginDataRoot contributes nothing (host-only result); an
// empty pluginDataRoot is the explicit host-only form. Both shapes stay
// non-crashing and keep the host entries.
void ModelScannerTest::testScopedScanMissingRootHostOnly()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");
    QVERIFY2(createModelDir(dir.path(), QStringLiteral("Solo")),
             "precondition: failed to create Solo fixture");

    const QVector<core::ScannedModel> missing =
        core::scanModelsScoped(
            dir.path(),
            QDir(dir.path()).absoluteFilePath(QStringLiteral("NoSuchRoot")));
    QCOMPARE(missing.size(), 1);
    QCOMPARE(missing.at(0).name, QStringLiteral("Solo"));
    QCOMPARE(missing.at(0).owner, QString());

    const QVector<core::ScannedModel> emptyRoot =
        core::scanModelsScoped(dir.path(), QString());
    QCOMPARE(emptyRoot.size(), 1);
    QCOMPARE(emptyRoot.at(0).owner, QString());
}

// resolvePluginModelPath (rev2 §3.2c): strict plugin-domain resolution —
// hit returns the absolute path; every miss shape returns empty; the HOST
// domain is never consulted as a fallback.
void ModelScannerTest::testResolvePluginModelPathStrictDomain()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");
    const QString root = QDir(dir.path()).absoluteFilePath(
        QStringLiteral("PluginData"));
    const QString modelDir = QDir(root).absoluteFilePath(
        QStringLiteral("plugA/Models/InDomain"));
    QVERIFY2(QDir().mkpath(modelDir),
             "precondition: failed to create plugin model dir");
    QVERIFY2(touchFile(modelDir, QStringLiteral("InDomain.model3.json")),
             "precondition: failed to create plugin model3.json");
    // Same model name in the HOST domain — must NOT be returned for a
    // scoped lookup that misses the plugin domain.
    QVERIFY2(createModelDir(dir.path(), QStringLiteral("InDomain")),
             "precondition: failed to create host twin fixture");

    const QString hit = core::resolvePluginModelPath(
        root, QStringLiteral("plugA"), QStringLiteral("InDomain"));
    QCOMPARE(hit, QDir(modelDir).absoluteFilePath(
                       QStringLiteral("InDomain.model3.json")));

    // Misses: unknown model / unknown owner / empty args / missing root.
    QCOMPARE(core::resolvePluginModelPath(
                 root, QStringLiteral("plugA"), QStringLiteral("Nope")),
             QString());
    QCOMPARE(core::resolvePluginModelPath(
                 root, QStringLiteral("plugB"), QStringLiteral("InDomain")),
             QString()); // NO host fallback — host twin exists but is ignored
    QCOMPARE(core::resolvePluginModelPath(
                 root, QString(), QStringLiteral("InDomain")),
             QString());
    QCOMPARE(core::resolvePluginModelPath(
                 root, QStringLiteral("plugA"), QString()),
             QString());
    QCOMPARE(core::resolvePluginModelPath(
                 QDir(dir.path()).absoluteFilePath(QStringLiteral("NoRoot")),
                 QStringLiteral("plugA"), QStringLiteral("InDomain")),
             QString());
}

QTEST_APPLESS_MAIN(ModelScannerTest)
#include "ModelScannerTest.moc"
