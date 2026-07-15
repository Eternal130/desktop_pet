// PathResolveTest — TDD for the backend-aware renderer path resolver (T14).
//
// Five behaviors locked (matches the task spec's MUST DO list):
//   1. OpenGL renderer on the current OS resolves when the file exists.
//   2. Vulkan renderer resolves when desktop-pet-renderer-vulkan exists.
//   3. Missing renderer -> std::nullopt returned, no crash (WARN logged).
//   4. Case-insensitive backend: "OpenGL" / "VULKAN" match the lowercase form.
//   5. Empty backend string falls back to the OpenGL renderer base name.
//
// The platform-specific executable suffix is selected via #ifdef Q_OS_WIN to
// mirror PathResolve.cpp exactly (".exe" on Windows, "" on Linux) — the test
// creates fake exes with the same suffix the production code will look for.
//
// QTEST_APPLESS_MAIN: no event loop needed. QTemporaryDir + QFile::exists()
// are synchronous. defaultRendererDir() (which needs QCoreApplication) is not
// exercised here because its result depends on where the test binary lives.

#include "core/PathResolve.hpp"

#include <QDir>
#include <QFile>
#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

namespace {

// OS-correct suffix matching PathResolve.cpp's #ifdef Q_OS_WIN branch. Must
// agree with the production code or the test would create files the resolver
// never looks for.
#ifdef Q_OS_WIN
constexpr const char* kExeSuffix = ".exe";
#else
constexpr const char* kExeSuffix = "";
#endif

// Create a zero-byte file at <dir>/<name> so QFile::exists() returns true.
// Returns the absolute path of the created file (or an empty string on
// failure — the caller QVERIFYs the result).
QString touchFile(const QString& dir, const QString& name)
{
    const QString path = QDir(dir).absoluteFilePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.close();
    return path;
}

// Build the expected absolute path for a renderer base name under <dir>, with
// the OS-correct suffix. QDir::absoluteFilePath is the same call
// resolveRendererPath uses internally, so the comparison is representation-
// stable regardless of separator quirks.
QString expectedPath(const QString& dir, const QString& baseName)
{
    return QDir(dir).absoluteFilePath(baseName + QString::fromLatin1(kExeSuffix));
}

} // namespace

class PathResolveTest : public QObject
{
    Q_OBJECT

private slots:
    void testOpenGLResolve();
    void testVulkanResolve();
    void testMissingReturnsNullopt();
    void testCaseInsensitiveBackend();
    void testEmptyBackendDefaultsToOpenGL();
};

void PathResolveTest::testOpenGLResolve()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");

    // Precondition: the OpenGL renderer exe exists under the temp dir.
    QVERIFY2(!touchFile(dir.path(), QStringLiteral("desktop-pet-renderer")
                        + QString::fromLatin1(kExeSuffix)).isEmpty(),
             "precondition: failed to create fake OpenGL renderer exe");

    const auto result = core::resolveRendererPath(dir.path(),
                                                  QStringLiteral("opengl"));
    QVERIFY2(result.has_value(),
             "OpenGL renderer should resolve when the file exists");
    QCOMPARE(*result, expectedPath(dir.path(), QStringLiteral("desktop-pet-renderer")));
}

void PathResolveTest::testVulkanResolve()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");

    QVERIFY2(!touchFile(dir.path(), QStringLiteral("desktop-pet-renderer-vulkan")
                        + QString::fromLatin1(kExeSuffix)).isEmpty(),
             "precondition: failed to create fake Vulkan renderer exe");

    const auto result = core::resolveRendererPath(dir.path(),
                                                  QStringLiteral("vulkan"));
    QVERIFY2(result.has_value(),
             "Vulkan renderer should resolve when the file exists");
    QCOMPARE(*result,
             expectedPath(dir.path(), QStringLiteral("desktop-pet-renderer-vulkan")));
}

void PathResolveTest::testMissingReturnsNullopt()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");
    // No files created — both renderer exes are absent under dir.path().

    const auto glMiss = core::resolveRendererPath(dir.path(),
                                                  QStringLiteral("opengl"));
    QVERIFY2(!glMiss.has_value(),
             "Missing OpenGL renderer must yield std::nullopt, not a stale path");

    // Cover the missing-file branch on the Vulkan base name too: the WARN is
    // logged but the call must not crash.
    const auto vkMiss = core::resolveRendererPath(dir.path(),
                                                  QStringLiteral("vulkan"));
    QVERIFY2(!vkMiss.has_value(),
             "Missing Vulkan renderer must also yield std::nullopt");
}

void PathResolveTest::testCaseInsensitiveBackend()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");

    // Both renderer variants exist in this temp dir.
    QVERIFY2(!touchFile(dir.path(), QStringLiteral("desktop-pet-renderer")
                        + QString::fromLatin1(kExeSuffix)).isEmpty(),
             "precondition: failed to create fake OpenGL renderer exe");
    QVERIFY2(!touchFile(dir.path(), QStringLiteral("desktop-pet-renderer-vulkan")
                        + QString::fromLatin1(kExeSuffix)).isEmpty(),
             "precondition: failed to create fake Vulkan renderer exe");

    const QString glExpected =
        expectedPath(dir.path(), QStringLiteral("desktop-pet-renderer"));
    const QString vkExpected =
        expectedPath(dir.path(), QStringLiteral("desktop-pet-renderer-vulkan"));

    // Mixed-case inputs must resolve to the same paths as the lowercase forms.
    const auto glMixed = core::resolveRendererPath(dir.path(),
                                                   QStringLiteral("OpenGL"));
    const auto vkUpper = core::resolveRendererPath(dir.path(),
                                                   QStringLiteral("VULKAN"));
    QVERIFY2(glMixed.has_value(), "Mixed-case 'OpenGL' should resolve");
    QVERIFY2(vkUpper.has_value(), "Uppercase 'VULKAN' should resolve");
    QCOMPARE(*glMixed, glExpected);
    QCOMPARE(*vkUpper, vkExpected);
}

void PathResolveTest::testEmptyBackendDefaultsToOpenGL()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");

    QVERIFY2(!touchFile(dir.path(), QStringLiteral("desktop-pet-renderer")
                        + QString::fromLatin1(kExeSuffix)).isEmpty(),
             "precondition: failed to create fake OpenGL renderer exe");

    // An empty backend string must select the OpenGL base name, NOT fail or
    // map to vulkan. This mirrors the Java reference's fallback for unset
    // graphics_backend config values.
    const auto result = core::resolveRendererPath(dir.path(), QString());
    QVERIFY2(result.has_value(),
             "Empty backend string must fall back to the OpenGL renderer");
    QCOMPARE(*result, expectedPath(dir.path(), QStringLiteral("desktop-pet-renderer")));
}

QTEST_APPLESS_MAIN(PathResolveTest)
#include "PathResolveTest.moc"
