#include "core/ConfigDir.hpp"

#include <QDir>
#include <QString>

// spdlog MUST be included before logging/Logging.hpp — Logging.hpp references
// the SPDLOG_* macros but does NOT include spdlog headers itself (T5 finding
// in .omo/notepads/qt-controller-foundation/learnings.md).
#include <spdlog/spdlog.h>
#include "logging/Logging.hpp"

namespace ConfigDir {

namespace {

// The shared config root relative to the user's home directory. IDENTICAL on
// Linux and Windows (architecture-blueprint.md §4.5.1) — byte-for-byte
// compatible with the legacy config format. Forward slashes are deliberate — Qt
// accepts and normalizes them on both platforms, so configDir() always returns
// a '/'-separated string.
constexpr const char* kConfigSubpath = "/.config/desktop-pet/";

} // namespace

QString configDir()
{
    return QDir::homePath() + QString::fromLatin1(kConfigSubpath);
}

QString instancesDir()
{
    return configDir() + QStringLiteral("instances/");
}

QString logsDir()
{
    return configDir() + QStringLiteral("logs/");
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
