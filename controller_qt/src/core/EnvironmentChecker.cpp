#include "core/EnvironmentChecker.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QHostAddress>
#include <QTcpServer>

// spdlog MUST be included before logging/Logging.hpp — Logging.hpp references
// the SPDLOG_* macros but does NOT include spdlog headers itself (T5 finding
// in .omo/notepads/qt-controller-foundation/learnings.md).
#include <spdlog/spdlog.h>
#include "core/PathResolve.hpp"
#include "logging/Logging.hpp"

namespace {
// Subpath under the renderer dir holding the Live2D model trees.
// (AGENTS.md: renderer working dir = its exe dir, so Resources/Models/<Name>/
// resolves relative to the directory this checker probes.)
constexpr const char* kModelsSubPath = "Resources/Models";

// glob filter for Live2D Cubism 5 model definition files.
const QStringList kModel3Filter =
    QStringList() << QStringLiteral("*.model3.json");
} // namespace

EnvironmentChecker::EnvironmentChecker(QObject* parent)
    : QObject(parent)
{
}

void EnvironmentChecker::runChecks()
{
    runAllChecks();
    emit checksChanged();
}

QString EnvironmentChecker::resolveRendererDir()
{
    // applicationDirPath() is the directory of the running controller exe.
    // build.py + CMake place the controller and the renderer exes side-by-side
    // in build/bin/, so the renderer dir == applicationDirPath(). This matches
    // poc_main.cpp:277 (the proven T6 round-trip). See the header comment for
    // why core::defaultRendererDir() is deliberately NOT used here.
    return QCoreApplication::applicationDirPath();
}

void EnvironmentChecker::runAllChecks()
{
    m_rendererDir = resolveRendererDir();
    m_modelsDir = QDir(m_rendererDir)
                      .absoluteFilePath(QString::fromLatin1(kModelsSubPath));

    // (1) OpenGL renderer exe — backend "opengl" → desktop-pet-renderer[.exe].
    const auto gl = core::resolveRendererPath(
        m_rendererDir, QStringLiteral("opengl"));
    m_openglRendererReady = gl.has_value();
    m_openglRendererPath = gl.value_or(QString());

    // (2) Vulkan renderer exe — backend "vulkan" → desktop-pet-renderer-vulkan.
    const auto vk = core::resolveRendererPath(
        m_rendererDir, QStringLiteral("vulkan"));
    m_vulkanRendererReady = vk.has_value();
    m_vulkanRendererPath = vk.value_or(QString());

    // (4) Resources/Models/ directory exists (QDir::exists returns false for a
    // file, so this only matches an actual directory at the resolved path).
    m_resourcesReady = QDir(m_modelsDir).exists();

    // (5) ≥1 model available — scan each subdir of Models/ for a *.model3.json.
    // A model "tree" is a directory containing at least one .model3.json
    // (Cubism SDK 5 model definition). The dir name is the model identifier
    // the renderer accepts via --model (e.g. "Hiyori").
    m_availableModels.clear();
    if (m_resourcesReady) {
        const QDir modelsDir(m_modelsDir);
        const QStringList subdirs =
            modelsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString& name : subdirs) {
            const QDir modelDir(modelsDir.absoluteFilePath(name));
            if (!modelDir.entryList(kModel3Filter, QDir::Files).isEmpty())
                m_availableModels.append(name);
        }
    }
    m_modelsAvailable = !m_availableModels.isEmpty();

    // (6) Port 9001 bindable — try to listen on 127.0.0.1, close immediately.
    // QTcpServer::listen is synchronous; no event loop needed. If something
    // EXTERNAL is already listening (another controller instance, the Java
    // controller, a stale renderer), listen fails and portBindable reports
    // false. Our own WsServer (started in main before the welcome page
    // probes) does NOT count as a conflict — m_ownServerListening overrides.
    QTcpServer probe;
    m_portBindable = m_ownServerListening
        || probe.listen(QHostAddress(QHostAddress::LocalHost), kPort);
    if (m_portBindable) {
        probe.close();
    } else {
        LOG_WARN("EnvironmentChecker: port {} not bindable ({}); another "
                 "process may be listening",
                 kPort, probe.errorString().toStdString());
    }

    LOG_INFO("EnvironmentChecker: opengl={} vulkan={} qt=true resources={} "
             "models={} (count={}) port={}",
             m_openglRendererReady, m_vulkanRendererReady, m_resourcesReady,
             m_modelsAvailable, m_availableModels.size(), m_portBindable);
}

void EnvironmentChecker::setOwnServerListening(bool listening)
{
    m_ownServerListening = listening;
}

// ── Read-only getters returning the cached probe results ───────────────────
// allReady excludes qtRuntimeReady (always true by definition) so the banner
// reflects the four external dependencies the controller needs before it can
// launch a pet instance.

bool EnvironmentChecker::openglRendererReady() const
{
    return m_openglRendererReady;
}

bool EnvironmentChecker::vulkanRendererReady() const
{
    return m_vulkanRendererReady;
}

bool EnvironmentChecker::resourcesReady() const
{
    return m_resourcesReady;
}

bool EnvironmentChecker::modelsAvailable() const
{
    return m_modelsAvailable;
}

bool EnvironmentChecker::portBindable() const
{
    return m_portBindable;
}

QStringList EnvironmentChecker::availableModels() const
{
    return m_availableModels;
}

bool EnvironmentChecker::allReady() const
{
    return m_openglRendererReady && m_vulkanRendererReady && m_resourcesReady
           && m_modelsAvailable && m_portBindable;
}

QString EnvironmentChecker::rendererDirectory() const
{
    return m_rendererDir;
}

QString EnvironmentChecker::openglRendererPath() const
{
    return m_openglRendererPath;
}

QString EnvironmentChecker::vulkanRendererPath() const
{
    return m_vulkanRendererPath;
}

QString EnvironmentChecker::modelsDirectory() const
{
    return m_modelsDir;
}
