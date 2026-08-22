#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <optional>

#include "core/DatabaseManager.hpp"
#include "core/InstanceConfig.hpp"

// InstanceConfigManager — per-instance config CRUD. The backend moved from
// <instancesDir>/<uuid>.json files to SQLite (DatabaseManager,
// instance_configs table): one row per instance, json column holding the
// instanceConfigToJson blob. NO migration from the old JSON files — fresh
// start (user decision). The public API is unchanged so InstanceManager,
// InstanceSession and their tests keep compiling.
//
// basePath injection: tests pass a QTemporaryDir path (the db lands at
// <basePath>/app.db). If basePath is empty (the production default), the
// real ConfigDir::configDir() is used. main.cpp owns ONE DatabaseManager and
// passes it by reference via setDatabase(); when none is injected, the
// manager lazily opens its own connection — keeps the historical
// single-argument constructor working for tests.
class InstanceConfigManager : public QObject
{
    Q_OBJECT

public:
    explicit InstanceConfigManager(const QString& basePath = {},
                                   QObject* parent = nullptr);
    ~InstanceConfigManager() override;

    // Share main.cpp's DatabaseManager (preferred production path). Must be
    // called before the first save()/load(). When set, the manager never
    // opens its own connection.
    void setDatabase(DatabaseManager* db) { m_sharedDb = db; }

    // Upsert the instance row. Returns true on success, false on db failure.
    bool save(const InstanceConfig& cfg);

    // Load a single instance by uuid. Returns std::nullopt if absent or the
    // stored JSON is corrupt. NEVER throws.
    std::optional<InstanceConfig> load(const QString& id) const;

    // Delete an instance row. Idempotent — deleting an absent uuid succeeds.
    bool deleteInstance(const QString& id);

    // All instances, sorted by uuid (stable order for callers).
    QList<InstanceConfig> loadAll() const;

private:
    // Shared injected db, else the lazily opened owned one (null on failure).
    DatabaseManager* db() const;

    QString m_basePath;
    DatabaseManager* m_sharedDb = nullptr;
    mutable DatabaseManager* m_ownedDb = nullptr;
    mutable bool m_ownedOpenAttempted = false;
};
