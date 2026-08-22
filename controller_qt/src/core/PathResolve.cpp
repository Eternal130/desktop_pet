#include "core/PathResolve.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QString>

// spdlog MUST be included before logging/Logging.hpp — Logging.hpp references
// the SPDLOG_* macros but does NOT include spdlog headers itself (T5 finding
// in .omo/notepads/qt-controller-foundation/learnings.md).
#include <spdlog/spdlog.h>
#include "logging/Logging.hpp"

namespace core {

namespace {

// Per-platform executable suffix (architecture-blueprint.md §9.4):
// Windows renderer binaries carry ".exe"; Linux renderer binaries have none.
#ifdef Q_OS_WIN
constexpr const char* kExeSuffix = ".exe";
#else
constexpr const char* kExeSuffix = "";
#endif

// Map the graphics backend name to the renderer executable base name.
//
//   "vulkan" (case-insensitive) -> desktop-pet-renderer-vulkan
//   "opengl", empty string, or any unrecognized value -> desktop-pet-renderer
//
// Matches docs/controller/README.md §2.3: anything that is not vulkan falls
// back to the OpenGL renderer. The Java reference (MainWindowController.
// resolveRendererPath) treats unknown backend values the same way rather than
// failing — a wrong config silently selects the OpenGL build instead of
// crashing instance startup.
QString rendererBaseName(const QString& graphicsBackend)
{
    if (graphicsBackend.compare(QStringLiteral("vulkan"),
                                Qt::CaseInsensitive) == 0) {
        return QStringLiteral("desktop-pet-renderer-vulkan");
    }
    return QStringLiteral("desktop-pet-renderer");
}

} // namespace

std::optional<QString> resolveRendererPath(const QString& rendererDir,
                                           const QString& graphicsBackend)
{
    const QString baseName = rendererBaseName(graphicsBackend);
    const QString fullPath = QDir(rendererDir).absoluteFilePath(
        baseName + QString::fromLatin1(kExeSuffix));

    if (QFile::exists(fullPath))
        return fullPath;

    // Missing file: log a WARN so a misconfigured rendererDir or absent Vulkan
    // install is visible in the log (the Java reference logs the same
    // fallback). The caller (T13 ProcessManager / T27 welcome page) decides
    // whether to abort the instance or skip the backend.
    LOG_WARN("Renderer executable not found at \"{}\" (backend=\"{}\")",
             fullPath.toStdString(), graphicsBackend.toStdString());
    return std::nullopt;
}

QString defaultRendererDir()
{
    // Two layouts must resolve:
    //   1. Deployed: build.py places the controller_qt exe and the renderer
    //      exes side-by-side in build/bin/ — rendererDir == applicationDirPath
    //      (matches EnvironmentChecker::resolveRendererDir).
    //   2. Build tree: the exe still sits in the CMake build dir
    //      (build/controller_qt/), one level below build/bin.
    // Probe the app dir first; fall back to the sibling layout only when no
    // renderer exe lives next to the controller.
    const QString appDir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
    const QString suffix = QStringLiteral(".exe");
#else
    const QString suffix;
#endif
    const bool rendererInAppDir =
        QFile::exists(QDir(appDir).absoluteFilePath(
            QStringLiteral("desktop-pet-renderer") + suffix))
        || QFile::exists(QDir(appDir).absoluteFilePath(
            QStringLiteral("desktop-pet-renderer-vulkan") + suffix));
    return rendererInAppDir ? appDir
                            : appDir + QStringLiteral("/../build/bin");
}

} // namespace core
