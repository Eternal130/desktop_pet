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
#include "api/IModelApi.hpp"
#include "api/IPluginContext.hpp"
#include "api/ISettingsApi.hpp"
#include "api/ITuningApi.hpp"
#include "api/IUiApi.hpp"
#include "core/PluginCapabilityStubs.hpp"

// InstanceManager + NotificationStreamController are GLOBAL-namespace
// classes (mixed-namespace codebase); declared outside namespace core.
class InstanceManager;
class NotificationStreamController;
class InstanceSession;
class DatabaseManager;
struct PanelConfig;

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

// S5 (v1.3): real per-instance tuning API — the pet::ITuningApi behind
// queryApi(kTuningApiId). Constructed ONCE in the PanelApplication
// service tree (same sharing pattern as InstanceControlApiImpl) and
// injected into every plugin context (via PluginHost), into the panel's
// own InstanceControlBridge write bridge AND into VoicePackController's
// mount path — host UI and plugins go through the same vtable. Every
// method is GUI thread, non-blocking, "accepted"-semantics; it only
// CALLS the existing InstanceSession surfaces (never modifies them).
class TuningApiImpl : public QObject, public pet::ITuningApi
{
    Q_OBJECT
public:
    // instanceManager must outlive this object (service tree guarantees
    // it; null is tolerated → every op returns NotFound, degraded wiring).
    TuningApiImpl(InstanceManager* instanceManager, QObject* parent);

    // Test seam for the mount packId resolution scan: overrides the two
    // scan sources (renderer base dir + user packs dir). Empty values
    // (production default) resolve to defaultRendererDir() +
    // ConfigDir::userVoicePacksDir() — the same sources the voice-pack
    // page and the plugin voicePackApi use.
    void setPackScanDirs(const QString& rendererDir, const QString& userPacksDir);

    pet::PluginError setOpacity(const QString& uuid, double opacity) override;
    pet::PluginError setVolume(const QString& uuid, double volume) override;
    pet::PluginError setMuted(const QString& uuid, bool muted) override;
    pet::PluginError setFps(const QString& uuid, int fps) override;
    pet::PluginError playMotion(const QString& uuid, const QString& group,
                                int index) override;
    pet::PluginError setExpression(const QString& uuid,
                                   const QString& expressionId) override;
    pet::PluginError triggerHitArea(const QString& uuid,
                                    const QString& areaId) override;
    pet::PluginError mountVoicePack(const QString& uuid,
                                    const QString& packId) override;
    pet::PluginError unmountVoicePack(const QString& uuid) override;

private:
    // Shared prelude: NotFound for unknown uuids, Busy while a delete is
    // pending. Returns Ok with *out set to the session on success.
    pet::PluginError locateAlive(const QString& uuid, InstanceSession** out) const;
    // Linear scan over the manager's PUBLIC roster surface (same access
    // QML uses; sidebar-sized N).
    InstanceSession* sessionForUuid(const QString& uuid) const;
    // packId (directory name) → absolute pack dir via the host's own
    // dual-source pack scan; empty when unknown.
    QString resolvePackDir(const QString& packId) const;

    InstanceManager* m_instanceManager; // not owned (service tree)
    // Pack-resolution scan sources (empty = production defaults; test seam).
    QString m_packRendererDir;
    QString m_packUserDir;
};

// S5 (v1.3): real model-library read API — the pet::IModelApi behind
// queryApi(kModelApiId). Wraps the pure ModelScanner + ModelInfoParser
// functions behind ONE scan cache (the same instance the panel's
// ModelController consumes — one scan, not two). Read-only family: not
// capability- or switch-gated; a host without the shared impl wired
// answers queryApi with nullptr (no stub — there is no capability story
// to report for a read).
class ModelApiImpl : public QObject, public pet::IModelApi
{
    Q_OBJECT
public:
    ModelApiImpl(QObject* parent = nullptr);

    // Injection seam mirroring ModelController's existing one: the
    // renderer BASE dir (Resources/Models is appended internally);
    // triggers a rescan so the cache is populated before the first
    // availableModels() call.
    void setRendererDir(const QString& dir);

    QVector<pet::ModelSummary> availableModels() override;
    void refreshScan() override;
    pet::PluginError modelInfo(const QString& name, pet::ModelSummary* out) override;

    // The scanned directory (<rendererDir>/Resources/Models); empty when
    // no renderer dir is known (ModelController re-exports this for the
    // page header hint + empty state).
    QString modelsDir() const { return m_modelsDir; }

private:
    void rescan();

    QString m_rendererDir;
    QString m_modelsDir;
    QList<pet::ModelSummary> m_models; // scan cache, scan order
};

// S6 (v1.3): real panel-settings write API — the pet::ISettingsApi
// behind queryApi(kSettingsApiId). Every setter takes the SAME
// load-modify-save path the panel's own PanelConfigController uses
// (PanelStateManager over the shared DatabaseManager; the other
// PanelConfig fields always survive the write). autoLaunchSystem is
// deliberately absent (host-shell capability — see ISettingsApi.hpp).
class SettingsApiImpl : public QObject, public pet::ISettingsApi
{
    Q_OBJECT
public:
    // configDir: the config root (ConfigDir::configDir() in production,
    // injected temp path in tests). DatabaseManager optional — without
    // it each load-modify-save opens its own connection, exactly like
    // PanelStateManager's fallback.
    SettingsApiImpl(const QString& configDir, QObject* parent = nullptr);
    void setDatabase(DatabaseManager* db) { m_db = db; }

    pet::PluginError setCloseAction(const QString& action) override;
    pet::PluginError setConfirmOnExit(bool enabled) override;
    pet::PluginError setStartMinimized(bool enabled) override;
    pet::PluginError setDefaultModelName(const QString& name) override;

private:
    // Load-modify-save over the panel_config store (same discipline as
    // PanelConfigController::updateField). Returns true on save success.
    bool updateField(const std::function<void(PanelConfig&)>& mutator);

    QString m_configDir;
    DatabaseManager* m_db = nullptr; // shared backend, not owned
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
    pet::IVoicePackApi& voicePackApi() override
    {
        return m_sharedVoicePackApi != nullptr ? *m_sharedVoicePackApi
                                               : m_voicePackApi;
    }
    pet::IUiApi& uiApi() override { return m_uiApi; }
    pet::IDownloadApi& downloadApi() override { return m_downloadApi; }
    QString pluginConfigDir() override;
    void log(pet::PluginLogLevel level, const QString& message) override;
    pet::IExtApi* queryApi(const char* apiId, int minVersion) override;

    // ── v1.3 (S5/S6) shared-implementation injection ─────────────────────
    // Called by PluginHost::initializeAll right after construction (the
    // ctor signature stays frozen for the existing direct-construction
    // tests). Null (the default) keeps the per-context fallbacks: tuning
    // and settings degrade to their capability stubs, the model family
    // answers nullptr ("feature absent"), the voice-pack family keeps
    // the per-context scan-everything impl.
    void setTuningApi(pet::ITuningApi* tuningApi) { m_tuningApi = tuningApi; }
    void setModelApi(pet::IModelApi* modelApi) { m_modelApi = modelApi; }
    void setSettingsApi(pet::ISettingsApi* settingsApi) { m_settingsApi = settingsApi; }
    void setVoicePackApi(pet::IVoicePackApi* voicePackApi) { m_sharedVoicePackApi = voicePackApi; }

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
    VoicePackApiImpl m_voicePackApi;          // per-context fallback
    pet::IVoicePackApi* m_sharedVoicePackApi = nullptr; // v1.3 shared, not owned
    DownloadApiAdapter m_downloadApi;
    // v1.3 (S5/S6): shared tuning/model/settings implementations (service
    // tree / host fallback), not owned; null → stub (tuning, settings) or
    // nullptr (model). Tuning/settings are ALSO capability-gated
    // (precomputed below, same discipline as instance_lifecycle); the
    // model family is a read → ungated.
    pet::ITuningApi* m_tuningApi = nullptr;
    pet::IModelApi* m_modelApi = nullptr;
    pet::ISettingsApi* m_settingsApi = nullptr;
    TuningApiStub m_tuningStub;
    SettingsApiStub m_settingsStub;
    bool m_instanceTuningGranted = false;
    bool m_settingsWriteGranted = false;
};

} // namespace core
