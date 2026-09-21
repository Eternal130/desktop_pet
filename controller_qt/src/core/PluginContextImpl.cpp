#include "core/PluginContextImpl.hpp"

#include <QDir>
#include <QFileInfo>

#include <spdlog/spdlog.h>

#include "core/DownloadService.hpp"
#include "core/InstanceManager.hpp"
#include "core/InstanceSession.hpp"
#include "core/PluginHost.hpp"
#include "core/PluginPageModel.hpp"
#include "logging/Logging.hpp"
#include "ui/NotificationStreamController.hpp"

namespace core {

// ── InstanceApiImpl ─────────────────────────────────────────────────────────

InstanceApiImpl::InstanceApiImpl(InstanceManager* instanceManager, QObject* parent)
    : QObject(parent), m_instanceManager(instanceManager)
{
    // Model signals fire on the GUI thread — the §B.3 thread contract is
    // inherited, not enforced, here.
    if (m_instanceManager != nullptr) {
        connect(m_instanceManager, &QAbstractItemModel::rowsInserted,
                this, &InstanceApiImpl::fanoutRosterChanged);
        connect(m_instanceManager, &QAbstractItemModel::rowsRemoved,
                this, &InstanceApiImpl::fanoutRosterChanged);
        connect(m_instanceManager, &QAbstractItemModel::dataChanged,
                this, &InstanceApiImpl::fanoutRosterChanged);
        connect(m_instanceManager, &QAbstractItemModel::modelReset,
                this, &InstanceApiImpl::fanoutRosterChanged);

        // v1.2 (S2): attach the property-NOTIFY wiring to every session.
        // Sessions already in the roster (loaded from disk at manager
        // construction, before this API existed) + every row inserted
        // later. Connections are sender-bound: sessions are deleteLater'd
        // by the manager, which severs them automatically.
        connect(m_instanceManager, &QAbstractItemModel::rowsInserted, this,
                [this](const QModelIndex&, int first, int last) {
                    for (int row = first; row <= last; ++row)
                        observeSession(m_instanceManager->instanceAt(row));
                });
        for (int row = 0; row < m_instanceManager->rowCount(); ++row)
            observeSession(m_instanceManager->instanceAt(row));
    }
}

QVector<pet::InstanceInfo> InstanceApiImpl::instances()
{
    QVector<pet::InstanceInfo> out;
    if (m_instanceManager == nullptr)
        return out;
    for (int row = 0; row < m_instanceManager->rowCount(); ++row) {
        const InstanceSession* s = m_instanceManager->instanceAt(row);
        if (s == nullptr)
            continue;
        pet::InstanceInfo info;
        info.uuid = s->uuid();
        info.label = s->label();
        info.modelName = s->modelName();
        info.status = s->status();
        info.connected = s->connected();
        out.append(info);
    }
    return out;
}

void InstanceApiImpl::subscribeRoster(pet::IRosterObserver* observer)
{
    if (observer == nullptr || m_observers.contains(observer))
        return;
    m_observers.append(observer);
}

void InstanceApiImpl::unsubscribeRoster(pet::IRosterObserver* observer)
{
    m_observers.removeAll(observer);
}

void InstanceApiImpl::subscribeInstances(pet::IInstanceObserver* observer)
{
    if (observer == nullptr || m_instanceObservers.contains(observer))
        return;
    m_instanceObservers.append(observer);
}

void InstanceApiImpl::unsubscribeInstances(pet::IInstanceObserver* observer)
{
    m_instanceObservers.removeAll(observer);
}

void InstanceApiImpl::fanoutRosterChanged()
{
    // Copy: an observer that unsubscribes inside the callback must not
    // invalidate the iteration. Plugin exceptions must not poison the
    // fanout either (§A.1 context-boundary discipline).
    const auto observers = m_observers;
    for (pet::IRosterObserver* observer : observers) {
        try {
            observer->rosterChanged();
        } catch (...) {
            LOG_ERROR("[plugin-api] roster observer threw in rosterChanged() — "
                      "isolated, fanout continues");
        }
    }
    // v1.2: the instance observers share the roster-membership semantics
    // (same source signals, same copy-isolation discipline).
    const auto instanceObservers = m_instanceObservers;
    for (pet::IInstanceObserver* observer : instanceObservers) {
        try {
            observer->rosterChanged();
        } catch (...) {
            LOG_ERROR("[plugin-api] instance observer threw in rosterChanged() — "
                      "isolated, fanout continues");
        }
    }
}

void InstanceApiImpl::observeSession(InstanceSession* session)
{
    if (session == nullptr)
        return;
    // Coarse-grained by design: EVERY observable property NOTIFY fans out a
    // full InstanceRuntime snapshot (v1.2 contract). Raw-pointer capture is
    // safe because the connection is destroyed with the sender — a
    // deleteLater'd session can never invoke this lambda afterwards.
    for (auto signal : { &InstanceSession::statusChanged,
                         &InstanceSession::connectedChanged,
                         &InstanceSession::modelLoadedChanged,
                         &InstanceSession::modelNameChanged,
                         &InstanceSession::opacityChanged,
                         &InstanceSession::targetFpsChanged,
                         &InstanceSession::volumeChanged,
                         &InstanceSession::mutedChanged }) {
        connect(session, signal, this, [this, session]() {
            fanoutInstanceState(session);
        });
    }
    // Renderer rejected a model load → per-instance failure callback.
    connect(session, &InstanceSession::modelLoadFailed, this,
            [this, session](const QString& error) {
                fanoutModelLoadFailed(session, error);
            });
}

pet::InstanceRuntime InstanceApiImpl::snapshotOf(const InstanceSession* session)
{
    // Full-copy POD snapshot of every field the v1.2 struct carries. The
    // mounted-pack id is the pack DIRECTORY NAME (the SDK-wide pack
    // identity, e.g. "pack_v1") — mountedVoicePack() returns the absolute
    // dir; empty dir → empty id. Note: pack mount/unmount has NO NOTIFY
    // signal on InstanceSession (S4 freeze), so a pure mount change does
    // not trigger a fanout by itself — the next property flip (status,
    // model, ...) carries the fresh value. Coarse-grained by contract.
    pet::InstanceRuntime rt;
    rt.uuid = session->uuid();
    rt.label = session->label();
    rt.modelName = session->modelName();
    rt.status = session->status();
    rt.mountedPackId = QFileInfo(session->mountedVoicePack()).fileName();
    rt.connected = session->connected();
    rt.modelLoaded = session->modelLoaded();
    rt.muted = session->muted();
    rt.opacity = session->opacity();
    rt.volume = session->volume();
    rt.targetFps = session->targetFps();
    rt.restartAttempts = session->restartAttempts();
    return rt;
}

void InstanceApiImpl::fanoutInstanceState(InstanceSession* session)
{
    if (session == nullptr || m_instanceObservers.isEmpty())
        return;
    const pet::InstanceRuntime snapshot = snapshotOf(session);
    // Copy + try/catch: same §A.1 discipline as fanoutRosterChanged.
    const auto observers = m_instanceObservers;
    for (pet::IInstanceObserver* observer : observers) {
        try {
            observer->instanceStateChanged(snapshot.uuid, snapshot);
        } catch (...) {
            LOG_ERROR("[plugin-api] instance observer threw in "
                      "instanceStateChanged() — isolated, fanout continues");
        }
    }
}

void InstanceApiImpl::fanoutModelLoadFailed(InstanceSession* session,
                                            const QString& error)
{
    if (session == nullptr || m_instanceObservers.isEmpty())
        return;
    const QString uuid = session->uuid(); // value copy — observers may mutate roster
    const auto observers = m_instanceObservers;
    for (pet::IInstanceObserver* observer : observers) {
        try {
            observer->modelLoadFailed(uuid, error);
        } catch (...) {
            LOG_ERROR("[plugin-api] instance observer threw in "
                      "modelLoadFailed() — isolated, fanout continues");
        }
    }
}

// ── UiApiImpl ───────────────────────────────────────────────────────────────

UiApiImpl::UiApiImpl(const QString& pluginId, PluginPageModel* pageModel,
                     NotificationStreamController* stream, QObject* parent)
    : QObject(parent),
      m_pluginId(pluginId),
      m_pageModel(pageModel),
      m_notificationStream(stream)
{
}

pet::PluginError UiApiImpl::registerPage(const pet::PageDescriptor& page)
{
    if (m_pageModel == nullptr) {
        LOG_ERROR("[plugin-api] registerPage from '{}' with no page model mounted",
                  m_pluginId.toStdString());
        return pet::PluginError::Generic;
    }
    pet::PageDescriptor effective = page;
    if (effective.qmlUrl.isEmpty()) {
        // Build-injected default: the registry entry's qrc:/ URL derived
        // from the plugin's qt_add_qml_module (§B.6 resource layout).
        // Looked up through the host's entry table.
        effective.qmlUrl = PluginHost::entryQmlUrlFor(m_pluginId);
        if (effective.qmlUrl.isEmpty()) {
            LOG_WARN("[plugin-api] registerPage from '{}' has no qmlUrl and no "
                     "build-injected entry URL — rejected",
                     m_pluginId.toStdString());
            return pet::PluginError::InvalidArgument;
        }
    }
    // P6a (E-2): the IUiApi contract promises InvalidArgument for any
    // non-qrc:/ qmlUrl — enforce it here so the error code matches the
    // frozen header text (the page model would reject it too, but with
    // the less diagnosable Generic).
    if (!effective.qmlUrl.startsWith(QStringLiteral("qrc:/"))) {
        LOG_WARN("[plugin-api] registerPage from '{}' with non-qrc qmlUrl "
                 "'{}' — rejected",
                 m_pluginId.toStdString(), effective.qmlUrl.toStdString());
        return pet::PluginError::InvalidArgument;
    }
    return m_pageModel->addPage(m_pluginId, effective)
               ? pet::PluginError::Ok
               : pet::PluginError::Generic;
}

void UiApiImpl::notifyBubble(const QString& text, int durationMs)
{
    if (m_notificationStream == nullptr) {
        LOG_WARN("[plugin-api] notifyBubble from '{}' with no notification stream",
                 m_pluginId.toStdString());
        return;
    }
    m_notificationStream->push(m_pluginId, QString(), text, durationMs);
}

// ── DownloadApiAdapter (P5) ─────────────────────────────────────────────────

DownloadApiAdapter::DownloadApiAdapter(QString pluginId, bool networkGranted,
                                       DownloadService* service, QObject* parent)
    : QObject(parent),
      m_pluginId(std::move(pluginId)),
      m_networkGranted(networkGranted),
      m_service(service)
{
}

pet::JobId DownloadApiAdapter::start(const pet::DownloadRequest& request,
                                     pet::DownloadListener* listener)
{
    if (!m_networkGranted || m_service == nullptr) {
        LOG_WARN("[plugin-api] downloadApi().start() from '{}' without the "
                 "'network' capability (§B.4 gate) — ERR_CAPABILITY",
                 m_pluginId.toStdString());
        if (listener != nullptr) {
            try {
                listener->onError(0, pet::PluginError::Capability,
                                  QStringLiteral("network capability not granted"));
            } catch (...) {
            }
        }
        return 0;
    }
    return m_service->start(request, listener);
}

void DownloadApiAdapter::cancel(pet::JobId job)
{
    if (!m_networkGranted || m_service == nullptr)
        return; // no jobs could ever exist for this plugin
    m_service->cancel(job);
}

pet::PluginError DownloadApiAdapter::installArchive(pet::JobId job,
                                                    const pet::InstallSpec& spec)
{
    if (!m_networkGranted || m_service == nullptr) {
        LOG_WARN("[plugin-api] downloadApi().installArchive() from '{}' without "
                 "the 'network' capability (§B.4 gate) — ERR_CAPABILITY",
                 m_pluginId.toStdString());
        return pet::PluginError::Capability;
    }
    return m_service->installArchive(job, spec);
}

// ── InstanceControlApiImpl (S2, v1.2) ───────────────────────────────────────

InstanceControlApiImpl::InstanceControlApiImpl(InstanceManager* instanceManager,
                                               QObject* parent)
    : QObject(parent), m_instanceManager(instanceManager)
{
}

InstanceSession* InstanceControlApiImpl::sessionForUuid(const QString& uuid) const
{
    // Linear scan over the manager's PUBLIC roster surface (rowCount +
    // instanceAt — the same access QML uses). Sidebar-sized N; no manager
    // internals are touched.
    if (m_instanceManager == nullptr)
        return nullptr;
    for (int row = 0; row < m_instanceManager->rowCount(); ++row) {
        InstanceSession* s = m_instanceManager->instanceAt(row);
        if (s != nullptr && s->uuid() == uuid)
            return s;
    }
    return nullptr;
}

pet::PluginError InstanceControlApiImpl::create(const pet::InstanceSpec& spec,
                                                QString* outUuid)
{
    if (outUuid != nullptr)
        outUuid->clear();
    if (m_instanceManager == nullptr)
        return pet::PluginError::Generic;
    if (spec.label.isEmpty()) {
        LOG_WARN("[plugin-api] instanceControlApi().create() with empty label "
                 "— ERR_INVALID_ARGUMENT");
        return pet::PluginError::InvalidArgument;
    }
    // Same creation path as the panel's own Add flow (persist → row →
    // savePanel). Empty return = persist failure.
    const QString uuid = m_instanceManager->createInstance(
        spec.label, spec.avatar, spec.modelName);
    if (uuid.isEmpty()) {
        LOG_ERROR("[plugin-api] instanceControlApi().create('{}') — persist "
                  "failed, no row added", spec.label.toStdString());
        return pet::PluginError::Generic;
    }
    if (outUuid != nullptr)
        *outUuid = uuid;
    // autoStart mirrors the detail page's toggle semantics: persist the
    // start-with-panel flag (applies at the next panel launch); the
    // instance is NOT launched synchronously here.
    if (spec.autoStart) {
        if (InstanceSession* s = sessionForUuid(uuid))
            s->setAutoStart(true);
    }
    LOG_INFO("[plugin-api] instanceControlApi().create('{}') → uuid=\"{}\"",
             spec.label.toStdString(), uuid.toStdString());
    return pet::PluginError::Ok;
}

pet::PluginError InstanceControlApiImpl::remove(const QString& uuid)
{
    if (sessionForUuid(uuid) == nullptr) {
        LOG_WARN("[plugin-api] instanceControlApi().remove('{}') — not in "
                 "roster, ERR_NOT_FOUND", uuid.toStdString());
        return pet::PluginError::NotFound;
    }
    // S4 semantics pass through untouched: a running renderer takes the
    // two-phase path (row survives until the async stop completes; the
    // roster/state observers report the eventual removal), a stopped
    // instance is removed immediately, and a remove already pending
    // merges inside the manager (idempotent — Ok is truthful: the removal
    // IS happening).
    m_instanceManager->deleteInstance(uuid);
    return pet::PluginError::Ok;
}

pet::PluginError InstanceControlApiImpl::start(const QString& uuid)
{
    InstanceSession* s = sessionForUuid(uuid);
    if (s == nullptr)
        return pet::PluginError::NotFound;
    if (s->isDeletePending()) {
        LOG_WARN("[plugin-api] instanceControlApi().start('{}') — pending "
                 "delete, ERR_BUSY", uuid.toStdString());
        return pet::PluginError::Busy;
    }
    s->start(); // async; progress via status/state observer events
    return pet::PluginError::Ok;
}

pet::PluginError InstanceControlApiImpl::stop(const QString& uuid)
{
    InstanceSession* s = sessionForUuid(uuid);
    if (s == nullptr)
        return pet::PluginError::NotFound;
    // S4: non-blocking initiate; completion via the status flip to
    // "stopped" (+ roster/modelLoaded updates) on the observer side. A
    // delete-pending session is already stopping — stop() is absorbed by
    // ProcessManager, Ok is truthful.
    s->stop();
    return pet::PluginError::Ok;
}

pet::PluginError InstanceControlApiImpl::restart(const QString& uuid)
{
    InstanceSession* s = sessionForUuid(uuid);
    if (s == nullptr)
        return pet::PluginError::NotFound;
    if (s->isDeletePending()) {
        LOG_WARN("[plugin-api] instanceControlApi().restart('{}') — pending "
                 "delete, ERR_BUSY", uuid.toStdString());
        return pet::PluginError::Busy;
    }
    s->restart(); // stop() + queued relaunch (S4)
    return pet::PluginError::Ok;
}

pet::PluginError InstanceControlApiImpl::loadModel(const QString& uuid,
                                                   const QString& modelName)
{
    if (modelName.isEmpty()) {
        LOG_WARN("[plugin-api] instanceControlApi().loadModel('{}') with empty "
                 "model name — ERR_INVALID_ARGUMENT", uuid.toStdString());
        return pet::PluginError::InvalidArgument;
    }
    InstanceSession* s = sessionForUuid(uuid);
    if (s == nullptr)
        return pet::PluginError::NotFound;
    if (s->isDeletePending()) {
        LOG_WARN("[plugin-api] instanceControlApi().loadModel('{}') — pending "
                 "delete, ERR_BUSY", uuid.toStdString());
        return pet::PluginError::Busy;
    }
    // Offline switch persists the choice (applies on next start); online
    // additionally sends load_model. Failures surface via the
    // modelLoadFailed observer callback.
    s->loadModel(modelName);
    return pet::PluginError::Ok;
}

// ── PluginContextImpl ───────────────────────────────────────────────────────

PluginContextImpl::PluginContextImpl(const QString& pluginId,
                                     InstanceApiImpl* sharedInstanceApi,
                                     InstanceControlApiImpl* sharedInstanceControlApi,
                                     std::function<bool()> writeEnabledProvider,
                                     PluginPageModel* pageModel,
                                     NotificationStreamController* stream,
                                     const QString& configRoot,
                                     DownloadService* downloadService,
                                     const QStringList& capabilities,
                                     std::function<void()> voicePackRefresh,
                                     QObject* parent)
    : QObject(parent),
      m_pluginId(pluginId),
      m_configRoot(configRoot),
      m_instanceApi(sharedInstanceApi),
      m_instanceControlApi(sharedInstanceControlApi),
      m_instanceLifecycleGranted(
          capabilities.contains(QString(pet::kCapabilityInstanceLifecycle))),
      m_writeEnabled(std::move(writeEnabledProvider)),
      m_uiApi(pluginId, pageModel, stream, this),
      m_voicePackApi(std::move(voicePackRefresh), this),
      m_downloadApi(pluginId,
                    capabilities.contains(QStringLiteral("network")),
                    downloadService, this)
{
}

QString PluginContextImpl::pluginConfigDir()
{
    const QString dir = QDir(m_configRoot).filePath(
        QStringLiteral("plugins/") + m_pluginId);
    // On-demand creation; failure degrades to a returned-but-missing path
    // (writes inside will fail loudly at the plugin's own file API).
    if (!QDir().mkpath(dir)) {
        LOG_WARN("[plugin-api] cannot create config dir '{}' for '{}'",
                 dir.toStdString(), m_pluginId.toStdString());
    }
    return dir;
}

void PluginContextImpl::log(pet::PluginLogLevel level, const QString& message)
{
    const auto id = m_pluginId.toStdString();
    switch (level) {
    case pet::PluginLogLevel::Debug:   LOG_DEBUG("[plugin/{}] {}", id, message.toStdString()); break;
    case pet::PluginLogLevel::Info:    LOG_INFO("[plugin/{}] {}", id, message.toStdString()); break;
    case pet::PluginLogLevel::Warning: LOG_WARN("[plugin/{}] {}", id, message.toStdString()); break;
    case pet::PluginLogLevel::Error:   LOG_ERROR("[plugin/{}] {}", id, message.toStdString()); break;
    }
}

pet::IExtApi* PluginContextImpl::queryApi(const char* apiId, int minVersion)
{
    if (apiId == nullptr)
        return nullptr;

    // ── Family: pet.instance_control (S2, v1.2) ─────────────────────────
    // Routing matrix (capability × host switch × wiring):
    //   granted  + switch on  + shared impl  → real InstanceControlApiImpl
    //   granted  + switch off                → stub (PluginError::Capability)
    //   not granted                           → stub (PluginError::Capability)
    //   degraded wiring (no shared impl)     → stub
    //   minVersion > family version          → nullptr ("feature absent")
    //   unknown apiId                        → nullptr
    // The switch is consulted LIVE on every query (the kill-switch must
    // revoke without a restart to stay useful); no provider injected
    // defaults to enabled, mirroring the kv default.
    if (QLatin1StringView(apiId) == QLatin1StringView(pet::kInstanceControlApiId)) {
        if (minVersion > pet::kInstanceControlApiVersion)
            return nullptr;
        const bool writeEnabled = m_writeEnabled ? m_writeEnabled() : true;
        if (m_instanceLifecycleGranted && writeEnabled && m_instanceControlApi != nullptr)
            return m_instanceControlApi;
        return &m_instanceControlStub;
    }

    // Unknown family — the documented "feature absent" answer (see
    // IPluginContext.hpp). Future families (ISettingsApi, ...) route here.
    return nullptr;
}

} // namespace core
