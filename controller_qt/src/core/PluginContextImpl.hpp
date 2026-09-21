#pragma once

// PluginContextImpl (P4, §B.3) — the host-side pet::IPluginContext handed
// to every plugin's initialize(). One instance per plugin, parented to the
// PluginHost (dies with the service tree, AFTER the QML context — plugin
// pages/bridges never outlive it).
//
// Exception contract (§A.1): the HOST wraps initialize() in try/catch;
// the context methods themselves are also defensive (roster fanout and
// page registration never throw across the boundary).

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

#include "api/IDownloadApi.hpp"
#include "api/IInstanceApi.hpp"
#include "api/IInstanceControlApi.hpp"
#include "api/IPluginContext.hpp"
#include "api/IUiApi.hpp"
#include "core/PluginCapabilityStubs.hpp"

// InstanceManager + NotificationStreamController are GLOBAL-namespace
// classes (mixed-namespace codebase); declared outside namespace core.
class InstanceManager;
class NotificationStreamController;
class InstanceSession;

namespace core {

class DownloadService;
class PluginHost;
class PluginPageModel;

// Roster API: read-only snapshot + pure-virtual observer fanout. Observers
// are notified from InstanceManager model signals (GUI thread — §B.3
// thread contract).
//
// v1.2 (S2): additionally serves the instance-granular observer family
// (subscribeInstances) — roster membership rides the same model-signal
// fanout, per-session property NOTIFYs drive instanceStateChanged
// (coarse-grained full InstanceRuntime snapshots) and the session's
// modelLoadFailed signal drives the observer's failure callback.
class InstanceApiImpl : public QObject, public pet::IInstanceApi
{
    Q_OBJECT
public:
    InstanceApiImpl(InstanceManager* instanceManager, QObject* parent);

    QVector<pet::InstanceInfo> instances() override;
    void subscribeRoster(pet::IRosterObserver* observer) override;
    void unsubscribeRoster(pet::IRosterObserver* observer) override;
    void subscribeInstances(pet::IInstanceObserver* observer) override;
    void unsubscribeInstances(pet::IInstanceObserver* observer) override;

private:
    void fanoutRosterChanged();

    // v1.2: attach the property-NOTIFY → instanceStateChanged wiring to one
    // session (called for sessions present at construction and for every
    // row later inserted). Connections die with the sender (deleteLater),
    // so no manual teardown is needed.
    void observeSession(InstanceSession* session);
    // Coarse-grained full-snapshot fanout for one session's observers.
    void fanoutInstanceState(InstanceSession* session);
    void fanoutModelLoadFailed(InstanceSession* session, const QString& error);
    // Build the full runtime snapshot of one session (POD copy).
    static pet::InstanceRuntime snapshotOf(const InstanceSession* session);

    InstanceManager* m_instanceManager; // not owned (service tree)
    QVector<pet::IRosterObserver*> m_observers;
    QVector<pet::IInstanceObserver*> m_instanceObservers; // v1.2 (S2)
};

// S2 (v1.2): real instance-lifecycle write API — the pet::
// IInstanceControlApi behind queryApi(kInstanceControlApiId). Constructed
// ONCE in the PanelApplication service tree (same sharing pattern as
// InstanceApiImpl) and injected both into every plugin context (via
// PluginHost) and into the panel's own RosterApiModel write bridge —
// host UI and plugins go through the same vtable. All methods are GUI
// thread, non-blocking; they only CALL the S4-semantics InstanceManager /
// InstanceSession surfaces (never modify them).
class InstanceControlApiImpl : public QObject, public pet::IInstanceControlApi
{
    Q_OBJECT
public:
    // instanceManager must outlive this object (service tree guarantees
    // it; null is tolerated → every op returns NotFound, degraded wiring).
    InstanceControlApiImpl(InstanceManager* instanceManager, QObject* parent);

    pet::PluginError create(const pet::InstanceSpec& spec, QString* outUuid) override;
    pet::PluginError remove(const QString& uuid) override;
    pet::PluginError start(const QString& uuid) override;
    pet::PluginError stop(const QString& uuid) override;
    pet::PluginError restart(const QString& uuid) override;
    pet::PluginError loadModel(const QString& uuid, const QString& modelName) override;

private:
    // Locate the session whose uuid() == uuid (linear scan over the
    // manager's public roster surface — sidebar-sized N). Null when the
    // manager is null or the uuid is not in the roster.
    InstanceSession* sessionForUuid(const QString& uuid) const;

    InstanceManager* m_instanceManager; // not owned (service tree)
};

// Real download API (P5): forwards to the host DownloadService when the
// plugin's manifest grants the "network" capability; without the grant
// every method fails with PluginError::Capability (§B.4 capability gate —
// the pre-P5 permanent stub is now CONDITIONAL, same loud-failure shape).
class DownloadApiAdapter final : public QObject, public pet::IDownloadApi
{
    Q_OBJECT
public:
    DownloadApiAdapter(QString pluginId, bool networkGranted,
                       DownloadService* service, QObject* parent);

    pet::JobId start(const pet::DownloadRequest& request,
                     pet::DownloadListener* listener) override;
    void cancel(pet::JobId job) override;
    pet::PluginError installArchive(pet::JobId job, const pet::InstallSpec& spec) override;

private:
    QString m_pluginId;
    bool m_networkGranted = false;
    DownloadService* m_service = nullptr; // null in degraded wiring → stub semantics
};

// UI API: page registration (boot window enforced by PluginPageModel) +
// bubbles through the panel's NotificationStreamController.
class UiApiImpl : public QObject, public pet::IUiApi
{
    Q_OBJECT
public:
    UiApiImpl(const QString& pluginId, PluginPageModel* pageModel,
              NotificationStreamController* stream, QObject* parent);

    pet::PluginError registerPage(const pet::PageDescriptor& page) override;
    void notifyBubble(const QString& text, int durationMs) override;

private:
    QString m_pluginId;
    PluginPageModel* m_pageModel;                       // not owned
    NotificationStreamController* m_notificationStream; // not owned (may be null)
};

class PluginContextImpl : public QObject, public pet::IPluginContext
{
    Q_OBJECT
public:
    // S3 roster dogfooding: the InstanceApiImpl is now constructed ONCE in
    // the PanelApplication service tree and injected here (same object the
    // panel's own RosterApiModel consumes — host UI and plugins go through
    // the same vtable). `sharedInstanceApi` must be non-null and outlive
    // this context (PluginHost guarantees both; the degraded-wiring
    // fallback in PluginHost::initializeAll parents it to the host).
    //
    // S2 (v1.2) additions: sharedInstanceControlApi is the shared
    // InstanceControlApiImpl served through queryApi when the plugin's
    // manifest grants "instance_lifecycle" AND writeEnabledProvider()
    // returns true (the host-global plugin_write_enabled kv switch —
    // absent provider defaults to enabled, mirroring the kv default).
    // Null impl (degraded wiring) degrades to the capability stub. All
    // other pointers are host-owned and outlive the context. configRoot
    // is ConfigDir::configDir(); the per-plugin dir is created on demand.
    // capabilities is the plugin's manifest whitelist (gates downloadApi
    // + instance control); voicePackRefresh is the host's P5 seam into
    // VoicePackController::rescan.
    PluginContextImpl(const QString& pluginId,
                      InstanceApiImpl* sharedInstanceApi,
                      InstanceControlApiImpl* sharedInstanceControlApi,
                      std::function<bool()> writeEnabledProvider,
                      PluginPageModel* pageModel,
                      NotificationStreamController* stream,
                      const QString& configRoot,
                      DownloadService* downloadService,
                      const QStringList& capabilities,
                      std::function<void()> voicePackRefresh,
                      QObject* parent);

    // pet::IPluginContext
    pet::IInstanceApi& instanceApi() override { return *m_instanceApi; }
    pet::IVoicePackApi& voicePackApi() override { return m_voicePackApi; }
    pet::IUiApi& uiApi() override { return m_uiApi; }
    pet::IDownloadApi& downloadApi() override { return m_downloadApi; }
    QString pluginConfigDir() override;
    void log(pet::PluginLogLevel level, const QString& message) override;
    pet::IExtApi* queryApi(const char* apiId, int minVersion) override;

private:
    QString m_pluginId;
    QString m_configRoot;
    InstanceApiImpl* m_instanceApi; // shared, not owned (service tree)
    // S2: shared write-API implementation (service tree / host fallback);
    // null → stub semantics. m_instanceLifecycleGranted is precomputed
    // from the manifest capabilities; m_writeEnabled re-consults the
    // provider on every queryApi (the kill-switch is live, not boot-time).
    InstanceControlApiImpl* m_instanceControlApi = nullptr; // not owned
    InstanceControlApiStub m_instanceControlStub;
    bool m_instanceLifecycleGranted = false;
    std::function<bool()> m_writeEnabled;
    UiApiImpl m_uiApi;
    VoicePackApiImpl m_voicePackApi;
    DownloadApiAdapter m_downloadApi;
};

} // namespace core
