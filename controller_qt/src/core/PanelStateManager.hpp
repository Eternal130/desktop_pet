#pragma once

#include <QObject>
#include <QString>

#include "core/DatabaseManager.hpp"
#include "core/PanelConfig.hpp"

// PanelStateManager — load/save the panel-level UI state. The backend moved
// from panel.json to SQLite (DatabaseManager, <configDir>/app.db): the 12
// PanelConfig fields are serialized as one JSON blob under panel_config key
// "panel". NO migration from the old JSON files — fresh start (user
// decision). The public API is unchanged so InstanceManager,
// PanelConfigController, WindowStateSaver and their call sites keep
// compiling.
//
// basePath injection: tests pass a QTemporaryDir path (the db lands at
// <basePath>/app.db). If basePath is empty (the production default), the
// real ConfigDir::configDir() is used. main.cpp owns ONE DatabaseManager and
// passes it by reference via setDatabase(); when no DatabaseManager is
// injected, the manager lazily opens its own connection at the basePath db —
// this keeps the historical single-argument constructor working for tests.
class PanelStateManager : public QObject
{
    Q_OBJECT

public:
    explicit PanelStateManager(const QString& basePath = {},
                               QObject* parent = nullptr);
    ~PanelStateManager() override;

    // Share main.cpp's DatabaseManager (preferred production path). Must be
    // called before the first load()/save(). When set, the manager never
    // opens its own connection.
    void setDatabase(DatabaseManager* db) { m_sharedDb = db; }

    // Load the persisted PanelConfig. Empty db / corrupt row → defaults
    // (which are NOT written back — the next save() persists them). NEVER
    // throws.
    PanelConfig load();

    // Persist the PanelConfig to the panel_config kv row. Returns true on
    // success, false on db failure (logged).
    bool save(const PanelConfig& cfg);

private:
    // Returns the shared DatabaseManager when injected, otherwise the lazily
    // opened owned one. Null when the owned open failed.
    DatabaseManager* db() const;

    QString m_basePath;
    DatabaseManager* m_sharedDb = nullptr;
    // Owned fallback connection (tests / non-injected production path).
    mutable DatabaseManager* m_ownedDb = nullptr;
    mutable bool m_ownedOpenAttempted = false;
};
