#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <functional>

#include "core/DatabaseManager.hpp"
#include "core/InstanceConfigManager.hpp"
#include "core/PanelConfig.hpp"
#include "network/Envelope.hpp"

class InstanceSession;
class WsServer;
class PendingRequests;

// InstanceManager (Phase 5, todo 3) — QAbstractListModel backing the sidebar.
// Holds the QList<InstanceSession*> roster and is the SINGLE owner of those
// InstanceSession objects (created in createInstance, destroyed in deleteInstance
// + the destructor). Exposed to QML as the 'instanceManager' context property
// (wired in todo 7).
//
// Sidebar order = PanelConfig.instanceIds order (NOT InstanceConfigManager::
// loadAll's alphabetical sort — the m4 fix). Loaded on construction by iterating
// instanceIds IN ORDER and calling InstanceConfigManager::load(id) for each.
//
// Persistence (m5 fix): a std::function<void(const PanelConfig&)> savePanel
// callback is injected. After every roster mutation (create/delete) the updated
// PanelConfig is passed to savePanel. main.cpp wires this to
// PanelStateManager::save; tests inject a capturing lambda that writes to a
// temp dir. This decouples InstanceManager from PanelStateManager for
// testability.
//
// Router (M2): route(instanceId, env) demuxes inbound WsServer messages to the
// InstanceSession whose instanceId() matches. main.cpp connects
// WsServer::messageReceived → route; todo 11 generalizes multi-instance routing.
//
// Ownership: InstanceSession pointers are owned by THIS manager, NOT via a
// QObject parent. Sessions are constructed with parent=nullptr and deleted
// explicitly by the destructor + deleteInstance. This avoids the QObject-parent
// double-delete when the model and the sessions are torn down together.
// S4: deleteInstance is TWO-PHASE for running instances (async stop first,
// real removal in the stopFinished callback); the removal itself is
// beginRemoveRows → removeAt → endRemoveRows → deleteLater (deleteLater
// because the deferred branch runs inside the session's stopFinished
// emission — destroying the sender synchronously from a direct-connected
// slot is UB).
class InstanceManager : public QAbstractListModel {
    Q_OBJECT
    // Roster size for QML badges/labels. Method calls like rowCount() create
    // no binding dependency, so a nav badge bound to instanceManager.rowCount()
    // never re-evaluates after a delete; this property re-evaluates on every
    // rowsInserted/rowsRemoved/modelReset.
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    // Custom roles exposed to QML via roleNames(). Offset from Qt::UserRole so
    // they never collide with Qt's built-in roles (display, decoration, ...).
    // The first four mirror InstanceSession's Q_PROPERTYs; UuidRole exposes the
    // underlying config().id (the persistence primary key) so the sidebar /
    // delete flows can address an instance without holding the InstanceConfig.
    enum Roles {
        LabelRole     = Qt::UserRole + 1,
        ModelNameRole,
        StatusRole,
        ConnectedRole,
        UuidRole,
        AvatarRole,
    };
    Q_ENUM(Roles)

    // configDir: the config-dir root (e.g. ~/.config/desktop-pet/). Instances
    // live at <configDir>/instances/<uuid>.json; panel.json at <configDir>/
    // panel.json. For tests inject a QTemporaryDir path.
    // server + pending: shared by every InstanceSession (M2 — PendingRequests is
    // the global id→result table). The caller MUST guarantee they outlive this.
    // savePanel: invoked after every roster mutation with the updated PanelConfig
    // (m5 fix — decouples persistence from PanelStateManager). May be empty.
    InstanceManager(const QString& configDir, WsServer& server, PendingRequests& pending,
                    std::function<void(const PanelConfig&)> savePanel,
                    QObject* parent = nullptr);
    ~InstanceManager() override;

    // Disable copy/move — holds references + owns heap sessions. A QObject-derived
    // model is identity-based and is meant to be created once and parented.
    InstanceManager(const InstanceManager&) = delete;
    InstanceManager& operator=(const InstanceManager&) = delete;
    InstanceManager(InstanceManager&&) = delete;
    InstanceManager& operator=(InstanceManager&&) = delete;

    // ── QAbstractListModel overrides ─────────────────────────────────────────
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_sessions.size(); }

    // ── Roster operations ────────────────────────────────────────────────────
    // Create a new instance: generate a fresh InstanceConfig (with a random UUID
    // via defaultInstanceConfig), set its label, persist it, append a row,
    // rebuild instanceIds, call savePanel. Returns the new UUID, or an empty
    // string if the persist failed (no row is added on failure).
    //
    // modelName (模型库 B 档): when non-empty, overrides the InstanceConfig
    // default ("Hiyori") — the model-library page passes the user's choice
    // from the create dialog. Empty keeps the default (callers that predate
    // the parameter keep compiling + behaving identically).
    Q_INVOKABLE QString createInstance(const QString& label,
                                       const QString& avatar = QStringLiteral("🐱"),
                                       const QString& modelName = QString());

    // Delete-protection pattern (blueprint §4.2): requestDelete emits
    // deleteConfirmed(uuid); the UI (todo 7 Sidebar) wires that to a confirm
    // dialog (default-focus Cancel) and, on confirm, calls deleteInstance(uuid).
    // The two-step keeps an accidental click from nuking an instance.
    Q_INVOKABLE void requestDelete(const QString& uuid);

    // Remove + destroy the instance: drop the row, delete the InstanceSession,
    // delete the instance file, rebuild instanceIds, call savePanel. No-op
    // (WARN log) if the uuid is not in the roster — never crashes.
    //
    // S4 TWO-PHASE for a RUNNING renderer (event-driven; the old path did a
    // synchronous `delete victim` whose ~ProcessManager force-killed the
    // child after a ~2s GUI block, skipping the graceful shutdown):
    //   Phase 1 — mark the session delete-pending (rejects racing start()/
    //             restart()), put the uuid in the pending set, initiate the
    //             ASYNC stop, return. The row is still present.
    //   Phase 2 — the session's stopFinished callback performs the REAL
    //             removal (rowsAboutToBeRemoved fires only here — the detail
    //             page's InstanceSession* safety depends on that ordering).
    // Re-deleting a pending uuid merges (WARN + no-op). A stopped/errored
    // instance (no live process) takes the immediate path exactly as before.
    Q_INVOKABLE void deleteInstance(const QString& uuid);

    // ── Access / routing ─────────────────────────────────────────────────────
    // Q_INVOKABLE: Main.qml's selectInstance(row, uuid) calls this to bind the
    // detail page's `instance` property. Without it, QML sees `undefined`,
    // `undefined !== null` is true, and the page binds to undefined (empty state).
    Q_INVOKABLE InstanceSession* instanceAt(int row) const;

    // M2 router: find the session whose instanceId() == instanceId and forward
    // env to its onMessage. WARN log + no-op if no session matches (an envelope
    // for an unknown instance). Synchronous — onMessage dispatch is in-thread.
    void route(int instanceId, const Envelope& env);

    // Wave 7 todo 15 — graceful shutdown of every running instance. Called
    // from Main.qml's exit path (closeAction=="exit" → confirm dialog → Ok).
    // S4 EVENT-DRIVEN (was N×5s of blocking GUI): initiates the async
    // graceful stop for every session whose renderer runs and RETURNS how
    // many are winding down. stopAllFinished fires (from the per-session
    // stop-completion callback) once no session has a live renderer anymore;
    // the exit path quits in that handler instead of blocking here. Returns
    // 0 (and emits nothing) when nothing is running. No-op re-invocation
    // while a wave is active simply keeps the wave.
    Q_INVOKABLE int stopAll();

    // Asset ref detachment hook (AssetManager wiring): when set,
    // deleteInstance calls it with the deleted uuid so the instance's
    // image-icon refs are detached. Images are referenced, never owned —
    // this NEVER deletes image files. Injected by main.cpp; empty default
    // keeps tests decoupled from the asset system.
    void setAssetRefDetacher(std::function<void(const QString&)> detacher)
    { m_detachAssetRefs = std::move(detacher); }

    // Dialogue sink seam (bubble-stream phase): when set, sessions forward
    // voice-pack dialogue lines here as (instanceId, name, avatar, text).
    // main.cpp wires it to the notification stream; empty default = no-op.
    // Stores the seam AND applies it to already-created sessions — main.cpp
    // sets this AFTER setDatabase→loadFromDisk created the initial roster,
    // so a store-only setter would leave those sessions with an empty sink.
    void setDialogueSink(std::function<void(const QString& instanceId,
                                              const QString& name,
                                              const QString& avatar,
                                              const QString& text,
                                              int durationMs)> sink);

    // Share main.cpp's DatabaseManager (SQLite config backend). Forwards to
    // the owned InstanceConfigManager and replaces the construction-time
    // panel.json read with a panel_config kv read. Must be called before the
    // manager is used (main.cpp calls it right after construction, before
    // any roster mutation; the ctor-time loadFromDisk still works without it
    // via the managers' lazily-opened own connections).
    void setDatabase(DatabaseManager* db);

signals:
    // Emitted by requestDelete. The UI connects this to a confirm dialog.
    void deleteConfirmed(const QString& uuid);
    // countChanged: re-emitted from rowsInserted/rowsRemoved/modelReset so
    // QML bindings on the `count` property refresh without polling.
    void countChanged();
    // S4: emitted once a stopAll() wave completes — every session's renderer
    // is down (gracefully or force-killed). The exit path (Main.qml doExit)
    // quits in this handler.
    void stopAllFinished();

private:
    // Rebuild m_panelConfig.instanceIds from the live roster order and persist
    // it via m_savePanel. Called after every create/delete. Rebuilding from the
    // roster (rather than mutating instanceIds in place) drops any stale id left
    // behind by a corrupt file skipped at load — self-healing on next mutation.
    void persistRoster();

    // Inject the manager-level dialogue sink seam into a freshly constructed
    // session. Called from every session-creation site (createInstance /
    // loadFromDisk). Also wires the S4 stop-completion routing.
    void wireSession(InstanceSession* session);

    // S4: per-session stopFinished callback — phase 2 of a two-phase delete
    // (perform the REAL removal) plus stopAll-wave accounting (emit
    // stopAllFinished when the last renderer is down).
    void onSessionStopFinished(InstanceSession* session);

    // S4: the actual row removal + config cleanup + persistence, shared by
    // the immediate (already-stopped) delete path and the deferred phase-2
    // path. rowsAboutToBeRemoved fires here, at the real removal.
    void finishInstanceDeletion(const QString& uuid);

    // Locate the row whose InstanceSession config().id == uuid; -1 if not found.
    int rowForUuid(const QString& uuid) const;

    // Read <configDir>/panel.json into m_panelConfig (defaults if missing or
    // unparseable), then load each instance IN instanceIds ORDER (m4 fix — NOT
    // loadAll's alphabetical sort), skipping corrupt/missing files with a WARN.
    void loadFromDisk();

    QString m_configDir;
    WsServer& m_server;
    PendingRequests& m_pending;
    std::function<void(const PanelConfig&)> m_savePanel;
    std::function<void(const QString&)> m_detachAssetRefs;
    std::function<void(const QString&, const QString&,
                       const QString&, const QString&,
                       int)> m_dialogueSink;
    DatabaseManager* m_db = nullptr; // shared SQLite backend (main.cpp)
    InstanceConfigManager m_configManager; // member by value; parent=nullptr (no Qt parent)
    PanelConfig m_panelConfig;
    QList<InstanceSession*> m_sessions; // owned — deleted in dtor + deleteInstance

    // ── S4 two-phase delete / stopAll bookkeeping ──────────────────────────
    // uuid → session awaiting the completion of its phase-1 async stop; the
    // stopFinished callback performs the real removal. Entries are created
    // only by deleteInstance and consumed only by onSessionStopFinished (or
    // dropped wholesale when the roster is reset in setDatabase).
    QHash<QString, InstanceSession*> m_pendingDeletes;
    // True between stopAll() initiating ≥1 async stop and the resulting
    // stopAllFinished emission.
    bool m_stopAllActive = false;
};
