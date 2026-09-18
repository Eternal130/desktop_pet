#include "system/AutoLaunchManager.hpp"

// spdlog MUST be included before logging/Logging.hpp — Logging.hpp references
// the SPDLOG_* macros but does NOT include spdlog headers itself (T5 finding
// in .omo/notepads/qt-controller-foundation/learnings.md).
#include <spdlog/spdlog.h>
#include "logging/Logging.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSaveFile>

namespace {

// Registry key + value name — kept byte-for-byte compatible with the legacy
// config format so existing installs keep their auto-launch state.
const QString kRegKey =
    QStringLiteral("HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
const QString kRegValueName = QStringLiteral("DesktopPet");
const QString kDesktopFileName = QStringLiteral("desktop-pet.desktop");

// --- Default suppliers (production paths) ---

// The current binary's path. Used as the Run value's data and the .desktop
// Exec= line. QCoreApplication::applicationFilePath uses '/' on every platform
// (Qt-normalized); reg.exe accepts '/' in argument values (only '/' at arg
// start is a switch), so no path escaping is needed.
QString defaultAppPath()
{
    return QCoreApplication::applicationFilePath();
}

// ~/.config/autostart — the XDG autostart directory. controller_qt convention
// (Phase 0-4 ConfigDir) uses ~/.config on both Windows + Linux (NOT %APPDATA%).
QString defaultLinuxConfigDir()
{
    return QDir::homePath() + QStringLiteral("/.config/autostart");
}

// Compile-time platform string. Mirrors Java's os.name detection.
QString defaultPlatform()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_LINUX)
    return QStringLiteral("linux");
#else
    return QStringLiteral("other");
#endif
}

// Real reg.exe executor — runs synchronously via QProcess::start +
// waitForFinished + readAll. Mirrors Java's ProcessBuilder + waitFor.
// Combines stdout+stderr (Java's redirectErrorStream) so isEnabled can grep
// the value name in either stream.
AutoLaunchManager::RegResult defaultRegExecutor(const QString& program,
                                                const QStringList& args)
{
    QProcess p;
    p.setProgram(program);
    p.setArguments(args);
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(QIODevice::ReadOnly);
    if (!p.waitForStarted(5000)) {
        LOG_WARN("AutoLaunch: failed to start '{}'", program.toStdString());
        return {-1, QString()};
    }
    p.waitForFinished(10000);
    AutoLaunchManager::RegResult r;
    r.exitCode = p.exitCode();
    r.output = QString::fromLocal8Bit(p.readAll());
    return r;
}

} // namespace

// ---------------------------------------------------------------------------
// Constructors
// ---------------------------------------------------------------------------

AutoLaunchManager::AutoLaunchManager(QObject* parent)
    : AutoLaunchManager(defaultAppPath, defaultLinuxConfigDir,
                         defaultRegExecutor, defaultPlatform, parent) {}

AutoLaunchManager::AutoLaunchManager(std::function<QString()> appPathSupplier,
                                     std::function<QString()> linuxConfigDirSupplier,
                                     RegExecutor regExecutor,
                                     std::function<QString()> platformSupplier,
                                     QObject* parent)
    : QObject(parent)
    , m_appPathSupplier(std::move(appPathSupplier))
    , m_linuxConfigDirSupplier(std::move(linuxConfigDirSupplier))
    , m_regExecutor(std::move(regExecutor))
    , m_platformSupplier(std::move(platformSupplier)) {}

// ---------------------------------------------------------------------------
// Public API — dispatch by platform
// ---------------------------------------------------------------------------

bool AutoLaunchManager::isEnabled() const
{
    const QString p = m_platformSupplier();
    if (p == QLatin1String("windows")) return checkWindowsRegistry();
    if (p == QLatin1String("linux"))   return checkLinuxDesktopFile();
    return false;
}

bool AutoLaunchManager::enable()
{
    const QString p = m_platformSupplier();
    if (p == QLatin1String("windows")) return enableWindows();
    if (p == QLatin1String("linux"))   return enableLinux();
    LOG_WARN("AutoLaunch: enable() not supported on platform '{}'",
             p.toStdString());
    return false;
}

bool AutoLaunchManager::disable()
{
    const QString p = m_platformSupplier();
    if (p == QLatin1String("windows")) return disableWindows();
    if (p == QLatin1String("linux"))   return disableLinux();
    LOG_WARN("AutoLaunch: disable() not supported on platform '{}'",
             p.toStdString());
    return false;
}

// ---------------------------------------------------------------------------
// Windows — reg.exe add/delete/query on HKCU\...\Run value 'DesktopPet'
// ---------------------------------------------------------------------------

bool AutoLaunchManager::checkWindowsRegistry() const
{
    const RegResult r = m_regExecutor(
        QStringLiteral("reg"),
        {QStringLiteral("query"), kRegKey,
         QStringLiteral("/v"), kRegValueName});
    return r.exitCode == 0 && r.output.contains(kRegValueName);
}

bool AutoLaunchManager::enableWindows()
{
    const QString appPath = m_appPathSupplier();
    const RegResult r = m_regExecutor(
        QStringLiteral("reg"),
        {QStringLiteral("add"), kRegKey,
         QStringLiteral("/v"), kRegValueName,
         QStringLiteral("/t"), QStringLiteral("REG_SZ"),
         QStringLiteral("/d"), appPath,
         QStringLiteral("/f")});
    if (r.exitCode == 0) {
        LOG_INFO("AutoLaunch enabled (Windows registry) path='{}'",
                 appPath.toStdString());
        return true;
    }
    LOG_WARN("AutoLaunch enable failed: reg exit code {}", r.exitCode);
    return false;
}

bool AutoLaunchManager::disableWindows()
{
    const RegResult r = m_regExecutor(
        QStringLiteral("reg"),
        {QStringLiteral("delete"), kRegKey,
         QStringLiteral("/v"), kRegValueName,
         QStringLiteral("/f")});
    if (r.exitCode == 0) {
        LOG_INFO("AutoLaunch disabled (Windows registry)");
        return true;
    }
    // reg delete exits non-zero when the value doesn't exist (already
    // disabled). Treat that as idempotent success — the end-state (value not
    // present) is what disable() promises.
    LOG_INFO("AutoLaunch disable: reg exit code {} (treated as success — "
             "value likely already absent)", r.exitCode);
    return true;
}

// ---------------------------------------------------------------------------
// Linux — atomic write/delete ~/.config/autostart/desktop-pet.desktop
// ---------------------------------------------------------------------------

bool AutoLaunchManager::checkLinuxDesktopFile() const
{
    return QFile::exists(desktopFilePath());
}

bool AutoLaunchManager::enableLinux()
{
    const QString dir = m_linuxConfigDirSupplier();
    const QString path = dir + QLatin1Char('/') + kDesktopFileName;
    const QString appPath = m_appPathSupplier();

    // Create the autostart dir if missing (mirrors Java's
    // Files.createDirectories). mkpath is idempotent.
    if (!QDir().mkpath(dir)) {
        LOG_WARN("AutoLaunch enable: mkpath failed for '{}'",
                 dir.toStdString());
        return false;
    }

    // XDG .desktop content. Matches the task spec (Type/Name/Exec/Icon/
    // Terminal=false). The Java reference uses Hidden=false instead of
    // Icon+Terminal; the C++ port follows the task spec for a more canonical
    // XDG entry. Exec + Icon both = appPath (single-binary distribution).
    const QByteArray content = QStringLiteral(
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=Desktop Pet\n"
        "Exec=%1\n"
        "Icon=%1\n"
        "Terminal=false\n").arg(appPath).toUtf8();

    // QSaveFile = atomic write (MoveFileEx on Windows, rename(2) on POSIX),
    // mirroring the InstanceConfigManager::atomicWrite pattern from T20.
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        LOG_WARN("AutoLaunch enable: open failed for '{}'",
                 path.toStdString());
        return false;
    }
    if (f.write(content) != content.size() || !f.flush()) {
        LOG_WARN("AutoLaunch enable: short write to '{}'",
                 path.toStdString());
        f.cancelWriting();
        return false;
    }
    if (!f.commit()) {
        LOG_WARN("AutoLaunch enable: commit failed for '{}'",
                 path.toStdString());
        return false;
    }
    LOG_INFO("AutoLaunch enabled (Linux .desktop) path='{}'",
             path.toStdString());
    return true;
}

bool AutoLaunchManager::disableLinux()
{
    const QString path = desktopFilePath();
    // Idempotent: if the file is already absent, that's the disabled state —
    // return true (matches Java's Files.deleteIfExists semantics).
    if (!QFile::exists(path)) {
        LOG_INFO("AutoLaunch disable: file already absent '{}'",
                 path.toStdString());
        return true;
    }
    if (!QFile::remove(path)) {
        LOG_WARN("AutoLaunch disable: QFile::remove failed for '{}'",
                 path.toStdString());
        return false;
    }
    LOG_INFO("AutoLaunch disabled (Linux .desktop)");
    return true;
}

QString AutoLaunchManager::desktopFilePath() const
{
    return m_linuxConfigDirSupplier() + QLatin1Char('/') + kDesktopFileName;
}
