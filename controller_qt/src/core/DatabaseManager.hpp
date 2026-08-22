#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QVariantMap>
#include <optional>

#include "core/InstanceConfig.hpp"

// DatabaseManager — thin wrapper over QSqlDatabase (QSQLITE driver) owning
// <configDir>/app.db. Replaces the JSON-file config backends (panel.json,
// instances/<uuid>.json) going forward; NO migration from those files (user
// decision: fresh start).
//
// Schema (created on open, idempotent):
//   panel_config     (key TEXT PRIMARY KEY, value TEXT)
//       — key-value store for the PanelConfig fields + logo_asset_id /
//         logo_sync_tray. PanelStateManager serializes the 12 PanelConfig
//         fields as one JSON blob under key "panel".
//   instance_configs (uuid TEXT PRIMARY KEY, json TEXT)
//       — one row per instance; json = instanceConfigToJson output.
//   assets           (id INTEGER PRIMARY KEY AUTOINCREMENT, sha256 TEXT
//                     UNIQUE, original_name TEXT, width INTEGER,
//                     height INTEGER, size_bytes INTEGER, uploaded_at TEXT)
//   asset_refs       (asset_id INTEGER REFERENCES assets(id), ref_type TEXT,
//                     ref_key TEXT, PRIMARY KEY(asset_id, ref_type, ref_key))
//       — ref_type ∈ {"logo","instance_icon"}; ref_key = "" for logo,
//         instance uuid for icons. Images are REFERENCED, not owned: deleting
//         an instance detaches its refs but never deletes image files.
//
// Contract: never throws. Every failure is logged (LOG_WARN/LOG_ERROR) and
// surfaced as a false return / empty optional / empty list. A failed open()
// makes every subsequent call a safe no-op returning failure values.
//
// Thread affinity: GUI thread only (QSqlDatabase connections are not
// thread-safe); all call sites are synchronous config I/O on the main thread.
class DatabaseManager : public QObject
{
    Q_OBJECT

public:
    explicit DatabaseManager(QObject* parent = nullptr);
    ~DatabaseManager() override;

    // Open (create if absent) the SQLite db at `path` and ensure the schema.
    // Returns false on driver/open/schema failure (logged). Re-opening with
    // a different path after a successful open is rejected (returns false).
    bool open(const QString& path);

    // True between a successful open() and destruction.
    bool isOpen() const { return m_open; }

    // ── panel_config key-value ────────────────────────────────────────────
    // Returns the stored value or `fallback` when absent / db closed.
    QString getValue(const QString& key, const QString& fallback = {}) const;
    // Insert-or-replace. Returns false on failure.
    bool setValue(const QString& key, const QString& value);

    // ── instance_configs ──────────────────────────────────────────────────
    bool saveInstance(const InstanceConfig& cfg);            // upsert by uuid
    std::optional<InstanceConfig> loadInstance(const QString& uuid) const;
    QList<InstanceConfig> loadAllInstances() const;          // sorted by uuid
    bool deleteInstance(const QString& uuid);                // idempotent

    // ── assets ────────────────────────────────────────────────────────────
    // Insert a new asset row. Returns the new id, or -1 on failure or when
    // `sha256` already exists (duplicate — caller treats -1 as reject).
    qint64 insertAsset(const QString& sha256, const QString& originalName,
                       int width, int height, qint64 sizeBytes,
                       const QString& uploadedAt);
    // Full asset row by id; empty map when absent.
    QVariantMap assetById(qint64 assetId) const;
    // Asset row by sha256; empty map when absent.
    QVariantMap assetBySha256(const QString& sha256) const;
    // All asset rows ordered by uploaded_at DESC (newest first).
    QList<QVariantMap> allAssets() const;
    // Delete the asset row (refs must be detached first — callers check
    // inUse()). Returns false on failure.
    bool deleteAssetRow(qint64 assetId);

    // ── asset_refs ────────────────────────────────────────────────────────
    // Attach (asset_id, refType, refKey). Idempotent (INSERT OR IGNORE).
    bool attachRef(qint64 assetId, const QString& refType, const QString& refKey);
    // Detach one ref. Idempotent.
    bool detachRef(qint64 assetId, const QString& refType, const QString& refKey);
    // Detach every ref of the given (refType, refKey) — e.g. all
    // instance_icon refs of a deleted instance, or the single logo ref.
    bool detachRefsOf(const QString& refType, const QString& refKey);
    // True iff at least one ref points at the asset.
    bool inUse(qint64 assetId) const;
    // Human-facing ref descriptors: "logo" or the instance uuid, one entry
    // per ref row.
    QStringList refsOf(qint64 assetId) const;

    // Raw statement execution for tests (planting corrupt rows etc.).
    // Returns false on failure. Not for production use — prefer the typed
    // helpers above.
    bool execRaw(const QString& sql) { return exec(sql, {}); }

private:
    // Runs `sql` (with bound `args`) ignoring its result; returns success.
    bool exec(const QString& sql, const QVariantList& args) const;
    // Runs `sql` and returns the first result row as a string list; empty
    // list on error / no rows.
    QStringList queryRow(const QString& sql, const QVariantList& args) const;

    bool m_open = false;
    QString m_connectionName; // unique per instance; QSqlDatabase registry key
};
