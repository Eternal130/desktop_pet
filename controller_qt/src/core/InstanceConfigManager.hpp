#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <optional>

#include "core/InstanceConfig.hpp"

// InstanceConfigManager (task T20) — per-instance JSON CRUD on top of the T18
// serde model and the T17 directory layout. Each InstanceConfig is persisted as
// a single JSON file at <instancesDir>/<id>.json (configuration.md §5), where
// <instancesDir> is ConfigDir::instancesDir() = <home>/.config/desktop-pet/
// instances/. The Qt controller and the JavaFX controller read/write the SAME
// files (snake_case keys, identical byte layout) so the two are interoperable.
//
// Atomicity: every save() writes to <id>.json.tmp first and then renames it
// onto <id>.json. A crash during the write leaves only the .tmp file; the real
// <id>.json — if any — is untouched. The "atomic write guarantee" test
// exercises this directly by planting a stale .tmp and asserting the real
// file's contents are unchanged.
//
// Robustness: loadAll() iterates <instancesDir>/*.json and skips any file that
// fails to parse with a WARN log (does not crash the whole enumeration — one
// corrupt file must not nuke the instance list). load(id) returns std::nullopt
// for a missing or unparseable file. deleteInstance is idempotent — removing a
// non-existent file is success (mirrors QFile::remove semantics).
//
// basePath injection: tests pass a QTemporaryDir path so they never touch the
// user's real ~/.config/desktop-pet/. If basePath is empty (the production
// default), the real ConfigDir::instancesDir() is used. This mirrors the T17
// ensureDirectories(basePath) injection pattern.
class InstanceConfigManager : public QObject
{
    Q_OBJECT

public:
    // basePath: the config-dir root (e.g. ~/.config/desktop-pet/). For tests,
    // inject a QTemporaryDir path. If empty (the default), uses the real
    // ConfigDir::instancesDir().
    explicit InstanceConfigManager(const QString& basePath = {},
                                  QObject* parent = nullptr);

    // Save instance config to <instancesDir>/<id>.json (atomic: .tmp + rename).
    // Returns true on success, false on write/rename failure (caller should
    // surface the error to the user — the file is left untouched on failure).
    bool save(const InstanceConfig& cfg);

    // Load a single instance by id. Returns std::nullopt if the file is absent
    // or the JSON fails to parse (corrupt). NEVER throws.
    std::optional<InstanceConfig> load(const QString& id) const;

    // Delete an instance config file. Returns true if deleted OR already
    // absent (idempotent). Returns false only on a real I/O error (e.g. the
    // file exists but cannot be removed — permissions).
    bool deleteInstance(const QString& id);

    // Load all instances from <instancesDir>/*.json. Corrupt files are skipped
    // with a WARN log (one bad file must not crash the whole loadAll). Returns
    // the list sorted by id (alphabetical) so the caller gets a stable order.
    QList<InstanceConfig> loadAll() const;

private:
    // The config-dir root; instances live at <m_basePath>/instances/. Empty
    // means "use the real ConfigDir::instancesDir()" (production default).
    QString m_basePath;

    // Returns the effective instances directory. If m_basePath is empty, this
    // is ConfigDir::instancesDir(); otherwise it is <m_basePath>/instances/.
    // A trailing '/' is appended for clean concatenation regardless of whether
    // the caller's basePath ends with one (mirrors ConfigDir::ensureDirectories).
    QString instancesDir() const;

    // Returns the absolute path of the JSON file for `id`:
    // <instancesDir>/<id>.json. The id is used verbatim as the filename — no
    // sanitization, because ids are UUIDs (QUuid::toString) and never contain
    // path separators or other filesystem-hazardous characters.
    QString instanceFilePath(const QString& id) const;

    // Atomic write: write to <path>.tmp, then rename to <path>. Cleans up the
    // .tmp on any failure so no half-written .tmp lingers. Returns true on
    // success. See InstanceConfigManager.cpp for the Windows-target-overwrite
    // caveat (QFile::rename semantics differ between POSIX and Windows).
    bool atomicWrite(const QString& path, const QByteArray& data) const;
};
