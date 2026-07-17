#pragma once

#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>

#include <optional>

// HitAreaCacheManager (Phase 5 todo 10) — C++ port of Java's
// `controller/.../core/HitAreaCacheManager.java` (100 LOC). A JSON cache at
// `<configDir>/hit_area_cache.json` mapping modelName → QStringList hitAreas,
// so the controller can avoid re-parsing the .model3.json on every model switch.
//
// Cache file format (JSON object, keys are model short-names, values are arrays
// of hit-area name strings):
//   {
//     "Hiyori": ["Body"],
//     "Haru": ["Head", "Body"],
//     ...
//   }
//
// API:
//   load()                            read the cache file → QMap (empty on
//                                     missing/corrupt; NEVER throws)
//   save(cache)                       atomic write (.tmp + rename via QSaveFile)
//   get(modelName)                    load() + lookup → std::optional<QStringList>
//                                     (std::nullopt if the model is not cached)
//   put(modelName, hitAreas)          load() + insert + save() (read-modify-write)
//
// Robustness: corrupt/missing/unreadable files degrade to an empty map with a
// WARN log — NEVER throws. This mirrors the Java reference's try/catch +
// LinkedHashMap fallback, and matches the InstanceConfigManager atomic-write
// discipline. Write failures log an ERROR but do not throw.
//
// basePath injection: tests pass a QTemporaryDir path so they never touch the
// user's real ~/.config/desktop-pet/. If basePath is empty (the production
// default), the real ConfigDir::configDir() is used. The cache file lives at
// <basePath>/hit_area_cache.json (or <configDir>/hit_area_cache.json).
//
// Standalone for todo 10 (tested independently via HitAreaCacheManagerTest).
// InstanceSession owns a HitAreaCacheManager member and consults it on
// model_loaded to reuse parsed hitAreas across model switches.
class HitAreaCacheManager : public QObject
{
    Q_OBJECT

public:
    // basePath: the config-dir root. For tests, inject a QTemporaryDir path.
    // If empty (the default), uses the real ConfigDir::configDir().
    explicit HitAreaCacheManager(const QString& basePath = {},
                                 QObject* parent = nullptr);
    ~HitAreaCacheManager() override = default;

    // Read the cache file → QMap<modelName, QStringList>. Returns an empty map
    // on missing file, corrupt JSON, non-object root, or any read error. Each
    // value entry must be an array of strings; non-string elements are skipped
    // (mirrors the Java reference's isJsonPrimitive + getAsString filter).
    // NEVER throws.
    QMap<QString, QStringList> load() const;

    // Atomically write the full cache map to the cache file. Creates the parent
    // directory if it does not exist. Uses QSaveFile for the .tmp + rename
    // atomic-write guarantee (matches InstanceConfigManager's discipline). Logs
    // an ERROR on write failure but does NOT throw.
    void save(const QMap<QString, QStringList>& cache) const;

    // Convenience: load() + lookup by modelName. Returns std::nullopt when the
    // model is absent from the cache (first load, or cache was cleared).
    std::optional<QStringList> get(const QString& modelName) const;

    // Convenience: load() + insert(modelName, hitAreas) + save(). A read-
    // modify-write cycle — safe because InstanceSession is main-thread only
    // (all event handlers marshal to the main thread via Qt::QueuedConnection).
    void put(const QString& modelName, const QStringList& hitAreas);

private:
    // Resolve the cache file path: <m_basePath>/hit_area_cache.json, or
    // ConfigDir::configDir() + "/hit_area_cache.json" when m_basePath is empty.
    QString resolveCachePath() const;

    QString m_basePath;
};
