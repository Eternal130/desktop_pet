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
#include <QStandardPaths>

namespace {

// Registry key + value name — kept byte-for-byte compatible with the legacy
// config format so existing installs keep their auto-launch state.
const QString kRegKey =
    QStringLiteral("HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
const QString kRegValueName = QStringLiteral("DesktopPet");
const QString kDesktopFileName = QStringLiteral("desktop-pet.desktop");

// --- Default suppliers (production paths) ---

// The current binary's path ('/'-separated on every platform — Qt's
// normalization). The platform writers escape as needed: enableWindows
// converts to native separators and wraps the value in double quotes
// (unquoted Run-key data splits at the first space); enableLinux quotes
// it verbatim in the Exec= line.
QString defaultAppPath()
{
    return QCoreApplication::applicationFilePath();
}

// The production linuxConfigDirSupplier — delegates to the public static
// defaultAutostartDir() (single source of truth; the static is exposed so
// tests can lock the $XDG_CONFIG_HOME precedence without writing into the
// real home directory).
QString defaultLinuxConfigDir()
{
    return AutoLaunchManager::defaultAutostartDir();
}

// reg.exe program path. Resolved ONCE via QStandardPaths::findExecutable
// (PATH lookup, .exe suffix handled by Qt on Windows); falls back to the
// bare "reg" when the lookup comes up empty (reg.exe lives in System32,
// which is always on PATH, so the fallback is belt-and-braces).
QString regProgram()
{
    static const QString resolved = [] {
        const QString found =
            QStandardPaths::findExecutable(QStringLiteral("reg"));
        return found.isEmpty() ? QStringLiteral("reg") : found;
    }();
    return resolved;
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
    if (!p.waitForFinished(5000)) {
        // Timeout: QProcess's exitCode() still holds its initial 0 here —
        // callers checking exitCode == 0 would see a FALSE success. Fail
        // closed with -1 (every downstream check treats non-zero as
        // failure) after reaping the straggler.
        LOG_WARN("AutoLaunch: '{}' timed out", program.toStdString());
        p.kill();
        p.waitForFinished(1000);
        return {-1, QString::fromLocal8Bit(p.readAll())};
    }
    if (p.exitStatus() != QProcess::NormalExit) {
        // Crashed (exitStatus CrashExit) — same fail-closed contract.
        LOG_WARN("AutoLaunch: '{}' crashed", program.toStdString());
        return {-1, QString::fromLocal8Bit(p.readAll())};
    }
    AutoLaunchManager::RegResult r;
    r.exitCode = p.exitCode();
    r.output = QString::fromLocal8Bit(p.readAll());
    return r;
}

} // namespace

// ---------------------------------------------------------------------------
// Public static — production autostart dir (XDG-aware)
// ---------------------------------------------------------------------------

QString AutoLaunchManager::defaultAutostartDir()
{
    // XDG base-dir spec: $XDG_CONFIG_HOME only counts when set AND
    // absolute; otherwise the default is ~/.config. '/autostart' is the
    // XDG autostart-spec suffix.
    const QString xdg = qEnvironmentVariable("XDG_CONFIG_HOME");
    const QString base = (!xdg.isEmpty() && QDir::isAbsolutePath(xdg))
        ? xdg
        : QDir::homePath() + QStringLiteral("/.config");
    return base + QStringLiteral("/autostart");
}

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
        regProgram(),
        {QStringLiteral("query"), kRegKey,
         QStringLiteral("/v"), kRegValueName});
    // Require BOTH the value name and the REG_SZ type marker: a successful
    // `reg query /v DesktopPet` prints "    DesktopPet    REG_SZ    <data>".
    // The name alone can appear in unrelated output (e.g. the key path echo
    // of a sibling-value listing), so the type marker is the cheap tighten.
    return r.exitCode == 0 && r.output.contains(kRegValueName)
        && r.output.contains(QLatin1String("REG_SZ"));
}

bool AutoLaunchManager::enableWindows()
{
    const QString appPath = m_appPathSupplier();
    // Store the value data as a QUOTED native-separator path: the Run-key
    // launcher (Explorer/Userinit) splits UNQUOTED data at the first
    // space — `C:\Program Files\...\app.exe` would execute `C:\Program`
    // (the classic unquoted-CWE-428 breakage). Native separators because
    // the value is consumed by the OS shell, not Qt (which normalizes
    // paths to '/').
    const QString quotedPath = QLatin1Char('"')
        + QDir::toNativeSeparators(appPath) + QLatin1Char('"');
    const RegResult r = m_regExecutor(
        regProgram(),
        {QStringLiteral("add"), kRegKey,
         QStringLiteral("/v"), kRegValueName,
         QStringLiteral("/t"), QStringLiteral("REG_SZ"),
         QStringLiteral("/d"), quotedPath,
         QStringLiteral("/f")});
    if (r.exitCode == 0) {
        LOG_INFO("AutoLaunch enabled (Windows registry) path='{}'",
                 quotedPath.toStdString());
        return true;
    }
    LOG_WARN("AutoLaunch enable failed: reg exit code {}", r.exitCode);
    return false;
}

bool AutoLaunchManager::disableWindows()
{
    const RegResult r = m_regExecutor(
        regProgram(),
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
// Linux — atomic write/delete desktop-pet.desktop in the XDG autostart dir
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
    // Exec is wrapped in double quotes: the Exec field splits on UNQUOTED
    // whitespace, so an install path containing a space would launch a
    // nonexistent binary. Icon stays unquoted (the icon-spec has no
    // quoting — a quoted value would break icon-name resolution).
    const QString execValue = QLatin1Char('"') + appPath + QLatin1Char('"');
    const QByteArray content = QStringLiteral(
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=Desktop Pet\n"
        "Exec=%1\n"
        "Icon=%2\n"
        "Terminal=false\n").arg(execValue, appPath).toUtf8();

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
