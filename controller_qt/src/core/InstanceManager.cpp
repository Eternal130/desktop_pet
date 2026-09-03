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

QString InstanceManager::createInstance(const QString& label, const QString& avatar)
{
    // defaultInstanceConfig mints a fresh UUID as the persistence primary key.
    InstanceConfig cfg = defaultInstanceConfig();
    cfg.label = label;
    if (!avatar.isEmpty())
        cfg.avatar = avatar;

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
    const int row = rowForUuid(uuid);
    if (row < 0) {
        LOG_WARN("InstanceManager::deleteInstance: uuid=\"{}\" not in roster (no-op)",
                 uuid.toStdString());
        return;
    }

    InstanceSession* victim = m_sessions.at(row);
    beginRemoveRows(QModelIndex(), row, row);
    m_sessions.removeAt(row);
    endRemoveRows();
    delete victim; // synchronous teardown — the session is gone after this line

    // deleteInstance is idempotent (returns true if absent) — safe even if the
    // file was already removed out-of-band.
    m_configManager.deleteInstance(uuid);
    // Detach image-icon refs (never deletes image files — images are
    // referenced, not owned). No-op when the hook is not injected (tests).
    if (m_detachAssetRefs)
        m_detachAssetRefs(uuid);
    persistRoster();
    LOG_INFO("InstanceManager: deleted instance uuid=\"{}\" (row={})", uuid.toStdString(), row);
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

void InstanceManager::stopAll()
{
    // Wave 7 todo 15 — graceful shutdown of every instance on app exit.
    // InstanceSession::stop() is blocking (ProcessManager::stop runs the
    // 3-stage graceful shutdown: close-WS → send shutdown → wait-for-exit,
    // capped at 5s per instance). For an exit path with N instances this
    // blocks the GUI thread up to N×5s — acceptable because the user has
    // explicitly confirmed they want to quit.
    //
    // manuallyStopping is set inside stop() so onProcessExited treats the
    // teardown as user-initiated (no startFailed emission, no RestartController
    // re-arm). Sessions that are already stopped are a fast no-op (pm.stop
    // checks state() != NotRunning).
    const int count = m_sessions.size();
    for (InstanceSession* session : m_sessions) {
        session->stop();
    }
    LOG_INFO("InstanceManager::stopAll: stopped {} instance(s)", count);
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
