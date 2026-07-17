#include "core/HitAreaCacheManager.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <spdlog/spdlog.h>
#include "logging/Logging.hpp"
#include "core/ConfigDir.hpp"

// HitAreaCacheManager implementation — C++ port of Java's HitAreaCacheManager.
//
// File format: a JSON object whose keys are model short-names and values are
// arrays of hit-area name strings. Example:
//   { "Hiyori": ["Body"], "Haru": ["Head", "Body"] }
//
// Atomicity: save() uses QSaveFile (.tmp + commit/rename), matching
// InstanceConfigManager's discipline. A crash during the write leaves only the
// .tmp file; the real cache file — if any — is untouched.
//
// Robustness: load() returns an empty map on any error (missing file, corrupt
// JSON, non-object root, read failure). It NEVER throws — the caller always gets
// a usable (possibly empty) map. save() logs an ERROR on failure but does not
// throw (the cache is a perf optimization, not a correctness requirement).

HitAreaCacheManager::HitAreaCacheManager(const QString& basePath, QObject* parent)
    : QObject(parent)
    , m_basePath(basePath)
{
}

QMap<QString, QStringList> HitAreaCacheManager::load() const
{
    const QString path = resolveCachePath();
    QFile f(path);
    if (!f.exists()) {
        return {};
    }
    if (!f.open(QIODevice::ReadOnly)) {
        LOG_WARN("HitAreaCacheManager: cannot read \"{}\" (open failed)",
                 path.toStdString());
        return {};
    }

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) {
        LOG_WARN("HitAreaCacheManager: cache root is not a JSON object (\"{}\")",
                 path.toStdString());
        return {};
    }

    // Iterate the root object's keys. Each value MUST be an array of strings;
    // non-array values are skipped (mirrors the Java reference's isArray filter).
    // Non-string array elements are skipped (mirrors isJsonPrimitive + getAsString).
    QMap<QString, QStringList> result;
    const QJsonObject root = doc.object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        const QJsonValue val = it.value();
        if (!val.isArray()) {
            continue;
        }
        QStringList areas;
        const QJsonArray arr = val.toArray();
        for (const QJsonValue& item : arr) {
            if (item.isString()) {
                areas.append(item.toString());
            }
        }
        result.insert(it.key(), areas);
    }
    return result;
}

void HitAreaCacheManager::save(const QMap<QString, QStringList>& cache) const
{
    const QString path = resolveCachePath();

    // Ensure the parent directory exists (mkpath is idempotent). The cache file
    // lives at <configDir>/hit_area_cache.json; configDir should already exist
    // (ConfigDir::ensureDirectories runs at startup), but this guard is cheap
    // insurance against a missing dir on first write.
    const QDir parentDir = QFileInfo(path).dir();
    if (!parentDir.mkpath(QStringLiteral("."))) {
        LOG_ERROR("HitAreaCacheManager: cannot create directory \"{}\"",
                  parentDir.path().toStdString());
        return;
    }

    // Serialize: { "<model>": ["area1", "area2", ...], ... }
    QJsonObject root;
    for (auto it = cache.constBegin(); it != cache.constEnd(); ++it) {
        QJsonArray arr;
        for (const QString& area : it.value()) {
            arr.append(area);
        }
        root.insert(it.key(), arr);
    }
    const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Indented);

    // Atomic write: QSaveFile writes to <path>.tmp and atomically renames on
    // commit(). On Windows, QSaveFile uses MoveFileEx with REPLACE_EXISTING.
    // A crash before commit() leaves only the .tmp file; the real cache is
    // untouched (matches InstanceConfigManager's atomicWrite discipline).
    QSaveFile file(path);
    if (!file.open(QSaveFile::WriteOnly)) {
        LOG_ERROR("HitAreaCacheManager: cannot open \"{}\" for writing",
                  path.toStdString());
        return;
    }
    // T25 exception audit: check write() return value + flush() before
    // commit(). A short write (disk full, I/O error) would leave the .tmp
    // with partial data; commit() would succeed on the truncated file,
    // silently corrupting the cache. Matches InstanceConfigManager +
    // PanelStateManager atomicWrite discipline.
    const qint64 written = file.write(data);
    if (written != data.size() || !file.flush()) {
        LOG_ERROR("HitAreaCacheManager: short write to \"{}\" ({} of {} bytes)",
                  path.toStdString(), written, data.size());
        file.cancelWriting();
        return;
    }
    if (!file.commit()) {
        LOG_ERROR("HitAreaCacheManager: atomic write failed for \"{}\": {}",
                  path.toStdString(),
                  file.errorString().toStdString());
    }
}

std::optional<QStringList> HitAreaCacheManager::get(const QString& modelName) const
{
    const QMap<QString, QStringList> cache = load();
    const auto it = cache.constFind(modelName);
    if (it == cache.constEnd()) {
        return std::nullopt;
    }
    return it.value();
}

void HitAreaCacheManager::put(const QString& modelName, const QStringList& hitAreas)
{
    // Read-modify-write. Safe because InstanceSession (the sole caller) is
    // main-thread only — all event handlers marshal to the main thread via
    // Qt::QueuedConnection. No concurrent put() can interleave.
    QMap<QString, QStringList> cache = load();
    cache.insert(modelName, hitAreas);
    save(cache);
}

QString HitAreaCacheManager::resolveCachePath() const
{
    // Empty basePath → real ConfigDir::configDir(). Non-empty → <basePath>.
    // The cache file is always "<dir>/hit_area_cache.json" under whichever root
    // is in effect. QDir::cleanPath normalizes any trailing separator so the
    // join produces a clean absolute path regardless of basePath's form.
    const QString root = m_basePath.isEmpty()
        ? ConfigDir::configDir()
        : m_basePath;
    return QDir::cleanPath(root + QStringLiteral("/hit_area_cache.json"));
}
