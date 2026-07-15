#include "core/InstanceConfigManager.hpp"

#include <algorithm>  // std::sort
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QList>
#include <QSaveFile>
#include <QStringList>

// spdlog MUST be included before logging/Logging.hpp — Logging.hpp references
// the SPDLOG_* macros but does NOT include spdlog headers itself (T5 finding
// in .omo/notepads/qt-controller-foundation/learnings.md).
#include <spdlog/spdlog.h>
#include "core/ConfigDir.hpp"
#include "logging/Logging.hpp"

InstanceConfigManager::InstanceConfigManager(const QString& basePath, QObject* parent)
    : QObject(parent), m_basePath(basePath)
{
}

bool InstanceConfigManager::save(const InstanceConfig& cfg)
{
    // Serialize via T18 serde (snake_case keys, 28 fields) and write atomically
    // so a crash mid-write cannot leave a half-written <id>.json. Compact form
    // keeps the on-disk file small and matches the Java Gson default layout.
    const QJsonDocument doc(instanceConfigToJson(cfg));
    const QByteArray data = doc.toJson(QJsonDocument::Compact);

    // Ensure the instances/ directory exists before the atomic write — a fresh
    // install (or a temp-dir basePath in tests) has no instances/ yet.
    // mkpath is idempotent: a second call on an existing dir returns true.
    // Production callers usually pre-create the tree via ConfigDir::
    // ensureDirectories(); doing it here too means save() is self-contained
    // and a missing instances/ never silently fails the first write.
    const QString dir = instancesDir();
    if (!QDir().mkpath(dir)) {
        LOG_WARN("save: failed to create instances dir \"{}\"",
                 dir.toStdString());
        return false;
    }
    return atomicWrite(instanceFilePath(cfg.id), data);
}

std::optional<InstanceConfig> InstanceConfigManager::load(const QString& id) const
{
    const QString path = instanceFilePath(id);
    QFile f(path);
    if (!f.exists()) {
        // Missing file is not an error — caller may legitimately ask for an id
        // that has never been saved. nullopt distinguishes "absent" from
        // "present but unparseable" without throwing.
        return std::nullopt;
    }
    if (!f.open(QIODevice::ReadOnly)) {
        // Present but unreadable (permissions, lock). Treat as unloadable.
        LOG_WARN("Failed to open instance file \"{}\" for reading",
                 path.toStdString());
        return std::nullopt;
    }
    const QByteArray raw = f.readAll();
    f.close();

    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        // Corrupt JSON — return nullopt, do not throw. loadAll() depends on
        // this to skip-and-continue instead of nuking the whole enumeration.
        LOG_WARN("Corrupt instance file \"{}\" ({}); skipping",
                 path.toStdString(),
                 parseErr.errorString().toStdString());
        return std::nullopt;
    }

    // instanceConfigFromJson never throws; it merges missing/wrong-typed fields
    // from the struct defaults (interface.md §1.5).
    return instanceConfigFromJson(doc.object());
}

bool InstanceConfigManager::deleteInstance(const QString& id)
{
    const QString path = instanceFilePath(id);
    // QFile::remove returns true if the file was removed OR was already absent
    // — that is exactly the idempotency the spec requires ("returns true if
    // deleted or already absent"). A real I/O error (e.g. permission) surfaces
    // as false so the caller can react.
    if (!QFile::exists(path))
        return true;
    return QFile::remove(path);
}

QList<InstanceConfig> InstanceConfigManager::loadAll() const
{
    const QString dir = instancesDir();
    QList<InstanceConfig> out;

    // No instances/ dir yet — fresh install. Empty list, no error.
    if (!QDir(dir).exists())
        return out;

    // nameFilters = ["*.json"] — exclude the .tmp artifacts from atomicWrite
    // (a stale .tmp must NEVER appear as a phantom instance). QDir::Files
    // excludes directories and symlinks-to-dirs; QDir::NoDotAndDotDot is
    // belt-and-suspenders (nameFilters already exclude "." and "..").
    const QStringList entries = QDir(dir).entryList(
        QStringList() << QStringLiteral("*.json"),
        QDir::Files | QDir::NoDotAndDotDot);

    for (const QString& name : entries) {
        // Derive the id from the filename by stripping ".json". This is the
        // inverse of instanceFilePath(): <id>.json → id. The id is what load()
        // keys on, so reusing load() here is the single source of truth.
        const QString id = QFileInfo(name).completeBaseName();
        auto opt = load(id);
        if (opt.has_value()) {
            out.append(*opt);
        } else {
            // load() already logged the WARN for the corrupt file. Continuing
            // here means one bad file does not block the rest — the contract
            // loadAll() makes with its callers.
            LOG_WARN("Skipping corrupt instance file: \"{}\"",
                     (dir + name).toStdString());
        }
    }

    // Sort by id (alphabetical) so the panel shows a stable order regardless
    // of filesystem enumeration order (NTFS/ext4/tmpfs differ). std::sort
    // isn't usable on QList<InstanceConfig> without a comparator lambda; using
    // std::sort directly via iterators is clearer than rolling a custom one.
    std::sort(out.begin(), out.end(),
              [](const InstanceConfig& a, const InstanceConfig& b) {
                  return a.id < b.id;
              });
    return out;
}

QString InstanceConfigManager::instancesDir() const
{
    // Empty basePath → real ConfigDir::instancesDir() (which already ends in
    // '/'). Non-empty basePath → treat as the config-dir root, normalize to a
    // trailing '/' so "<root>instances" joins cleanly (mirrors the T17
    // ensureDirectories normalization, see ConfigDir.cpp:46-49).
    if (m_basePath.isEmpty())
        return ConfigDir::instancesDir();
    QString root = m_basePath;
    if (!root.endsWith(QLatin1Char('/')))
        root += QLatin1Char('/');
    return root + QStringLiteral("instances/");
}

QString InstanceConfigManager::instanceFilePath(const QString& id) const
{
    // id is a UUID string (no path separators), used verbatim as the filename.
    // No escaping needed — see header doc.
    return instancesDir() + id + QStringLiteral(".json");
}

bool InstanceConfigManager::atomicWrite(const QString& path,
                                        const QByteArray& data) const
{
    // QSaveFile is Qt's purpose-built atomic-write primitive: it commits via
    // MoveFileEx(MOVEFILE_REPLACE_EXISTING) on Windows and rename(2) on POSIX,
    // so the target is overwritten atomically even when it already exists (a
    // property plain QFile::rename lacks on Windows — see Qt docs). abort()
    // cleans up the internal .tmp so a failed write leaves nothing behind.
    //
    // This is a strict superset of the "write .tmp then QFile::rename" pattern
    // in the task spec: same .tmp-then-rename shape, just with the Windows
    // overwrite case actually working.
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        LOG_WARN("atomicWrite: failed to open \"{}\" for writing",
                 path.toStdString());
        return false;
    }
    const qint64 written = f.write(data);
    if (written != data.size() || !f.flush()) {
        LOG_WARN("atomicWrite: short write to \"{}\" ({} of {} bytes)",
                 path.toStdString(), written, data.size());
        f.cancelWriting();  // discards the staged .tmp
        return false;
    }
    if (!f.commit()) {
        // commit() already cleaned up the .tmp on failure; just report.
        LOG_WARN("atomicWrite: commit failed for \"{}\"",
                 path.toStdString());
        return false;
    }
    return true;
}
