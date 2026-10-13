#include "core/PluginContextImpl.hpp"

#include <QDir>
#include <QDateTime>
#include <QFileInfo>

#include <spdlog/spdlog.h>

#include "api/EventIds.hpp"
#include "core/DownloadService.hpp"
#include "core/ConfigDir.hpp"
#include "core/EventBus.hpp"
#include "core/EventCatalog.hpp"
#include "core/InstanceManager.hpp"
#include "core/InstanceSession.hpp"
#include "core/ModelInfoParser.hpp"
#include "core/ModelScanner.hpp"
#include "core/MetaMkoParser.hpp"
#include "core/PanelConfig.hpp"
#include "core/PanelStateManager.hpp"
#include "core/PathResolve.hpp"
#include "core/PluginHost.hpp"
#include "core/PluginPageModel.hpp"
#include "core/VoicePackScanner.hpp"
#include "logging/Logging.hpp"
#include "ui/MonitorDataModel.hpp"
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
    // v1.3 (S5): mountedVoicePackChanged is the 9th hook — the minimal
    // InstanceSession seam that makes mountedPackId real-time instead of
    // "carried by the next unrelated property flip".
    for (auto signal : { &InstanceSession::statusChanged,
                         &InstanceSession::connectedChanged,
                         &InstanceSession::modelLoadedChanged,
                         &InstanceSession::modelNameChanged,
                         &InstanceSession::opacityChanged,
                         &InstanceSession::targetFpsChanged,
                         &InstanceSession::volumeChanged,
                         &InstanceSession::mutedChanged,
                         &InstanceSession::mountedVoicePackChanged }) {
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
    // dir; empty dir → empty id. v1.3 (S5): the mountedVoicePackChanged
    // NOTIFY makes a pure mount/unmount fan out immediately (previously
    // the value rode the next unrelated property flip).
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

// ── TuningApiImpl (S5, v1.3) ─────────────────────────────────────────────────

TuningApiImpl::TuningApiImpl(InstanceManager* instanceManager, QObject* parent)
    : QObject(parent), m_instanceManager(instanceManager)
{
}

void TuningApiImpl::setPackScanDirs(const QString& rendererDir,
                                    const QString& userPacksDir)
{
    m_packRendererDir = rendererDir;
    m_packUserDir = userPacksDir;
}

InstanceSession* TuningApiImpl::sessionForUuid(const QString& uuid) const
{
    // Same public-surface linear scan as InstanceControlApiImpl (sidebar-
    // sized N).
    if (m_instanceManager == nullptr)
        return nullptr;
    for (int row = 0; row < m_instanceManager->rowCount(); ++row) {
        InstanceSession* s = m_instanceManager->instanceAt(row);
        if (s != nullptr && s->uuid() == uuid)
            return s;
    }
    return nullptr;
}

pet::PluginError TuningApiImpl::locateAlive(const QString& uuid,
                                            InstanceSession** out) const
{
    *out = sessionForUuid(uuid);
    if (*out == nullptr) {
        LOG_WARN("[plugin-api] tuningApi() op on '{}' — not in roster, "
                 "ERR_NOT_FOUND", uuid.toStdString());
        return pet::PluginError::NotFound;
    }
    if ((*out)->isDeletePending()) {
        LOG_WARN("[plugin-api] tuningApi() op on '{}' — pending delete, "
                 "ERR_BUSY", uuid.toStdString());
        return pet::PluginError::Busy;
    }
    return pet::PluginError::Ok;
}

QString TuningApiImpl::resolvePackDir(const QString& packId) const
{
    // packId is the DIRECTORY NAME (SDK-wide pack identity). Reverse-lookup
    // against the host's own dual-source scan (VoicePackScanner returns
    // ABSOLUTE pack dirs; user packs win collisions — same discovery the
    // voice-pack page and the install pipeline use). An id that is not a
    // discovered directory resolves empty → NotFound at the call site: a
    // caller-supplied PATH never reaches the behavior engine.
    // Scan sources: injected test seams when set, production defaults
    // otherwise (same pair VoicePackApiImpl's standalone scan uses).
    const QString rendererDir = !m_packRendererDir.isEmpty()
        ? m_packRendererDir : defaultRendererDir();
    const QString userDir = !m_packUserDir.isEmpty()
        ? m_packUserDir : ConfigDir::userVoicePacksDir();
    const QStringList dirs = scanAvailableVoicePacks(rendererDir, userDir);
    for (const QString& dir : dirs) {
        if (QFileInfo(dir).fileName() == packId)
            return dir;
    }
    return QString();
}

pet::PluginError TuningApiImpl::setOpacity(const QString& uuid, double opacity)
{
    InstanceSession* s = nullptr;
    const pet::PluginError locate = locateAlive(uuid, &s);
    if (locate != pet::PluginError::Ok)
        return locate;
    s->setOpacity(opacity); // no-op on same value; renderer clamps range
    return pet::PluginError::Ok;
}

pet::PluginError TuningApiImpl::setVolume(const QString& uuid, double volume)
{
    InstanceSession* s = nullptr;
    const pet::PluginError locate = locateAlive(uuid, &s);
    if (locate != pet::PluginError::Ok)
        return locate;
    s->setVolume(volume);
    return pet::PluginError::Ok;
}

pet::PluginError TuningApiImpl::setMuted(const QString& uuid, bool muted)
{
    InstanceSession* s = nullptr;
    const pet::PluginError locate = locateAlive(uuid, &s);
    if (locate != pet::PluginError::Ok)
        return locate;
    s->setMuted(muted);
    return pet::PluginError::Ok;
}

pet::PluginError TuningApiImpl::setFps(const QString& uuid, int fps)
{
    InstanceSession* s = nullptr;
    const pet::PluginError locate = locateAlive(uuid, &s);
    if (locate != pet::PluginError::Ok)
        return locate;
    s->setFps(fps);
    return pet::PluginError::Ok;
}

pet::PluginError TuningApiImpl::playMotion(const QString& uuid,
                                           const QString& group, int index)
{
    if (group.isEmpty()) {
        LOG_WARN("[plugin-api] tuningApi().playMotion('{}') with empty "
                 "group — ERR_INVALID_ARGUMENT", uuid.toStdString());
        return pet::PluginError::InvalidArgument;
    }
    InstanceSession* s = nullptr;
    const pet::PluginError locate = locateAlive(uuid, &s);
    if (locate != pet::PluginError::Ok)
        return locate;
    s->playMotion(group, index); // renderer ignores unknown group/index
    return pet::PluginError::Ok;
}

pet::PluginError TuningApiImpl::setExpression(const QString& uuid,
                                              const QString& expressionId)
{
    if (expressionId.isEmpty()) {
        LOG_WARN("[plugin-api] tuningApi().setExpression('{}') with empty "
                 "id — ERR_INVALID_ARGUMENT", uuid.toStdString());
        return pet::PluginError::InvalidArgument;
    }
    InstanceSession* s = nullptr;
    const pet::PluginError locate = locateAlive(uuid, &s);
    if (locate != pet::PluginError::Ok)
        return locate;
    s->setExpression(expressionId); // renderer ignores unknown ids
    return pet::PluginError::Ok;
}

pet::PluginError TuningApiImpl::triggerHitArea(const QString& uuid,
                                               const QString& areaId)
{
    if (areaId.isEmpty()) {
        LOG_WARN("[plugin-api] tuningApi().triggerHitArea('{}') with empty "
                 "area — ERR_INVALID_ARGUMENT", uuid.toStdString());
        return pet::PluginError::InvalidArgument;
    }
    InstanceSession* s = nullptr;
    const pet::PluginError locate = locateAlive(uuid, &s);
    if (locate != pet::PluginError::Ok)
        return locate;
    // Same decision chain a real click runs (pack behavior first, then
    // the default hit→motion handler).
    s->triggerHitArea(areaId);
    return pet::PluginError::Ok;
}

pet::PluginError TuningApiImpl::mountVoicePack(const QString& uuid,
                                               const QString& packId)
{
    if (packId.isEmpty()) {
        LOG_WARN("[plugin-api] tuningApi().mountVoicePack('{}') with empty "
                 "pack id — ERR_INVALID_ARGUMENT", uuid.toStdString());
        return pet::PluginError::InvalidArgument;
    }
    InstanceSession* s = nullptr;
    const pet::PluginError locate = locateAlive(uuid, &s);
    if (locate != pet::PluginError::Ok)
        return locate;
    const QString dir = resolvePackDir(packId);
    if (dir.isEmpty()) {
        LOG_WARN("[plugin-api] tuningApi().mountVoicePack('{}', '{}') — "
                 "pack id not discovered by the host scan, ERR_NOT_FOUND "
                 "(paths are never interpreted)",
                 uuid.toStdString(), packId.toStdString());
        return pet::PluginError::NotFound;
    }
    // The session parses meta.mko itself; a parse failure leaves the
    // current mount untouched and reports Generic.
    return s->mountVoicePack(dir) ? pet::PluginError::Ok
                                  : pet::PluginError::Generic;
}

pet::PluginError TuningApiImpl::unmountVoicePack(const QString& uuid)
{
    InstanceSession* s = nullptr;
    const pet::PluginError locate = locateAlive(uuid, &s);
    if (locate != pet::PluginError::Ok)
        return locate;
    s->unmountVoicePack(); // idempotent on an already-unmounted session
    return pet::PluginError::Ok;
}

// ── ModelApiImpl (S5, v1.3) ─────────────────────────────────────────────────

ModelApiImpl::ModelApiImpl(QObject* parent)
    : QObject(parent)
{
}

void ModelApiImpl::setRendererDir(const QString& dir)
{
    m_rendererDir = dir;
    rescan();
}

void ModelApiImpl::refreshScan()
{
    rescan();
}

void ModelApiImpl::rescan()
{
    // Ported verbatim from ModelController::rescan (the cache moved here —
    // ONE scan serves the panel page and the plugin API). Pre-parse at
    // scan time so availableModels()/modelInfo() stay cache lookups; a
    // missing/corrupt .model3.json degrades to empty detail lists, the
    // roster entry survives.
    m_models.clear();
    m_modelsDir = m_rendererDir.isEmpty()
        ? QString()
        : QDir(m_rendererDir).absoluteFilePath(
              QStringLiteral("Resources/Models"));
    const QStringList names = scanAvailableModels(m_rendererDir);
    for (const QString& name : names) {
        pet::ModelSummary summary;
        summary.name = name;
        const QString model3Json = QDir(m_modelsDir).absoluteFilePath(
            name + QLatin1Char('/') + name + QStringLiteral(".model3.json"));
        const auto info = parseModelInfo(model3Json);
        if (info.has_value()) {
            summary.motionGroups = info->motionGroups.keys();
            summary.expressions = info->expressions;
            summary.hitAreas = info->hitAreas;
        } else {
            LOG_WARN("ModelApiImpl: could not parse \"{}\" — empty details",
                     model3Json.toStdString());
        }
        m_models.append(std::move(summary));
    }
    LOG_DEBUG("ModelApiImpl: scan found {} model(s) under \"{}\"",
              m_models.size(), m_modelsDir.toStdString());
}

QVector<pet::ModelSummary> ModelApiImpl::availableModels()
{
    return m_models;
}

pet::PluginError ModelApiImpl::modelInfo(const QString& name,
                                         pet::ModelSummary* out)
{
    if (out != nullptr)
        out->name.clear(); // never leave *out half-written on a miss
    for (const pet::ModelSummary& summary : m_models) {
        if (summary.name == name) {
            if (out != nullptr)
                *out = summary;
            return pet::PluginError::Ok;
        }
    }
    return pet::PluginError::NotFound;
}

// ── SettingsApiImpl (S6, v1.3) ───────────────────────────────────────────────

SettingsApiImpl::SettingsApiImpl(const QString& configDir, QObject* parent)
    : QObject(parent), m_configDir(configDir)
{
}

bool SettingsApiImpl::updateField(const std::function<void(PanelConfig&)>& mutator)
{
    // IDENTICAL load-modify-save discipline to PanelConfigController::
    // updateField (the panel's own settings page writes land here too
    // since S6 — one persistence path, the other 8 PanelConfig fields
    // always survive).
    PanelStateManager psm(m_configDir);
    if (m_db != nullptr)
        psm.setDatabase(m_db);
    PanelConfig cfg = psm.load();
    mutator(cfg);
    if (!psm.save(cfg)) {
        LOG_WARN("[plugin-api] settingsApi write — panel_config save failed");
        return false;
    }
    return true;
}

pet::PluginError SettingsApiImpl::setCloseAction(const QString& action)
{
    // Same B2 guard as the panel's own path: exactly "exit" | "minimize".
    if (action != QLatin1String("exit") && action != QLatin1String("minimize")) {
        LOG_WARN("[plugin-api] settingsApi().setCloseAction('{}') — illegal "
                 "value (only \"exit\"/\"minimize\"), ERR_INVALID_ARGUMENT",
                 action.toStdString());
        return pet::PluginError::InvalidArgument;
    }
    if (updateField([action](PanelConfig& cfg) { cfg.closeAction = action; })) {
        // v1.7 (S9): post-commit panel-level publish — only after the
        // panel_config row actually persisted.
        publishSettingsChanged(QStringLiteral("close_action"), action);
        return pet::PluginError::Ok;
    }
    return pet::PluginError::Generic;
}

pet::PluginError SettingsApiImpl::setConfirmOnExit(bool enabled)
{
    if (updateField([enabled](PanelConfig& cfg) { cfg.confirmOnExit = enabled; })) {
        publishSettingsChanged(QStringLiteral("confirm_on_exit"), enabled);
        return pet::PluginError::Ok;
    }
    return pet::PluginError::Generic;
}

pet::PluginError SettingsApiImpl::setStartMinimized(bool enabled)
{
    if (updateField([enabled](PanelConfig& cfg) { cfg.startMinimized = enabled; })) {
        publishSettingsChanged(QStringLiteral("start_minimized"), enabled);
        return pet::PluginError::Ok;
    }
    return pet::PluginError::Generic;
}

pet::PluginError SettingsApiImpl::setDefaultModelName(const QString& name)
{
    // Free-form string (a dir name under Resources/Models) — validity is
    // the model-library UI's concern; only the empty string is illegal
    // (empty MEANS "use the default" at the call sites).
    if (name.isEmpty()) {
        LOG_WARN("[plugin-api] settingsApi().setDefaultModelName(\"\") — "
                 "ERR_INVALID_ARGUMENT");
        return pet::PluginError::InvalidArgument;
    }
    if (updateField([name](PanelConfig& cfg) { cfg.defaultModelName = name; })) {
        publishSettingsChanged(QStringLiteral("default_model_name"), name);
        return pet::PluginError::Ok;
    }
    return pet::PluginError::Generic;
}

void SettingsApiImpl::publishSettingsChanged(const QString& field,
                                             const QJsonValue& value)
{
    // v1.7 (S9): pet.panel.settings_changed — the FIRST host use of the
    // reserved pet.panel.* domain. PANEL-LEVEL: instanceUuid stays "" (no
    // per-pet identity applies). autoLaunchSystem never emits HERE
    // (structurally — it has no ISettingsApi surface); since v1.9 its
    // host-direct PanelConfigController path publishes the same channel
    // itself (see docs/bus-events.md §6.19).
    if (m_eventPublisher == nullptr)
        return; // tests / degraded wiring — pre-v1.7 behavior
    pet::Event evt;
    evt.eventId = QString(pet::kEventPanelSettingsChanged);
    evt.instanceUuid = QString(); // panel-level
    evt.publisherId = QString(pet::kEventPublisherHost);
    evt.cancellable = false;
    evt.payload = core::makeSettingsChangedPayload(field, value);
    m_eventPublisher->publish(evt); // synchronous; errors are bus-level
}

// ── MonitorApiImpl (S7, v1.4) ─────────────────────────────────────────────────

MonitorApiImpl::MonitorApiImpl(InstanceManager* instanceManager, QObject* parent)
    : QObject(parent), m_instanceManager(instanceManager)
{
    // Same wiring pattern as InstanceApiImpl's constructor: attach to
    // every session present at construction (instances load from disk
    // at manager construction) + every row inserted later. Connections
    // are sender-bound — a deleteLater'd session destroys its model with
    // itself and severs them automatically, so subscriptions to a dead
    // instance simply stop firing (the table entries linger harmlessly
    // until unsubscribe; uuids are never recycled).
    if (m_instanceManager != nullptr) {
        connect(m_instanceManager, &QAbstractItemModel::rowsInserted, this,
                [this](const QModelIndex&, int first, int last) {
                    for (int row = first; row <= last; ++row)
                        observeSession(m_instanceManager->instanceAt(row));
                });
        for (int row = 0; row < m_instanceManager->rowCount(); ++row)
            observeSession(m_instanceManager->instanceAt(row));
    }
}

void MonitorApiImpl::observeSession(InstanceSession* session)
{
    if (session == nullptr)
        return;
    // monitorModel() returns the session-owned MonitorDataModel as a
    // QObject* (the QML-facing Q_INVOKABLE signature). The dynamic type
    // IS MonitorDataModel (member object, single QObject inheritance) —
    // the static_cast is a pure read-path adapter, verified by
    // construction. Zero InstanceSession/MonitorDataModel changes.
    auto* model = static_cast<MonitorDataModel*>(session->monitorModel());
    if (model == nullptr)
        return;
    const QString uuid = session->uuid(); // value copy — safe in the lambda
    connect(model, &MonitorDataModel::snapshotAppended, this,
            [this, uuid]() { fanoutSample(uuid); });
}

QVector<pet::MonitorSample> MonitorApiImpl::history(const QString& uuid)
{
    QVector<pet::MonitorSample> out;
    InstanceSession* session = sessionForUuid(uuid);
    if (session == nullptr)
        return out; // unknown uuid / degraded wiring → empty, never error
    auto* model = static_cast<MonitorDataModel*>(session->monitorModel());
    if (model == nullptr)
        return out;
    const QVector<MonitorSnapshot> ring = model->history();
    out.reserve(ring.size());
    for (const MonitorSnapshot& snap : ring)
        out.append(projectSnapshot(snap));
    return out;
}

pet::PluginError MonitorApiImpl::subscribe(const QString& uuid,
                                           pet::IMonitorObserver* observer,
                                           const pet::MonitorSubscription& sub)
{
    if (sessionForUuid(uuid) == nullptr) {
        LOG_WARN("[plugin-api] monitorApi().subscribe('{}') — not in "
                 "roster, ERR_NOT_FOUND", uuid.toStdString());
        return pet::PluginError::NotFound;
    }
    if (observer == nullptr) {
        LOG_WARN("[plugin-api] monitorApi().subscribe('{}') with null "
                 "observer — ERR_INVALID_ARGUMENT", uuid.toStdString());
        return pet::PluginError::InvalidArgument;
    }
    // Re-subscribe of the same (uuid, observer) pair refreshes the
    // interval (and resets the throttle window) instead of duplicating
    // the entry — same idempotence discipline as subscribeRoster.
    for (Subscription& s : m_subscriptions) {
        if (s.uuid == uuid && s.observer == observer) {
            s.minIntervalMs = sub.minIntervalMs > 0 ? sub.minIntervalMs : 0;
            s.lastPushMs = 0;
            return pet::PluginError::Ok;
        }
    }
    Subscription s;
    s.uuid = uuid;
    s.observer = observer;
    s.minIntervalMs = sub.minIntervalMs > 0 ? sub.minIntervalMs : 0;
    m_subscriptions.append(s);
    return pet::PluginError::Ok;
}

void MonitorApiImpl::unsubscribe(const QString& uuid,
                                 pet::IMonitorObserver* observer)
{
    for (int i = m_subscriptions.size() - 1; i >= 0; --i) {
        const Subscription& s = m_subscriptions.at(i);
        if (s.uuid == uuid && s.observer == observer)
            m_subscriptions.remove(i);
    }
}

void MonitorApiImpl::fanoutSample(const QString& uuid)
{
    if (m_subscriptions.isEmpty())
        return;
    InstanceSession* session = sessionForUuid(uuid);
    if (session == nullptr)
        return;
    auto* model = static_cast<MonitorDataModel*>(session->monitorModel());
    if (model == nullptr)
        return;
    const pet::MonitorSample sample =
        projectSnapshot(model->latestSnapshot());
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    // Iterate REVERSE: an observer may unsubscribe (or re-subscribe)
    // inside the callback; removing the element at index i only shifts
    // already-visited higher indices, so reverse order guarantees each
    // remaining subscriber exactly one visit per fanout (push order is
    // reverse-subscription — not contractual). Plugin exceptions must
    // not poison the fanout either (§A.1 boundary discipline).
    for (int i = m_subscriptions.size() - 1; i >= 0; --i) {
        Subscription& s = m_subscriptions[i];
        if (s.uuid != uuid || s.observer == nullptr)
            continue;
        // Per-subscriber merge floor: samples landing inside the window
        // are SKIPPED (not queued) — the next accepted push carries the
        // LATEST snapshot, so bursts coalesce to one fresh sample.
        if (s.minIntervalMs > 0 && now - s.lastPushMs < s.minIntervalMs)
            continue;
        s.lastPushMs = now;
        try {
            s.observer->sample(uuid, sample);
        } catch (...) {
            LOG_ERROR("[plugin-api] monitor observer threw in sample() — "
                      "isolated, fanout continues");
        }
    }
}

InstanceSession* MonitorApiImpl::sessionForUuid(const QString& uuid) const
{
    // Same public-surface linear scan as InstanceControlApiImpl (the
    // access QML itself uses; sidebar-sized N).
    if (m_instanceManager == nullptr)
        return nullptr;
    for (int row = 0; row < m_instanceManager->rowCount(); ++row) {
        InstanceSession* s = m_instanceManager->instanceAt(row);
        if (s != nullptr && s->uuid() == uuid)
            return s;
    }
    return nullptr;
}

pet::MonitorSample MonitorApiImpl::projectSnapshot(
    const std::optional<MonitorSnapshot>& snapshot)
{
    // MonitorSnapshot (std::optional halves) → flat POD. Nullable GPU /
    // VRAM fields project to the -1 sentinel; halves that have not
    // reported yet project to their 0 "no data" defaults — the exact
    // values the Monitor page's own Q_INVOKABLE getters produce.
    pet::MonitorSample out;
    if (!snapshot.has_value())
        return out;
    if (snapshot->controller.has_value()) {
        out.controllerCpu = snapshot->controller->cpuPercent;
        out.controllerRssBytes =
            static_cast<double>(snapshot->controller->rssBytes);
    }
    if (snapshot->renderer.has_value()) {
        out.rendererCpu = snapshot->renderer->cpuPercent;
        out.rendererRssBytes =
            static_cast<double>(snapshot->renderer->rssBytes);
        out.rendererGpu = snapshot->renderer->gpuPercent.value_or(-1.0);
        out.rendererVramUsedBytes = snapshot->renderer->vramUsedBytes
            ? static_cast<double>(*snapshot->renderer->vramUsedBytes) : -1.0;
        out.rendererVramTotalBytes = snapshot->renderer->vramTotalBytes
            ? static_cast<double>(*snapshot->renderer->vramTotalBytes) : -1.0;
        // v1.8: the EMA-measured frame rate — an always-a-double half on
        // RendererStats whose -1 default IS the sentinel (missing key /
        // wrong type / renderer pre-first-frame sentinel all arrive as
        // -1 from RendererStats::fromJson), so a plain copy suffices.
        out.rendererFps = snapshot->renderer->fps;
    }
    out.capturedAtMs = snapshot->capturedAtMs;
    return out;
}

// ── LayoutApiImpl (v1.8) ─────────────────────────────────────────────────────

LayoutApiImpl::LayoutApiImpl(InstanceManager* instanceManager, QObject* parent)
    : QObject(parent), m_instanceManager(instanceManager)
{
}

InstanceSession* LayoutApiImpl::sessionForUuid(const QString& uuid) const
{
    // Same public-surface linear scan as TuningApiImpl (sidebar-sized N).
    if (m_instanceManager == nullptr)
        return nullptr;
    for (int row = 0; row < m_instanceManager->rowCount(); ++row) {
        InstanceSession* s = m_instanceManager->instanceAt(row);
        if (s != nullptr && s->uuid() == uuid)
            return s;
    }
    return nullptr;
}

pet::PluginError LayoutApiImpl::locateAlive(const QString& uuid,
                                            InstanceSession** out) const
{
    *out = sessionForUuid(uuid);
    if (*out == nullptr) {
        LOG_WARN("[plugin-api] layoutApi() op on '{}' — not in roster, "
                 "ERR_NOT_FOUND", uuid.toStdString());
        return pet::PluginError::NotFound;
    }
    if ((*out)->isDeletePending()) {
        LOG_WARN("[plugin-api] layoutApi() op on '{}' — pending delete, "
                 "ERR_BUSY", uuid.toStdString());
        return pet::PluginError::Busy;
    }
    return pet::PluginError::Ok;
}

pet::PluginError LayoutApiImpl::getLayout(const QString& uuid,
                                          pet::LayoutInfo* out)
{
    if (out != nullptr)
        *out = pet::LayoutInfo{}; // never leave *out half-written on a miss
    if (out == nullptr) {
        LOG_WARN("[plugin-api] layoutApi().getLayout('{}') with null out "
                 "pointer — ERR_INVALID_ARGUMENT", uuid.toStdString());
        return pet::PluginError::InvalidArgument;
    }
    InstanceSession* s = nullptr;
    const pet::PluginError locate = locateAlive(uuid, &s);
    if (locate != pet::PluginError::Ok)
        return locate;
    // CONFIG-SNAPSHOT projection (the ILayoutApi.hpp contract): the
    // persisted InstanceConfig halves, NOT live renderer state — mid-
    // drag changes ride the pet.instance.layout_changed /
    // window_resized bus events. Field mapping (LayoutInfo ↔
    // InstanceConfig): x=windowX, y=windowY, width=windowWidth,
    // height=windowHeight (persisted on drag_end / window_resized);
    // scale=layoutScale (the set_layout / reset_layout domain,
    // persisted on layout_changed — NOT the legacy modelScale boot
    // field).
    const InstanceConfig& cfg = s->config();
    out->x = static_cast<double>(cfg.windowX);
    out->y = static_cast<double>(cfg.windowY);
    out->width = static_cast<double>(cfg.windowWidth);
    out->height = static_cast<double>(cfg.windowHeight);
    out->scale = cfg.layoutScale;
    return pet::PluginError::Ok;
}

pet::PluginError LayoutApiImpl::resetLayout(const QString& uuid)
{
    InstanceSession* s = nullptr;
    const pet::PluginError locate = locateAlive(uuid, &s);
    if (locate != pet::PluginError::Ok)
        return locate;
    // Accepted, not applied (fire-and-forget reset_layout): the renderer
    // applies defaults; any resulting layout_changed persists + publishes
    // through the session's existing event path.
    s->resetLayout();
    return pet::PluginError::Ok;
}

pet::PluginError LayoutApiImpl::setAutoStart(const QString& uuid, bool enabled)
{
    InstanceSession* s = nullptr;
    const pet::PluginError locate = locateAlive(uuid, &s);
    if (locate != pet::PluginError::Ok)
        return locate;
    // Same persistence the detail page's toggle drives: the flag applies
    // at the next panel launch, no synchronous renderer start. The
    // session absorbs same-value writes (idempotent) — Ok stays truthful.
    s->setAutoStart(enabled);
    return pet::PluginError::Ok;
}

// ── EventApiImpl + EventApiProxy (S8a, v1.5) ────────────────────────────────

EventApiImpl::EventApiImpl(EventBus* bus, QObject* parent)
    : QObject(parent), m_bus(bus)
{
}

pet::PluginError EventApiImpl::subscribe(const QString& eventId,
                                         pet::IEventListener* listener,
                                         pet::EventPriority priority)
{
    if (m_bus == nullptr)
        return pet::PluginError::Generic; // degraded wiring, never a crash
    return m_bus->subscribe(eventId, listener, priority, QString());
}

void EventApiImpl::unsubscribe(const QString& eventId,
                               pet::IEventListener* listener)
{
    if (m_bus == nullptr)
        return;
    m_bus->unsubscribe(eventId, listener);
}

pet::PluginError EventApiImpl::publish(pet::Event& event)
{
    if (m_bus == nullptr)
        return pet::PluginError::Generic; // degraded wiring, never a crash
    // Direct/host channel: publisherId forwarded VERBATIM (the host
    // emitter owns the field — only the proxy force-stamps).
    return m_bus->publish(event);
}

pet::PluginError EventApiImpl::subscribeAs(const QString& publisherId,
                                           const QString& eventId,
                                           pet::IEventListener* listener,
                                           pet::EventPriority priority)
{
    if (m_bus == nullptr)
        return pet::PluginError::Generic;
    return m_bus->subscribe(eventId, listener, priority, publisherId);
}

int EventApiImpl::unsubscribeByPublisher(const QString& publisherId)
{
    return m_bus != nullptr ? m_bus->unsubscribeByPublisher(publisherId) : 0;
}

EventApiProxy::EventApiProxy(const QString& pluginId, EventApiImpl* shared)
    : m_pluginId(pluginId), m_shared(shared)
{
}

pet::PluginError EventApiProxy::subscribe(const QString& eventId,
                                          pet::IEventListener* listener,
                                          pet::EventPriority priority)
{
    if (m_shared == nullptr)
        return pet::PluginError::Generic; // no wiring — never a crash
    // Booked under the plugin id: PluginHost's unload sweep finds these
    // entries even when the plugin forgot to unsubscribe.
    return m_shared->subscribeAs(m_pluginId, eventId, listener, priority);
}

void EventApiProxy::unsubscribe(const QString& eventId,
                                pet::IEventListener* listener)
{
    if (m_shared == nullptr)
        return;
    m_shared->unsubscribe(eventId, listener);
}

pet::PluginError EventApiProxy::publish(pet::Event& event)
{
    if (m_shared == nullptr)
        return pet::PluginError::Generic; // no wiring — never a crash
    // Anti-forgery (api/EventIds.hpp): a plugin may ONLY publish its own
    // plugin.<ownId>.* namespace — host pet.* channels are not
    // impersonable. Violations die here with ZERO dispatch.
    event.publisherId = m_pluginId; // forced stamp, before validation
    const QString ownPrefix =
        QString(pet::kEventPluginNamespace) + m_pluginId + QLatin1Char('.');
    if (!event.eventId.startsWith(ownPrefix)) {
        LOG_WARN("[plugin-api] eventsApi().publish(\"{}\") from '{}' — "
                 "outside the plugin's own \"{}*\" namespace, "
                 "ERR_INVALID_ARGUMENT (zero dispatch)",
                 event.eventId.toStdString(), m_pluginId.toStdString(),
                 ownPrefix.toStdString());
        return pet::PluginError::InvalidArgument;
    }
    return m_shared->publish(event);
}

// ── RuleApiImpl + RuleApiProxy (S8f, v1.6) ──────────────────────────────────

RuleApiImpl::RuleApiImpl(RuleEngineService* ruleEngine, QObject* parent)
    : QObject(parent), m_ruleEngine(ruleEngine)
{
}

pet::PluginError RuleApiImpl::registerRule(const QJsonObject& rule)
{
    // HOST channel: Panel scope, empty owner — the same stamp the
    // panel-kv loading pipeline applies (the file pipeline remains the
    // primary host surface; this channel is symmetry + tests).
    if (m_ruleEngine == nullptr)
        return pet::PluginError::Generic; // degraded wiring, never a crash
    RuleError error;
    const auto stored = m_ruleEngine->upsertRuleJson(
        rule, RuleScope::Panel, QString(), &error);
    return stored.has_value() ? pet::PluginError::Ok
                              : pet::PluginError::InvalidArgument;
}

void RuleApiImpl::unregisterRule(const QString& ruleId)
{
    if (m_ruleEngine != nullptr)
        m_ruleEngine->removeRuleById(ruleId);
}

QString RuleApiImpl::pluginRuleId(const QString& pluginId,
                                  const QString& bareId)
{
    return QStringLiteral("plugin.%1.%2").arg(pluginId, bareId);
}

pet::PluginError RuleApiImpl::registerPluginRule(
    const QString& pluginId, const QJsonObject& rule,
    const std::function<void(pet::PluginLogLevel, const QString&)>& log)
{
    auto pluginLog =
        [&log](pet::PluginLogLevel level, const QString& message) {
            if (log)
                log(level, message);
        };
    if (m_ruleEngine == nullptr) {
        pluginLog(pet::PluginLogLevel::Error,
                  QStringLiteral("rulesApi().registerRule: no rule engine "
                                 "wired (degraded host)"));
        return pet::PluginError::Generic;
    }
    // Ownership pre-check: a stored id that already exists under another
    // owner is refused (dotted plugin ids can nest — "org.a" vs
    // "org.a.b" — so the namespaced id alone is not proof of ownership).
    const QJsonValue idValue = rule.value(QStringLiteral("id"));
    if (idValue.isString()) {
        const auto existing = m_ruleEngine->ruleById(
            pluginRuleId(pluginId, idValue.toString()));
        if (existing.has_value()
            && (existing->scope != RuleScope::Plugin
                || existing->ownerUuid != pluginId)) {
            pluginLog(pet::PluginLogLevel::Warning,
                      QStringLiteral("rulesApi().registerRule: id '%1' is "
                                     "already stored under another owner — "
                                     "rejected")
                          .arg(idValue.toString()));
            return pet::PluginError::InvalidArgument;
        }
    }
    RuleError error;
    const auto stored = m_ruleEngine->upsertRuleJson(
        rule, RuleScope::Plugin, pluginId, &error);
    if (!stored.has_value()) {
        // The service already WARNed the structured reason; surface it
        // through the plugin's own context log as well (the IRuleApi
        // contract's "reason via context log" half).
        pluginLog(pet::PluginLogLevel::Warning,
                  QStringLiteral("rulesApi().registerRule: rejected — %1 "
                                 "(%2)")
                      .arg(error.message, error.code));
        return pet::PluginError::InvalidArgument;
    }
    pluginLog(pet::PluginLogLevel::Info,
              QStringLiteral("rulesApi().registerRule: stored as '%1'")
                  .arg(stored->id));
    return pet::PluginError::Ok;
}

void RuleApiImpl::unregisterPluginRule(const QString& pluginId,
                                       const QString& bareRuleId)
{
    if (m_ruleEngine == nullptr)
        return;
    const QString storedId = pluginRuleId(pluginId, bareRuleId);
    // Ownership re-verification before removal — the prefixed id alone
    // must not be the only guard (a plugin id that is a dotted PREFIX of
    // another plugin's id could otherwise name the neighbor's stored
    // rules).
    const auto existing = m_ruleEngine->ruleById(storedId);
    if (!existing.has_value())
        return; // unknown id — the documented legal no-op
    if (existing->scope != RuleScope::Plugin
        || existing->ownerUuid != pluginId) {
        LOG_WARN("[plugin-api] rulesApi().unregisterRule(\"{}\") from "
                 "'{}' — the stored rule belongs to another owner, "
                 "refused",
                 bareRuleId.toStdString(), pluginId.toStdString());
        return;
    }
    m_ruleEngine->removeRuleById(storedId);
}

int RuleApiImpl::sweepPluginRules(const QString& pluginId)
{
    if (m_ruleEngine == nullptr)
        return 0;
    return m_ruleEngine->removeRulesByOwnerPrefix(
        QStringLiteral("plugin.%1.").arg(pluginId));
}

RuleApiProxy::RuleApiProxy(
    const QString& pluginId, RuleApiImpl* shared,
    std::function<void(pet::PluginLogLevel, const QString&)> log)
    : m_pluginId(pluginId), m_shared(shared), m_log(std::move(log))
{
}

pet::PluginError RuleApiProxy::registerRule(const QJsonObject& rule)
{
    if (m_shared == nullptr)
        return pet::PluginError::Generic; // no wiring — never a crash
    // Owner stamping lives in the shared impl's plugin seam: scope=Plugin
    // + ownerUuid=<ownId> + the "plugin.<ownId>." stored-id prefix.
    return m_shared->registerPluginRule(m_pluginId, rule, m_log);
}

void RuleApiProxy::unregisterRule(const QString& ruleId)
{
    if (m_shared == nullptr)
        return;
    m_shared->unregisterPluginRule(m_pluginId, ruleId);
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
    // v1.3 (S5/S6): tuning/settings capability bits precomputed from the
    // manifest (same discipline as instance_lifecycle). Set in the BODY —
    // the members are declared after the init-list members and a reordered
    // init list would trip -Wreorder.
    m_instanceTuningGranted =
        capabilities.contains(QString(pet::kCapabilityInstanceTuning));
    m_settingsWriteGranted =
        capabilities.contains(QString(pet::kCapabilitySettingsWrite));
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

void PluginContextImpl::setEventApi(EventApiImpl* eventApi)
{
    // Post-construction seam (the frozen-ctor-signature discipline of
    // the v1.3/v1.4 setters). The proxy is (re)built only when a shared
    // impl exists; a null injection tears the proxy down — queryApi then
    // answers nullptr ("feature absent").
    m_sharedEventApi = eventApi;
    m_eventProxy = eventApi != nullptr
        ? std::make_unique<EventApiProxy>(m_pluginId, eventApi)
        : nullptr;
}

void PluginContextImpl::setRuleApi(RuleApiImpl* ruleApi)
{
    // v1.6 (S8f): same post-construction seam discipline as setEventApi.
    // The proxy carries the context's own log() so schema rejection
    // reasons surface under the plugin's id prefix.
    m_sharedRuleApi = ruleApi;
    m_ruleProxy = ruleApi != nullptr
        ? std::make_unique<RuleApiProxy>(
              m_pluginId, ruleApi,
              [this](pet::PluginLogLevel level, const QString& message) {
                  log(level, message);
              })
        : nullptr;
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

    // ── Family: pet.instance_tuning (S5, v1.3) ──────────────────────────
    // Same routing matrix as pet.instance_control, gated by the
    // "instance_tuning" capability instead:
    //   granted + switch on + shared impl → real TuningApiImpl
    //   granted + switch off              → stub (PluginError::Capability)
    //   not granted                       → stub
    //   degraded wiring (no shared impl)  → stub
    //   minVersion > family version       → nullptr
    if (QLatin1StringView(apiId) == QLatin1StringView(pet::kTuningApiId)) {
        if (minVersion > pet::kTuningApiVersion)
            return nullptr;
        const bool writeEnabled = m_writeEnabled ? m_writeEnabled() : true;
        if (m_instanceTuningGranted && writeEnabled && m_tuningApi != nullptr)
            return m_tuningApi;
        return &m_tuningStub;
    }

    // ── Family: pet.model (S5, v1.3) ────────────────────────────────────
    // READ-ONLY family: deliberately NOT gated by any capability or the
    // plugin_write_enabled switch (the switch revokes WRITES; discovery
    // metadata leaks nothing the model-library page hides). Injected →
    // the shared real implementation; not wired → nullptr ("feature
    // absent" — there is no capability story to report for a read).
    if (QLatin1StringView(apiId) == QLatin1StringView(pet::kModelApiId)) {
        if (minVersion > pet::kModelApiVersion)
            return nullptr;
        return m_modelApi; // may be nullptr — feature-absent by contract
    }

    // ── Family: pet.monitor (S7, v1.4) ─────────────────────────────────
    // Same read-only routing as pet.model: no capability, no write
    // switch, no stub — injected → the shared MonitorApiImpl; not wired
    // → nullptr ("feature absent").
    if (QLatin1StringView(apiId) == QLatin1StringView(pet::kMonitorApiId)) {
        if (minVersion > pet::kMonitorApiVersion)
            return nullptr;
        return m_monitorApi; // may be nullptr — feature-absent by contract
    }

    // ── Family: pet.events (S8a, v1.5) ──────────────────────────────────
    // Bus family: NOT capability-gated and NOT write-switch-gated (the
    // switch revokes WRITES to host state; the bus is a notification
    // channel whose plugin-side forgery protection is the proxy's
    // publisher-prefix rule). Injected → the per-context EventApiProxy
    // (every plugin publish prefix-checked + publisher-stamped; the host
    // bridges hold the raw shared Impl precisely because the host may
    // publish pet.*); not wired → nullptr ("feature absent").
    if (QLatin1StringView(apiId) == QLatin1StringView(pet::kEventsApiId)) {
        if (minVersion > pet::kEventsApiVersion)
            return nullptr;
        return m_eventProxy.get(); // may be nullptr — feature-absent by contract
    }

    // ── Family: pet.rules (S8f, v1.6) ──────────────────────────────────
    // Rules-registration family: NO capability word and NO write-switch
    // consult at query time — registration is deliberately LAZY (an
    // inert act); the gate fires at ACTION-EMISSION time inside
    // RuleEngineService (a plugin-owned play_motion/set_expression
    // requires the owner's LIVE instance_tuning grant + the write
    // switch — stronger than queryApi-time gating: a lingered rule
    // surface cannot outlive its grant). Injected → the per-context
    // RuleApiProxy (owner-prefix stamping + scope stamping); not wired
    // → nullptr ("feature absent").
    if (QLatin1StringView(apiId) == QLatin1StringView(pet::kRulesApiId)) {
        if (minVersion > pet::kRulesApiVersion)
            return nullptr;
        return m_ruleProxy.get(); // may be nullptr — feature-absent by contract
    }

    // ── Family: pet.settings (S6, v1.3) ─────────────────────────────────
    // Same routing matrix as pet.instance_control, gated by the
    // "settings_write" capability instead.
    if (QLatin1StringView(apiId) == QLatin1StringView(pet::kSettingsApiId)) {
        if (minVersion > pet::kSettingsApiVersion)
            return nullptr;
        const bool writeEnabled = m_writeEnabled ? m_writeEnabled() : true;
        if (m_settingsWriteGranted && writeEnabled && m_settingsApi != nullptr)
            return m_settingsApi;
        return &m_settingsStub;
    }

    // ── Family: pet.layout (v1.8) ────────────────────────────────────────
    // Same routing matrix as pet.instance_tuning, gated by the SAME
    // "instance_tuning" capability (layout ops are the tuning domain —
    // the capability's own doc line reads "scale / layout / motion
    // parameters"):
    //   granted + switch on + shared impl → real LayoutApiImpl
    //   granted + switch off              → stub (PluginError::Capability)
    //   not granted                       → stub
    //   degraded wiring (no shared impl)  → stub
    //   minVersion > family version       → nullptr
    if (QLatin1StringView(apiId) == QLatin1StringView(pet::kLayoutApiId)) {
        if (minVersion > pet::kLayoutApiVersion)
            return nullptr;
        const bool writeEnabled = m_writeEnabled ? m_writeEnabled() : true;
        if (m_instanceTuningGranted && writeEnabled && m_layoutApi != nullptr)
            return m_layoutApi;
        return &m_layoutStub;
    }

    // Unknown family — the documented "feature absent" answer (see
    // IPluginContext.hpp).
    return nullptr;
}

} // namespace core
