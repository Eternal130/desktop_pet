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
    //
    // Fix 2 (lost-update hardening): a FAILED query (db open failure, SQL
    // error) ALSO returns defaults, but arms the internal last-load-failed
    // flag — the next save() then refuses instead of persisting a
    // defaults-seeded blob over the (unread) on-disk state. A genuinely
    // MISSING row stays first-run semantics: defaults, save allowed.
    PanelConfig load();

    // Persist the PanelConfig to the panel_config kv row. Returns true on
    // success, false on db failure (logged). Refuses (false + WARN) when
    // the previous load() failed — see load()'s Fix 2 note.
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
    // Fix 2: set by load() on db-open / query failure, cleared by a
    // successful load(); save() refuses while armed.
    bool m_lastLoadFailed = false;
};
