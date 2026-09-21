#include "core/InstanceManager.hpp"

#include <QDir>
#include <QFile>
#include <QModelIndex>
#include <QVariant>
#include <QtAlgorithms>

#include <spdlog/spdlog.h>
#include "logging/Logging.hpp"
#include "core/InstanceConfig.hpp"
#include "core/InstanceSession.hpp"
#include "core/PanelStateManager.hpp"
#include "network/Envelope.hpp"
#include "network/PendingRequests.hpp"
#include "network/WsServer.hpp"

InstanceManager::InstanceManager(const QString& configDir, WsServer& server,
                                 PendingRequests& pending,
                                 std::function<void(const PanelConfig&)> savePanel,
                                 QObject* parent)
    : QAbstractListModel(parent)
    , m_configDir(configDir)
    , m_server(server)
    , m_pending(pending)
    , m_savePanel(std::move(savePanel))
    , m_configManager(configDir)
{
    // Bridge the model-change signals to countChanged so QML bindings on the
    // `count` property re-evaluate (badges, group-label visibility).
    connect(this, &QAbstractListModel::rowsInserted,
            this, &InstanceManager::countChanged);
    connect(this, &QAbstractListModel::rowsRemoved,
            this, &InstanceManager::countChanged);
    connect(this, &QAbstractListModel::modelReset,
            this, &InstanceManager::countChanged);
    loadFromDisk();
}

InstanceManager::~InstanceManager()
{
    // Owns every InstanceSession explicitly (parent=nullptr at construction) —
    // qDeleteAll frees them; no QObject-parent double-delete.
    qDeleteAll(m_sessions);
}

// ── QAbstractListModel overrides ─────────────────────────────────────────────

int InstanceManager::rowCount(const QModelIndex& parent) const
{
    // Flat list — a valid parent means we're being asked for children of a node,
    // which a flat list model never has.
    return parent.isValid() ? 0 : m_sessions.size();
}

QVariant InstanceManager::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
        return {};
    const int row = index.row();
    if (row < 0 || row >= m_sessions.size())
        return {};
    InstanceSession* session = m_sessions.at(row);
    switch (role) {
        case LabelRole:     return session->label();
        case ModelNameRole: return session->modelName();
        case StatusRole:    return session->status();
        case ConnectedRole: return session->connected();
        case UuidRole:      return session->config().id;
        case AvatarRole:    return session->avatar();
    }
    return {};
}

QHash<int, QByteArray> InstanceManager::roleNames() const
{
    return {
        {LabelRole,     "label"},
        {ModelNameRole, "modelName"},
        {StatusRole,    "status"},
        {ConnectedRole, "connected"},
        {UuidRole,      "uuid"},
        {AvatarRole,    "avatar"},
    };
}

// ── Roster operations ────────────────────────────────────────────────────────

QString InstanceManager::createInstance(const QString& label,
                                        const QString& avatar,
                                        const QString& modelName)
{
    // defaultInstanceConfig mints a fresh UUID as the persistence primary key.
    InstanceConfig cfg = defaultInstanceConfig();
    cfg.label = label;
    if (!avatar.isEmpty())
        cfg.avatar = avatar;
    // Model-library selection: non-empty overrides the InstanceConfig default.
    // Empty → the default stays ("Hiyori" — matches the pre-parameter behavior).
    if (!modelName.isEmpty())
        cfg.modelName = modelName;

    if (!m_configManager.save(cfg)) {
        LOG_ERROR("InstanceManager::createInstance: failed to persist \"{}\"; aborting row insert",
                  cfg.id.toStdString());
        return {};
    }

    // parent=nullptr: InstanceManager owns the session explicitly (see header
    // ownership note). Setting parent=this would double-delete on teardown.
    // m_configDir threaded through as configBasePath so InstanceSession's owned
    // InstanceConfigManager + HitAreaCacheManager write under the same root
    // (tests inject a QTemporaryDir; production uses the real config dir).
    auto* session = new InstanceSession(cfg, m_server, m_pending, m_configDir, nullptr);
    wireSession(session);

    const int row = m_sessions.size();
    beginInsertRows(QModelIndex(), row, row);
    m_sessions.append(session);
    endInsertRows();

    persistRoster();
    LOG_INFO("InstanceManager: created instance uuid=\"{}\" label=\"{}\" (row={})",
             cfg.id.toStdString(), label.toStdString(), row);
    return cfg.id;
}

void InstanceManager::requestDelete(const QString& uuid)
{
    // Two-step delete: the UI wires deleteConfirmed → confirm dialog (default-
    // focus Cancel) → on user confirm, calls deleteInstance. We never touch the
    // roster here.
    emit deleteConfirmed(uuid);
}

void InstanceManager::deleteInstance(const QString& uuid)
{
    // Idempotent re-request while a two-phase delete is already pending:
    // MERGE (ignore) — the in-flight stop's single completion performs the
    // removal. Semantics chosen for simplicity: no second stop, no queued
    // duplicate removal, no signal spam.
    if (m_pendingDeletes.contains(uuid)) {
        LOG_WARN("InstanceManager::deleteInstance: uuid=\"{}\" already pending "
                 "delete (ignored)", uuid.toStdString());
        return;
    }

    const int row = rowForUuid(uuid);
    if (row < 0) {
        LOG_WARN("InstanceManager::deleteInstance: uuid=\"{}\" not in roster (no-op)",
                 uuid.toStdString());
        return;
    }

    InstanceSession* victim = m_sessions.at(row);

    // S4 two-phase delete: a session whose renderer still runs cannot be
    // destroyed synchronously without blocking the GUI — the old path did
    // `delete victim` immediately and relied on ~ProcessManager's
    // kill+waitForFinished(2s) to reap the child, stalling the GUI thread
    // and skipping the graceful 3-stage shutdown entirely.
    //   Phase 1 (here): mark the session delete-pending (rejects racing
    //   start()/restart()), record it in the pending set, initiate the ASYNC
    //   stop, return — the row stays visible until phase 2.
    //   Phase 2 (onSessionStopFinished → finishInstanceDeletion): real row
    //   removal + reap + config cleanup + persist.
    if (victim->isProcessRunning()) {
        victim->setDeletePending();
        m_pendingDeletes.insert(uuid, victim);
        LOG_INFO("InstanceManager: deleteInstance uuid=\"{}\" — renderer running; "
                 "deferring removal until the async stop completes",
                 uuid.toStdString());
        victim->stop();
        return;
    }

    // No live process — immediate removal, exactly the pre-S4 behavior.
    finishInstanceDeletion(uuid);
}

void InstanceManager::finishInstanceDeletion(const QString& uuid)
{
    const int row = rowForUuid(uuid);
    if (row < 0) {
        LOG_WARN("InstanceManager::finishInstanceDeletion: uuid=\"{}\" no longer "
                 "in roster (no-op)", uuid.toStdString());
        return;
    }
    InstanceSession* victim = m_sessions.at(row);

    // rowsAboutToBeRemoved fires HERE — at the REAL removal. The detail page
    // (and any QML view over this model) holds InstanceSession* pointers
    // whose safety depends on this ordering.
    beginRemoveRows(QModelIndex(), row, row);
    m_sessions.removeAt(row);
    endRemoveRows();

    // deleteLater, NOT delete: the deferred (phase-2) branch runs inside the
    // session's stopFinished emission — destroying the sender synchronously
    // from a direct-connected slot is UB. The row is already gone from the
    // model so no external pointer can reach the zombie; the event loop
    // reaps it as soon as the current call stack unwinds. The immediate
    // (already-stopped) path shares this helper for uniformity.
    victim->deleteLater();

    // deleteInstance is idempotent (returns true if absent) — safe even if the
    // file was already removed out-of-band.
    m_configManager.deleteInstance(uuid);
    // Detach image-icon refs (never deletes image files — images are
    // referenced, not owned). No-op when the hook is not injected (tests).
    if (m_detachAssetRefs)
        m_detachAssetRefs(uuid);
    persistRoster();
    LOG_INFO("InstanceManager: deleted instance uuid=\"{}\" (row={})",
             uuid.toStdString(), row);
}

void InstanceManager::onSessionStopFinished(InstanceSession* session)
{
    // The session may be deleteLater'd inside this call — capture the uuid
    // (value copy) up front and NEVER dereference `session` after
    // finishInstanceDeletion returns.
    const QString uuid = session->config().id;

    // Phase 2 of a two-phase delete: the async stop initiated by
    // deleteInstance finished → perform the REAL removal now. Plain user
    // stops (no pending entry) fall through to the stopAll accounting.
    if (m_pendingDeletes.remove(uuid) > 0)
        finishInstanceDeletion(uuid);

    // stopAll-wave accounting: emit once no session still has a live
    // renderer. Recomputed from the live roster (not a counter) so double
    // stopAll() calls or delete-pending sessions can never wedge the wave.
    // finishInstanceDeletion already removed any doomed row from m_sessions,
    // so this loop never touches the zombie.
    if (m_stopAllActive) {
        for (InstanceSession* s : m_sessions) {
            if (s->isProcessRunning())
                return; // at least one renderer still winding down
        }
        m_stopAllActive = false;
        LOG_INFO("InstanceManager: stopAll complete — every renderer is down");
        emit stopAllFinished();
    }
}

// ── Access / routing ─────────────────────────────────────────────────────────

InstanceSession* InstanceManager::instanceAt(int row) const
{
    if (row < 0 || row >= m_sessions.size())
        return nullptr;
    return m_sessions.at(row);
}

void InstanceManager::route(int instanceId, const Envelope& env)
{
    // M2 demux: linear scan by instanceId (the roster is small — sidebar-sized).
    // Todo 11 swaps this for an int→session map when WsServer routes many
    // concurrent renderer connections; for Phase 5 the linear scan is fine.
    for (InstanceSession* session : m_sessions) {
        if (session->instanceId() == instanceId) {
            session->onMessage(env);
            return;
        }
    }
    LOG_WARN("InstanceManager::route: no session for instanceId={} (action=\"{}\") — dropped",
             instanceId, env.action.toStdString());
}

int InstanceManager::stopAll()
{
    // S4 (event-driven rework — was N×5s of blocking GUI): initiate the
    // async graceful stop for every session whose renderer runs and return
    // how many are winding down. Sessions already stopping (user-initiated)
    // are counted too — their single stopFinished both completes their stop
    // and advances this wave. stopAllFinished fires from
    // onSessionStopFinished once no session has a live renderer anymore; the
    // exit path (Main.qml doExit) quits in that handler instead of blocking
    // here. manullyStopping is set inside each stop() so onProcessExited
    // treats the teardown as user-initiated (no startFailed, no
    // RestartController re-arm).
    int initiated = 0;
    for (InstanceSession* session : m_sessions) {
        if (session->isProcessRunning()) {
            session->stop();
            ++initiated;
        }
    }
    m_stopAllActive = (initiated > 0);
    LOG_INFO("InstanceManager::stopAll: initiated async stop for {} instance(s)",
             initiated);
    return initiated;
}

void InstanceManager::setDatabase(DatabaseManager* db)
{
    // The constructor already ran loadFromDisk() once (against the manager's
    // lazily-opened own db or an empty backend). Reload here against the
    // shared db WITHOUT duplicating: drop the roster first, then load fresh.
    if (!m_sessions.isEmpty()) {
        beginResetModel();
        qDeleteAll(m_sessions);
        m_sessions.clear();
        endResetModel();
        // S4: the destroyed sessions can no longer complete their pending
        // async stops — drop the two-phase / stopAll bookkeeping with them.
        m_pendingDeletes.clear();
        m_stopAllActive = false;
    }
    m_configManager.setDatabase(db);
    m_db = db;
    loadFromDisk();
}

// ── Private helpers ──────────────────────────────────────────────────────────

void InstanceManager::setDialogueSink(std::function<void(const QString&,
                                                         const QString&,
                                                         const QString&,
                                                         const QString&,
                                                         int)> sink)
{
    m_dialogueSink = std::move(sink);
    // Re-apply to existing sessions: main.cpp installs the sink after
    // setDatabase->loadFromDisk created the roster, so the construction-time
    // wireSession() copies were empty.
    for (InstanceSession* session : m_sessions)
        wireSession(session);
}
void InstanceManager::wireSession(InstanceSession* session)
{
    // Forward the manager-level dialogue sink so every session (whenever it
    // was constructed) reaches the shared bubble stream. std::function copies
    // are cheap handles; empty ones no-op inside the session.
    if (m_dialogueSink)
        session->setDialogueSink(m_dialogueSink);

    // S4: route every session's stop completion into the manager's two-phase
    // delete + stopAll accounting. Direct connection on the GUI thread — the
    // deferred delete inside uses deleteLater precisely because of that.
    // wireSession only ever runs on freshly constructed sessions, so this
    // can never double-connect.
    connect(session, &InstanceSession::stopFinished, this,
            [this, session]() { onSessionStopFinished(session); });
}

void InstanceManager::persistRoster()
{
    // Rebuild instanceIds from the live roster order so deletes + inserts are
    // reflected even if m_panelConfig.instanceIds drifted (e.g. a corrupt file
    // skipped at load left a stale id — persistRoster drops it on the next
    // mutation, self-healing the roster).
    m_panelConfig.instanceIds.clear();
    m_panelConfig.instanceIds.reserve(m_sessions.size());
    for (InstanceSession* session : m_sessions)
        m_panelConfig.instanceIds.append(session->config().id);
    if (m_savePanel)
        m_savePanel(m_panelConfig);
}

int InstanceManager::rowForUuid(const QString& uuid) const
{
    for (int i = 0; i < m_sessions.size(); ++i) {
        if (m_sessions.at(i)->config().id == uuid)
            return i;
    }
    return -1;
}

void InstanceManager::loadFromDisk()
{
    // Load the panel config from the SQLite kv store (panel_config key
    // "panel"); missing/corrupt → defaults. When no shared DatabaseManager
    // is injected, PanelStateManager opens its own connection at
    // <m_configDir>/app.db — same backend, same semantics.
    PanelStateManager psm(m_configDir);
    if (m_db != nullptr)
        psm.setDatabase(m_db);
    m_panelConfig = psm.load();

    // m4 fix (CRITICAL): iterate instanceIds IN ORDER — NOT loadAll() (which
    // returns instances sorted by id alphabetically). Sidebar order is the
    // user's creation order, preserved in PanelConfig.instanceIds across
    // restarts; loadAll's sort would scramble it.
    //
    // Corrupt/missing files degrade to a WARN + skip (never crash). The stale
    // id stays in m_panelConfig.instanceIds until the next persistRoster()
    // rebuilds it from the live roster — so a later create/delete self-heals.
    const QStringList ids = m_panelConfig.instanceIds; // local copy avoids detach
    for (const QString& id : ids) {
        const auto cfg = m_configManager.load(id);
        if (!cfg.has_value()) {
            LOG_WARN("InstanceManager: skipping corrupt/missing instance \"{}\" during load",
                     id.toStdString());
            continue;
        }
        m_sessions.append(new InstanceSession(*cfg, m_server, m_pending, m_configDir, nullptr));
        wireSession(m_sessions.last());
    }
    LOG_INFO("InstanceManager: loaded {} instance(s) from \"{}\" (instanceIds had {})",
             m_sessions.size(), m_configDir.toStdString(), ids.size());
}
