#include "core/ConfigDir.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStandardPaths>

// spdlog MUST be included before logging/Logging.hpp — Logging.hpp references
// the SPDLOG_* macros but does NOT include spdlog headers itself (T5 finding
// in .omo/notepads/qt-controller-foundation/learnings.md).
#include <spdlog/spdlog.h>
#include "logging/Logging.hpp"

namespace ConfigDir {

namespace {

// QStandardPaths results never end in a separator and always use '/'
// (even on Windows) — normalize to the trailing-'/' contract every caller
// of this namespace relies on ("<root>instances" string concatenation).
QString withTrailingSlash(const QString& path)
{
    return path.endsWith(QLatin1Char('/')) ? path : path + QLatin1Char('/');
}

} // namespace

QString configDir()
{
    // AppConfigLocation with app name "desktop-pet" + empty org name:
    //   Linux   ~/.config/desktop-pet      (byte-identical to the old
    //                                      homePath()+"/.config/desktop-pet")
    //   Windows %APPDATA%\desktop-pet      (Roaming)
    return withTrailingSlash(
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
}

QString dataDir()
{
    // AppLocalDataLocation (NOT AppDataLocation — that one resolves to the
    // ROAMING %APPDATA% on Windows): %LOCALAPPDATA%\desktop-pet on Windows,
    // ~/.local/share/desktop-pet on Linux (identical to AppDataLocation
    // there). Data (voice packs / downloads) is machine-local, not roaming.
    return withTrailingSlash(
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
}

QString userVoicePacksDir()
{
    return dataDir() + QStringLiteral("VoicePacks/");
}

QString downloadsDir()
{
    return dataDir() + QStringLiteral("downloads/");
}

QString instancesDir()
{
    return configDir() + QStringLiteral("instances/");
}

QString logsDir()
{
    return configDir() + QStringLiteral("logs/");
}

bool migrateLegacyIfNeeded(const QString& legacyBase, const QString& newBase)
{
#ifndef Q_OS_WIN
    // Non-Windows: the config path is UNCHANGED (~/.config/desktop-pet/) —
    // there is nothing to migrate by design.
    Q_UNUSED(legacyBase);
    Q_UNUSED(newBase);
    return false;
#else
    const QString legacy = legacyBase.isEmpty()
        ? QDir::homePath() + QStringLiteral("/.config/desktop-pet")
        : legacyBase;
    const QString target = newBase.isEmpty() ? configDir() : newBase;

    // No legacy tree → fresh install (or wiped profile): nothing to do.
    if (!QDir(legacy).exists()) {
        LOG_DEBUG("ConfigDir: no legacy dir at \"{}\" — migration skipped",
                  legacy.toStdString());
        return false;
    }

    // Target already populated → the app has run with the new layout before
    // (ensureDirectories scaffolds instances/ + logs/; DatabaseManager adds
    // app.db). Copying legacy state over newer files could clobber them.
    const QDir newDir(target);
    if (newDir.exists() &&
        !newDir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty()) {
        LOG_DEBUG("ConfigDir: target \"{}\" already populated — migration "
                  "skipped",
                  target.toStdString());
        return false;
    }

    if (!QDir().mkpath(target)) {
        LOG_WARN("ConfigDir: cannot create target dir \"{}\" — legacy "
                 "migration aborted (continuing with empty config)",
                 target.toStdString());
        return false;
    }

    // Recursive copy preserving relative paths. A file that fails to copy
    // logs WARN and the walk continues — a partial migration beats none
    // (config managers treat missing files as defaults, never errors).
    QDirIterator it(legacy,
                    QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        const QString rel = QDir(legacy).relativeFilePath(it.filePath());
        const QString dest = QDir(target).absoluteFilePath(rel);
        if (fi.isDir()) {
            QDir().mkpath(dest); // failure surfaces via the files below
        } else {
            QDir().mkpath(QFileInfo(dest).absolutePath());
            if (!QFile::copy(it.filePath(), dest)) {
                LOG_WARN("ConfigDir: legacy migration could not copy "
                         "\"{}\" — skipped",
                         rel.toStdString());
            }
        }
    }

    // Rename the legacy dir out of the way so this is one-time. If the
    // rename fails the copy has already succeeded — WARN and keep booting.
    const QString backup = legacy + QStringLiteral("-migrated-backup");
    if (!QDir().rename(legacy, backup)) {
        LOG_WARN("ConfigDir: legacy config copied to \"{}\" but the legacy "
                 "dir could not be renamed to \"{}\" (left in place)",
                 target.toStdString(), backup.toStdString());
    } else {
        LOG_INFO("ConfigDir: migrated legacy config \"{}\" -> \"{}\" "
                 "(backup at \"{}\")",
                 legacy.toStdString(), target.toStdString(),
                 backup.toStdString());
    }
    return true;
#endif
}

bool ensureDirectories(const QString& basePath)
{
    // basePath empty -> use the real config dir. Otherwise treat basePath as
    // the config-dir root. Normalize to a trailing '/' so "<root>instances"
    // joins cleanly whether the caller passed a path with or without one
    // (configDir() already ends in '/', QTemporaryDir::path() does not).
    QString root = basePath.isEmpty() ? configDir() : basePath;
    if (!root.endsWith(QLatin1Char('/')))
        root += QLatin1Char('/');

    // mkpath() creates all missing parents and is idempotent (returns true when
    // the path already exists), so the second call on an existing tree is a
    // genuine no-op that still returns true. Attempt all three regardless of
    // earlier failures so the WARN reports the full picture, then OR-reduce.
    QDir dir;
    const bool okRoot = dir.mkpath(root);
    const bool okInstances = dir.mkpath(root + QStringLiteral("instances"));
    const bool okLogs = dir.mkpath(root + QStringLiteral("logs"));

    const bool ok = okRoot && okInstances && okLogs;
    if (!ok) {
        // Graceful: do not throw. A read-only / unwritable location returns
        // false and logs a WARN so the caller (and the user, via the log file)
        // can react — e.g. Logging (T2) degrades to console-only, config
        // managers fall back to in-memory defaults.
        LOG_WARN("Failed to create config directory tree under \"{}\" "
                 "(root={}, instances={}, logs={})",
                 root.toStdString(), okRoot, okInstances, okLogs);
    }
    return ok;
}

} // namespace ConfigDir
