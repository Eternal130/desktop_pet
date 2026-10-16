#include "core/ConfigDir.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QStandardPaths>

#include <algorithm>

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
// QStandardPaths can hand back native separators on Windows (known to leak
// backslashes for some locations/environments). ConfigDir's documented
// contract is '/'-separated output on every platform, so normalize here —
// every accessor derives from these two roots.
QString withTrailingSlash(const QString& path)
{
    QString p = path;
    p.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return p.endsWith(QLatin1Char('/')) ? p : p + QLatin1Char('/');
}

} // namespace

QString configDir()
{
    // App name "desktop-pet" + empty org name: flat <root>/desktop-pet.
    //   Linux   AppConfigLocation -> ~/.config/desktop-pet  (byte-identical
    //                                  to the legacy homePath()+"/.config/
    //                                  desktop-pet")
    //   Windows AppDataLocation  -> %APPDATA%\desktop-pet    (Roaming)
    // Platform split is REQUIRED: Qt's Windows implementation maps
    // AppConfigLocation onto the SAME Local root as AppLocalDataLocation
    // (verified on the CI runner: writableLocation(AppConfigLocation) ==
    // writableLocation(AppLocalDataLocation)), which would put config into
    // Local instead of the designed Roaming location. AppDataLocation is
    // the genuine %APPDATA% (Roaming) location on Windows and is unused
    // for data (that is AppLocalDataLocation, %LOCALAPPDATA%).
#ifdef Q_OS_WIN
    return withTrailingSlash(
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
#else
    return withTrailingSlash(
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
#endif
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

QString pluginsDir()
{
    // User drop-in target (stage 2) — deliberately under dataDir (machine-
    // local, user-writable), NOT configDir: a plugin binary is not config
    // state. See the header note distinguishing this from
    // IPluginContext::pluginConfigDir().
    return dataDir() + QStringLiteral("plugins/");
}

QString pluginDataRoot()
{
    // v1.13 resource isolation (rev2 §2): per-plugin DATA domains. Mixed
    // case "PluginData" — see the header's three-way distinction note
    // (binary dir plugins/ vs SDK config plugins/<id>/ vs this data root).
    return dataDir() + QStringLiteral("PluginData/");
}

QString pluginModelsDir(const QString& pluginId)
{
    // Empty = host domain (caller error); charset-invalid = traversal
    // attempt (e.g. "a.b\..\.." on Windows). Both return empty — the id
    // is never concatenated into a path.
    if (!isValidPluginDataOwnerId(pluginId)) {
        if (!pluginId.isEmpty()) {
            LOG_WARN("ConfigDir: rejected invalid plugin id \"{}\" for "
                     "pluginModelsDir (charset [A-Za-z0-9.-])",
                     pluginId.toStdString());
        }
        return {};
    }
    return pluginDataRoot() + pluginId + QStringLiteral("/Models/");
}

QString pluginVoicePacksDir(const QString& pluginId)
{
    // Same gate as pluginModelsDir — see that accessor's comment.
    if (!isValidPluginDataOwnerId(pluginId)) {
        if (!pluginId.isEmpty()) {
            LOG_WARN("ConfigDir: rejected invalid plugin id \"{}\" for "
                     "pluginVoicePacksDir (charset [A-Za-z0-9.-])",
                     pluginId.toStdString());
        }
        return {};
    }
    return pluginDataRoot() + pluginId + QStringLiteral("/VoicePacks/");
}

bool isValidPluginDataOwnerId(const QString& id)
{
    // v1.13 hardening (oracle follow-up Should#1): the ONE charset gate
    // shared by manifest ids (§B.6) and PluginData domain owners. Dot-only
    // ids are rejected explicitly — '.' and '..' pass a naive charset test
    // yet are the canonical path-traversal names.
    if (id.isEmpty() || id == QLatin1String(".") || id == QLatin1String(".."))
        return false;
    for (const QChar& c : id) {
        const char16_t u = c.unicode();
        const bool allowed = (u >= u'a' && u <= u'z')
            || (u >= u'A' && u <= u'Z')
            || (u >= u'0' && u <= u'9')
            || u == u'.' || u == u'-';
        if (!allowed)
            return false;
    }
    return true;
}

QStringList pluginDataOwners(const QString& root)
{
    // §3.2c: PURE directory traversal — never the plugin registry. Missing
    // root → empty list; hidden dirs skipped (same discipline as the
    // scanners); case-insensitive sort for deterministic UI/ordering.
    // v1.13 hardening: entries outside the documented id charset are
    // skipped with a WARN — a hostile directory name (spaces, separators,
    // traversal fragments) can never surface as an owner id.
    // Test seam: an injected root replaces pluginDataRoot() (the
    // ensureDirectories basePath discipline) so the filter is testable
    // without touching the user's real data dir.
    const QString effectiveRoot = root.isEmpty() ? pluginDataRoot() : root;
    const QDir dir(effectiveRoot);
    if (!dir.exists())
        return {};
    QStringList owners = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    owners.erase(std::remove_if(owners.begin(), owners.end(),
                                [](const QString& name) {
                                    if (name.startsWith(QLatin1Char('.')))
                                        return true; // hidden (unchanged rule)
                                    if (!isValidPluginDataOwnerId(name)) {
                                        LOG_WARN("ConfigDir: skipping "
                                                 "hostile plugin-data owner "
                                                 "dir \"{}\" (charset "
                                                 "[A-Za-z0-9.-])",
                                                 name.toStdString());
                                        return true;
                                    }
                                    return false;
                                }),
                 owners.end());
    std::sort(owners.begin(), owners.end(),
              [](const QString& a, const QString& b) {
                  return a.compare(b, Qt::CaseInsensitive) < 0;
              });
    return owners;
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
