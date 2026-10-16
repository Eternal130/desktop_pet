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
#include <memory>
#include <optional>

#include "api/IDownloadApi.hpp"
#include "api/IEventApi.hpp"
#include "api/IInstanceApi.hpp"
#include "api/IInstanceControlApi.hpp"
#include "api/ILayoutApi.hpp"
#include "api/IModelApi.hpp"
#include "api/IMonitorApi.hpp"
#include "api/IPluginContext.hpp"
#include "api/IResourceScopeApi.hpp"
#include "api/IRuleApi.hpp"
#include "api/ISettingsApi.hpp"
#include "api/ITuningApi.hpp"
#include "api/IUiApi.hpp"
#include "core/PluginCapabilityStubs.hpp"
#include "core/RuleEngineService.hpp"

// InstanceManager + NotificationStreamController are GLOBAL-namespace
// classes (mixed-namespace codebase); declared outside namespace core.
class InstanceManager;
class NotificationStreamController;
class InstanceSession;
class DatabaseManager;
struct PanelConfig;
struct MonitorSnapshot; // ui/MonitorDataModel.hpp (MonitorApiImpl seam)

namespace core {

class DownloadService;
class EventBus;
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
    pet::PluginError setAvatar(const QString& uuid, const QString& avatar) override;

    // ── v1.13 host/plugin-internal scope-stamping seams (deliberately
    // NOT on the pet:: interface — the SDK InstanceSpec stays frozen;
    // the per-context InstanceControlApiProxy calls these with
    // callerId = its plugin id. callerId "" = the host channel,
    // byte-identical to the pre-v1.13 surface) ────────────────────────
    // create() + stamp the initial InstanceConfig.modelScope = callerId
    // (a plugin-created instance resolves its model in the plugin's
    // data domain from the first boot).
    pet::PluginError createAs(const QString& callerId,
                              const pet::InstanceSpec& spec, QString* outUuid);
    // loadModel() + scope: forwards to InstanceSession::loadModel(name,
    // callerId) — the wave-1 scoped switch (strict-domain resolution,
    // persist model_scope).
    pet::PluginError loadModelAs(const QString& callerId, const QString& uuid,
                                 const QString& modelName);

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

    // Test seam for the mount packId resolution scan: overrides the
    // scan sources (renderer base dir + user packs dir). Empty values
    // (production default) resolve to defaultRendererDir() +
    // ConfigDir::userVoicePacksDir() — the same sources the voice-pack
    // page and the plugin voicePackApi use.
    // v1.13: optional third seam — the plugin DATA root for the union
    // pack scan (mountVoicePackAs domain matching). Empty (the default)
    // resolves to ConfigDir::pluginDataRoot(); a non-existent injected
    // path simply contributes no plugin-domain packs.
    void setPackScanDirs(const QString& rendererDir, const QString& userPacksDir,
                         const QString& pluginDataRoot = QString());

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
    pet::PluginError setGraphicsBackend(const QString& uuid,
                                        const QString& backend) override;

    // ── v1.13 host/plugin-internal scope-stamping seam (per-context
    // TuningApiProxy calls this with callerId = its plugin id; the raw
    // pet:: surface keeps the legacy host-domain contract) ───────────
    // mountVoicePack() + scope: packId resolves against the UNION scan
    // (caller's OWN domain first, then the host domain — a plugin never
    // sees another plugin's packs), and the session mount persists
    // InstanceConfig.voicePackScope = the DOMAIN THE PACK ACTUALLY
    // RESOLVED IN (the caller's id on an own-domain hit; "" on a
    // host-domain pass-2 fallback — the session seam then sees ""). The
    // stamp mirrors the physical mount, not the caller identity.
    pet::PluginError mountVoicePackAs(const QString& callerId, const QString& uuid,
                                      const QString& packId);

private:
    // Shared prelude: NotFound for unknown uuids, Busy while a delete is
    // pending. Returns Ok with *out set to the session on success.
    pet::PluginError locateAlive(const QString& uuid, InstanceSession** out) const;
    // Linear scan over the manager's PUBLIC roster surface (same access
    // QML uses; sidebar-sized N).
    InstanceSession* sessionForUuid(const QString& uuid) const;
    // packId (directory name) → absolute pack dir via the host's UNION
    // pack scan (scanVoicePacksScoped): the caller's OWN plugin domain
    // wins, then the host domain (owner="" — the dual-source user-wins
    // merge, byte-identical to the pre-v1.13 resolution for callerId
    // ""). Domains other than the caller's are NEVER matched; empty when
    // unknown. When non-null, *matchedOwner receives the owner of the
    // winning domain — callerId on an own-domain hit, "" on a host-domain
    // fallback — so the caller can stamp scope DOMAIN-CORRECTLY (a pack
    // that resolved in the host domain must persist "").
    QString resolvePackDir(const QString& callerId, const QString& packId,
                           QString* matchedOwner = nullptr) const;

    InstanceManager* m_instanceManager; // not owned (service tree)
    // Pack-resolution scan sources (empty = production defaults; test seam).
    QString m_packRendererDir;
    QString m_packUserDir;
    QString m_packPluginDataRoot; // v1.13 (empty = ConfigDir::pluginDataRoot())
};

// S5 (v1.3): real model-library read API — the pet::IModelApi behind
// queryApi(kModelApiId). Wraps the pure ModelScanner + ModelInfoParser
// functions behind ONE scan cache (the same instance the panel's
// ModelController consumes — one scan, not two). Read-only family: not
// capability- or switch-gated; a host without the shared impl wired
// answers queryApi with nullptr (no stub — there is no capability story
// to report for a read).
//
// v1.13 (resource isolation): the cache is now the UNION scan
// (scanModelsScoped over the renderer tree + ConfigDir::pluginDataRoot()
// — or the injected test root). The pet.model PROJECTION stays
// host-domain-only (availableModels()/modelInfo() answer owner==""
// entries exclusively — SDK semantics narrowed to the host domain, which
// auto-isolates the host pages AND keeps legacy plugins seeing exactly
// the pre-v1.13 surface). The union half feeds the host-internal
// resolveModelPath() seam: PanelApplication binds every session's
// ModelPathResolverFn to it (strict-domain resolution, miss → QString()).
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

    // v1.13 test seam: the plugin DATA root for the union scan. Empty
    // (the default / production) resolves to ConfigDir::pluginDataRoot()
    // at scan time; tests inject a QTemporaryDir path so resolveModelPath
    // is verifiable without touching the real <dataDir>/PluginData/.
    void setPluginDataRoot(const QString& root);

    QVector<pet::ModelSummary> availableModels() override;
    void refreshScan() override;
    pet::PluginError modelInfo(const QString& name, pet::ModelSummary* out) override;

    // The scanned directory (<rendererDir>/Resources/Models); empty when
    // no renderer dir is known (ModelController re-exports this for the
    // page header hint + empty state).
    QString modelsDir() const { return m_modelsDir; }

    // ── v1.13 host-internal seam (strict-domain resolution, rev2
    // §3.4): absolute .model3.json path for (name, scope) out of the
    // union side index. scope empty → QString() (host domain — the
    // renderer's own Resources/ short-name resolution applies); scoped
    // miss → QString() (NEVER a host-domain fallback). ─────────────────
    QString resolveModelPath(const QString& name, const QString& scope) const;

private:
    void rescan();

    // One union-scan entry (host + every plugin data domain).
    struct Entry {
        QString name;
        QString owner;         // "" = host domain; else the plugin id
        QString modelJsonPath; // absolute <Name>.model3.json path
        pet::ModelSummary summary;
    };

    QString m_rendererDir;
    QString m_modelsDir;
    QString m_pluginDataRoot;      // empty → ConfigDir::pluginDataRoot()
    QList<pet::ModelSummary> m_models; // HOST-domain projection (owner=="")
    QList<Entry> m_union;              // full union (resolveModelPath index)
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

    // v1.7 (S9) event-publisher seam: the shared pet::IEventApi every
    // successful settings write publishes pet.panel.settings_changed
    // onto (PANEL-LEVEL — instanceUuid ""). Interface pointer (NOT the
    // concrete impl) so tests inject fakes; plain C++ setter,
    // deliberately NOT a Q_INVOKABLE / Q_PROPERTY WRITE (the
    // api-boundary gate's pattern table stays untouched — the
    // InstanceSession::setEventPublisher / setDialogueSink discipline).
    // Null (the default) = publish nothing (pre-v1.7 behavior).
    void setEventPublisher(pet::IEventApi* publisher)
    { m_eventPublisher = publisher; }

    pet::PluginError setCloseAction(const QString& action) override;
    pet::PluginError setConfirmOnExit(bool enabled) override;
    pet::PluginError setStartMinimized(bool enabled) override;
    pet::PluginError setDefaultModelName(const QString& name) override;
    pet::PluginError setDefaultGraphicsBackend(const QString& backend) override;

private:
    // Load-modify-save over the panel_config store (same discipline as
    // PanelConfigController::updateField). Returns true on save success.
    bool updateField(const std::function<void(PanelConfig&)>& mutator);
    // v1.7 (S9): publish pet.panel.settings_changed {field, value} as a
    // PANEL-LEVEL host event. No-op without an injected publisher.
    void publishSettingsChanged(const QString& field, const QJsonValue& value);

    QString m_configDir;
    DatabaseManager* m_db = nullptr; // shared backend, not owned
    pet::IEventApi* m_eventPublisher = nullptr; // v1.7, not owned
};

// S7 (v1.4): real resource-monitor read API — the pet::IMonitorApi
// behind queryApi(kMonitorApiId). Read-only family (ungated, like
// pet.model): wraps each session's MonitorDataModel — the SAME
// 60-sample ring the Monitor page renders; zero MonitorDataModel
// changes (history()/latestSnapshot()/snapshotAppended are the existing
// public read surfaces). history() projects MonitorSnapshot → the flat
// MonitorSample POD (std::optional halves → -1 sentinel); subscribe
// fans out latestSnapshot() on the model's snapshotAppended signal with
// per-subscriber minIntervalMs merge throttling. The panel's own
// MonitorPage.qml keeps binding the live model object (approved
// host boundary — this family serves plugin consumers only).
class MonitorApiImpl : public QObject, public pet::IMonitorApi
{
    Q_OBJECT
public:
    // instanceManager must outlive this object (service tree guarantees
    // it; null is tolerated → history() is empty, subscribe() NotFound).
    MonitorApiImpl(InstanceManager* instanceManager, QObject* parent);

    QVector<pet::MonitorSample> history(const QString& uuid) override;
    pet::PluginError subscribe(const QString& uuid, pet::IMonitorObserver*,
                               const pet::MonitorSubscription&) override;
    void unsubscribe(const QString& uuid,
                     pet::IMonitorObserver*) override;

private:
    // One live subscription (flat value type; the observer is NOT owned).
    struct Subscription {
        QString uuid;
        pet::IMonitorObserver* observer = nullptr;
        int minIntervalMs = 0;
        qint64 lastPushMs = 0; // 0 → the first sample always pushes
    };

    // Attach the snapshotAppended wiring to one session's model (called
    // for sessions present at construction and for every row later
    // inserted — the InstanceApiImpl::observeSession pattern). The
    // connection dies with the sender (the session deleteLaters the
    // model with itself), so no manual teardown is needed.
    void observeSession(InstanceSession* session);
    // Throttled fanout of the model's LATEST snapshot to that uuid's
    // subscribers (the merge floor is per subscriber).
    void fanoutSample(const QString& uuid);
    // Locate the session whose uuid() == uuid (linear scan over the
    // manager's public roster surface; null when unknown/unwired).
    InstanceSession* sessionForUuid(const QString& uuid) const;
    // MonitorSnapshot → flat MonitorSample (optional halves → -1/0
    // sentinels; see MonitorSample's PluginTypes comment).
    static pet::MonitorSample projectSnapshot(
        const std::optional<MonitorSnapshot>& snapshot);

    InstanceManager* m_instanceManager = nullptr; // not owned (service tree)
    QVector<Subscription> m_subscriptions;
};

// v1.8: real layout family — the pet::ILayoutApi behind
// queryApi(kLayoutApiId). Constructed ONCE in the PanelApplication
// service tree (same sharing pattern as TuningApiImpl) and injected
// into every plugin context (via PluginHost) AND into the panel's own
// InstanceControlBridge layout writes — host UI and plugins go through
// the same vtable. getLayout projects the session's PERSISTED
// InstanceConfig layout halves (config snapshot, NOT live renderer
// state — live changes ride the pet.instance.layout_changed /
// window_resized bus events); resetLayout / setAutoStart forward to the
// existing InstanceSession surfaces (the same methods the detail page
// drove directly pre-v1.8). GUI thread, non-blocking,
// "accepted"-semantics; it only CALLS the existing surfaces.
class LayoutApiImpl : public QObject, public pet::ILayoutApi
{
    Q_OBJECT
public:
    // instanceManager must outlive this object (service tree guarantees
    // it; null is tolerated → every op returns NotFound, degraded wiring).
    LayoutApiImpl(InstanceManager* instanceManager, QObject* parent);

    pet::PluginError getLayout(const QString& uuid,
                               pet::LayoutInfo* out) override;
    pet::PluginError resetLayout(const QString& uuid) override;
    pet::PluginError setAutoStart(const QString& uuid, bool enabled) override;

private:
    // Shared prelude (the TuningApiImpl::locateAlive discipline):
    // NotFound for unknown uuids, Busy while a delete is pending.
    pet::PluginError locateAlive(const QString& uuid,
                                 InstanceSession** out) const;
    // Linear scan over the manager's PUBLIC roster surface (same access
    // QML uses; sidebar-sized N).
    InstanceSession* sessionForUuid(const QString& uuid) const;

    InstanceManager* m_instanceManager; // not owned (service tree)
};

// Real download API (P5): forwards to the host DownloadService when the
// plugin's manifest grants the "network" capability; without the grant
// every method fails with PluginError::Capability (§B.4 capability gate —
// the pre-P5 permanent stub is now CONDITIONAL, same loud-failure shape).
//
// v1.13 (rev2 §3.1/§3.5 api_version routing gate): the adapter receives
// the plugin's MANIFEST api_version. ≥ 1.13 → installArchive forwards to
// the 3-arg DownloadService overload with owner = the plugin id (installs
// land in the plugin's data domain); < 1.13 → owner "" (the legacy host
// install roots, byte-identical to the pre-v1.13 behavior). Host-internal
// callers of the raw DownloadService always use the legacy channel.
class DownloadApiAdapter final : public QObject, public pet::IDownloadApi
{
    Q_OBJECT
public:
    DownloadApiAdapter(QString pluginId, bool networkGranted,
                        int manifestApiMajor, int manifestApiMinor,
                        DownloadService* service, QObject* parent);

    pet::JobId start(const pet::DownloadRequest& request,
                      pet::DownloadListener* listener) override;
    void cancel(pet::JobId job) override;
    pet::PluginError installArchive(pet::JobId job, const pet::InstallSpec& spec) override;

private:
    QString m_pluginId;
    bool m_networkGranted = false;
    bool m_domainRouting = false; // v1.13: manifest api_version ≥ 1.13
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

// S8a (v1.5): real event-bus API — the shared pet::IEventApi behind
// queryApi(kEventsApiId). Constructed ONCE in the PanelApplication
// service tree over the shared core::EventBus (same sharing pattern as
// MonitorApiImpl) and injected into every plugin context (via
// PluginHost) AND into the host's own emission points (InstanceSession
// publishers). Plugins never see THIS object directly — queryApi hands
// each context a per-plugin EventApiProxy (below) that enforces the
// publisher-prefix rule; the host emission points hold the raw Impl
// precisely because the host IS allowed to publish pet.*.
class EventApiImpl : public QObject, public pet::IEventApi
{
    Q_OBJECT
public:
    // bus must outlive this object (service-tree registration order in
    // PanelApplication / PluginHost fallback guarantees it).
    explicit EventApiImpl(EventBus* bus, QObject* parent = nullptr);

    // pet::IEventApi — the HOST channel. subscribe books publisherId ""
    // (host bookkeeping; the unsubscribe sweep never matches "" — plugins
    // always book their own id). publish forwards publisherId VERBATIM
    // (the host emitter owns the field — no overwrite).
    pet::PluginError subscribe(const QString& eventId,
                               pet::IEventListener*,
                               pet::EventPriority) override;
    void unsubscribe(const QString& eventId,
                     pet::IEventListener*) override;
    pet::PluginError publish(pet::Event& event) override;

    // ── Host/plugin-internal seams (deliberately NOT on the pet::
    // interface — plugins must not be able to book a foreign publisherId
    // or sweep another plugin's subscriptions) ────────────────────────
    // Proxy subscribe: books `publisherId` so the unload sweep can find
    // these entries. Same validation/merge rules as subscribe().
    pet::PluginError subscribeAs(const QString& publisherId,
                                 const QString& eventId,
                                 pet::IEventListener*,
                                 pet::EventPriority);
    // Plugin-unload sweep (PluginHost::shutdownAll): drops every
    // subscription booked under publisherId; returns the removal count
    // (the host WARNs when a plugin needed sweeping).
    int unsubscribeByPublisher(const QString& publisherId);

    // The wrapped bus (EventApiProxy + host emission points never need
    // this; exposed for tests).
    EventBus* bus() const { return m_bus; }

private:
    EventBus* m_bus = nullptr; // not owned (service tree / host fallback)
};

// S8a (v1.5): the per-plugin publishing proxy served through
// queryApi("pet.events"). Enforces the anti-forgery rules the shared
// Impl cannot (it serves the host too):
//   - publish force-stamps publisherId with the context's plugin id;
//   - publish rejects any channel OUTSIDE "plugin.<ownId>.*" with
//     InvalidArgument and ZERO dispatch (a plugin cannot impersonate
//     pet.* host channels);
//   - subscribe/unsubscribe forward to the shared bus with the plugin
//     id booked as publisherId (the unload-sweep accounting).
// Plain class (no QObject) — a value member of PluginContextImpl.
class EventApiProxy final : public pet::IEventApi
{
public:
    EventApiProxy(const QString& pluginId, EventApiImpl* shared);

    pet::PluginError subscribe(const QString& eventId,
                               pet::IEventListener*,
                               pet::EventPriority) override;
    void unsubscribe(const QString& eventId,
                     pet::IEventListener*) override;
    pet::PluginError publish(pet::Event& event) override;

private:
    QString m_pluginId;
    EventApiImpl* m_shared = nullptr; // not owned; null-safe (no wiring)
};

// S8f (v1.6): shared rules-registration API — the pet::IRuleApi behind
// queryApi(kRulesApiId) over the shared core::RuleEngineService.
// Constructed ONCE in the PanelApplication service tree (AFTER the
// RuleEngineService it borrows) and injected into every plugin context
// via PluginHost. Plugins never see THIS object directly — queryApi
// hands each context a per-plugin RuleApiProxy (below) that stamps the
// "plugin.<ownId>." id prefix + RuleScope::Plugin; the raw impl's own
// pet::IRuleApi surface is the HOST channel (Panel scope, empty owner —
// the file-loading pipeline remains the primary host surface, the raw
// channel exists for symmetry + tests).
class RuleApiImpl : public QObject, public pet::IRuleApi
{
    Q_OBJECT
public:
    // ruleEngine must outlive this object (service-tree registration
    // order guarantees it); null is tolerated — registerRule returns
    // Generic, unregisterRule no-ops (degraded wiring, never a crash).
    explicit RuleApiImpl(RuleEngineService* ruleEngine,
                         QObject* parent = nullptr);

    // pet::IRuleApi — the HOST channel: Panel scope, empty owner (the
    // same stamp the panel-kv loading pipeline applies).
    pet::PluginError registerRule(const QJsonObject& rule) override;
    void unregisterRule(const QString& ruleId) override;

    // ── Host/plugin-internal seams (deliberately NOT on the pet::
    // interface — a plugin must not register rules under a foreign
    // owner or sweep another plugin's set) ────────────────────────
    // Proxy register: scope=Plugin + ownerUuid=pluginId; the service
    // namespaces the stored id "plugin.<pluginId>.<bareId>". A stored
    // id that already exists under ANOTHER owner is refused
    // (InvalidArgument — dotted plugin ids can nest, so the id alone
    // is not proof of ownership). Rejections return InvalidArgument
    // with the structured reason routed through `log` (the plugin's
    // own context log).
    pet::PluginError registerPluginRule(
        const QString& pluginId, const QJsonObject& rule,
        const std::function<void(pet::PluginLogLevel, const QString&)>& log);
    // Proxy unregister: bare id → the plugin's OWN prefixed stored id,
    // with the stored rule's scope/ownerUuid re-verified before removal
    // (an id computed from a dotted-prefix neighbor can never pass).
    // Unknown id → no-op.
    void unregisterPluginRule(const QString& pluginId,
                              const QString& bareRuleId);
    // Plugin-unload sweep (PluginHost::shutdownAll): removes every rule
    // stored under "plugin.<pluginId>." and recomputes the bus
    // subscriptions. Returns the removal count (the host WARNs on
    // non-zero — the sweep is the safety net, not the contract).
    int sweepPluginRules(const QString& pluginId);

private:
    // Stored id for a plugin's bare rule id.
    static QString pluginRuleId(const QString& pluginId,
                                const QString& bareId);

    RuleEngineService* m_ruleEngine = nullptr; // not owned (service tree)
};

// S8f (v1.6): the per-plugin rules proxy served through
// queryApi("pet.rules"). Enforces the owner rules the shared Impl
// cannot (it serves the host too): every registration is forwarded with
// the context's plugin id as the owner (stored id prefix
// "plugin.<ownId>.", RuleScope::Plugin) and every unregister is
// re-stamped with the same prefix — a plugin can only ever touch its
// own rules. Plain class (no QObject) — a value member of
// PluginContextImpl (EventApiProxy pattern).
class RuleApiProxy final : public pet::IRuleApi
{
public:
    // log: the context's own log() (rejection reasons surface under the
    // plugin's id prefix); may be null.
    RuleApiProxy(const QString& pluginId, RuleApiImpl* shared,
                 std::function<void(pet::PluginLogLevel, const QString&)> log);

    pet::PluginError registerRule(const QJsonObject& rule) override;
    void unregisterRule(const QString& ruleId) override;

private:
    QString m_pluginId;
    RuleApiImpl* m_shared = nullptr; // not owned; null-safe (no wiring)
    std::function<void(pet::PluginLogLevel, const QString&)> m_log;
};

// v1.13: the per-plugin instance-control proxy served through
// queryApi("pet.instance_control") for api_version ≥ 1.13 plugin
// contexts. The shared InstanceControlApiImpl serves the HOST channel
// (the panel's own RosterApiModel/InstanceControlBridge hold the raw
// impl — host UI stays ungated + unscoped by design); the proxy adds
// ONLY scope stamping on the two resource-bearing ops: create stamps the
// initial InstanceConfig.modelScope and loadModel routes through the
// scoped session switch. Capability + write-switch gating are unchanged
// (the route still falls back to the stub exactly as before — the proxy
// only wraps the REAL impl). Plain class (no QObject) — built by
// PluginContextImpl when domain routing is on. For < 1.13 manifests the
// context keeps serving the RAW impl (byte-identical legacy).
class InstanceControlApiProxy final : public pet::IInstanceControlApi
{
public:
    InstanceControlApiProxy(const QString& pluginId,
                            InstanceControlApiImpl* shared);

    pet::PluginError create(const pet::InstanceSpec& spec, QString* outUuid) override;
    pet::PluginError remove(const QString& uuid) override;
    pet::PluginError start(const QString& uuid) override;
    pet::PluginError stop(const QString& uuid) override;
    pet::PluginError restart(const QString& uuid) override;
    pet::PluginError loadModel(const QString& uuid,
                               const QString& modelName) override;
    pet::PluginError setAvatar(const QString& uuid, const QString& avatar) override;

private:
    QString m_pluginId;
    InstanceControlApiImpl* m_shared = nullptr; // not owned; null-safe
};

// v1.13: the per-plugin tuning proxy served through
// queryApi("pet.instance_tuning") for api_version ≥ 1.13 plugin
// contexts. Same shape as InstanceControlApiProxy: the host UI
// (VoicePackController / InstanceControlBridge) keeps the raw impl; the
// proxy stamps callerId on mountVoicePack only (the one resource-bearing
// tuning op — packId resolves against the union scan with the caller's
// OWN domain first, and the session persists voicePackScope). The other
// nine ops forward verbatim.
class TuningApiProxy final : public pet::ITuningApi
{
public:
    TuningApiProxy(const QString& pluginId, TuningApiImpl* shared);

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
    pet::PluginError setGraphicsBackend(const QString& uuid,
                                        const QString& backend) override;

private:
    QString m_pluginId;
    TuningApiImpl* m_shared = nullptr; // not owned; null-safe
};

// v1.13 (rev2 §3.3): shared resource-scope family — the pet::
// IResourceScopeApi behind queryApi(kResourceScopeApiId). Constructed
// ONCE in the PanelApplication service tree (same sharing pattern as
// ModelApiImpl) and injected into every plugin context (via PluginHost).
// Plugins never see THIS object directly — queryApi hands each context a
// per-plugin ResourceScopeApiProxy that stamps callerId; the owner check
// (ownerId == callerId, both non-empty) is the family's ONLY boundary
// (无能力词，属主校验即边界 — the pet.rules precedent). Backing data is the
// PURE directory-traversal union scans (scanModelsScoped /
// scanVoicePacksScoped) — never the plugin registry (rev2 §3.2c).
// Reads scan lazily per call (the VoicePackApiImpl::listPacks cost
// class); refreshScan() re-runs the wired ModelApiImpl cache alongside.
class ResourceScopeApiImpl : public QObject, public pet::IResourceScopeApi
{
    Q_OBJECT
public:
    ResourceScopeApiImpl(QObject* parent = nullptr);

    // Injection seam mirroring ModelApiImpl's: the renderer BASE dir
    // (Resources/{Models,VoicePacks} appended internally). Empty (the
    // default) resolves to QCoreApplication::applicationDirPath() at
    // scan time — the DownloadService modelsInstallRoot convention.
    void setRendererDir(const QString& dir);

    // Test seam: overrides all three scan roots at once (renderer base
    // dir + user packs dir + plugin DATA root). Empty values resolve to
    // the production defaults (applicationDirPath() /
    // ConfigDir::userVoicePacksDir() / ConfigDir::pluginDataRoot()).
    void setScanDirs(const QString& rendererDir, const QString& userPacksDir,
                     const QString& pluginDataRoot);

    // Cross-wiring seam (PanelApplication): refreshScan() also refreshes
    // the shared ModelApiImpl cache so the pet.model family and the
    // library page observe resource removals/installs. Null (default) =
    // resource refreshes stay local.
    void setModelApi(ModelApiImpl* modelApi) { m_modelApi = modelApi; }

    // pet::IResourceScopeApi — the HOST channel. The host domain is not
    // addressable here: ownerId "" fails the owner check (reads come
    // back empty, removeScoped → InvalidArgument). Host-internal
    // callers use the *As seams with an explicit callerId.
    QVector<pet::ScopedModelInfo> scopedModels(const QString& ownerId) override;
    QVector<pet::ScopedPackInfo> scopedPacks(const QString& ownerId) override;
    pet::PluginError refreshScan() override;
    pet::PluginError removeScoped(const QString& ownerId, const QString& name,
                                  bool isModel) override;

    // ── Host/plugin-internal seams (explicit caller identity — the
    // EventApiImpl::subscribeAs discipline) ──────────────────────────
    QVector<pet::ScopedModelInfo> scopedModelsAs(const QString& callerId,
                                                 const QString& ownerId);
    QVector<pet::ScopedPackInfo> scopedPacksAs(const QString& callerId,
                                               const QString& ownerId);
    pet::PluginError removeScopedAs(const QString& callerId, const QString& ownerId,
                                    const QString& name, bool isModel);

private:
    // Effective scan roots (empty members → production defaults).
    QString rendererDirOrDefault() const;
    QString userPacksDirOrDefault() const;
    QString pluginDataRootOrDefault() const;
    // <pluginDataRoot>/<ownerId>/Models|VoicePacks (derived from the
    // INJECTED root so tests never touch the real ConfigDir paths; in
    // production the root IS ConfigDir::pluginDataRoot()).
    QString domainModelsDir(const QString& ownerId) const;
    QString domainPacksDir(const QString& ownerId) const;

    QString m_rendererDir;      // empty → applicationDirPath()
    QString m_userPacksDir;     // empty → ConfigDir::userVoicePacksDir()
    QString m_pluginDataRoot;   // empty → ConfigDir::pluginDataRoot()
    ModelApiImpl* m_modelApi = nullptr; // not owned (cross-refresh)
};

// v1.13: the per-plugin resource-scope proxy served through
// queryApi("pet.resource_scope"). Forwards with callerId = the context's
// plugin id (the anti-forgery rule the shared Impl cannot enforce — it
// serves every context). Ungated read-family routing: null impl →
// nullptr ("feature absent"). Plain class (no QObject) — a value member
// of PluginContextImpl (EventApiProxy pattern).
class ResourceScopeApiProxy final : public pet::IResourceScopeApi
{
public:
    ResourceScopeApiProxy(const QString& pluginId, ResourceScopeApiImpl* shared);

    QVector<pet::ScopedModelInfo> scopedModels(const QString& ownerId) override;
    QVector<pet::ScopedPackInfo> scopedPacks(const QString& ownerId) override;
    pet::PluginError refreshScan() override;
    pet::PluginError removeScoped(const QString& ownerId, const QString& name,
                                  bool isModel) override;

private:
    QString m_pluginId;
    ResourceScopeApiImpl* m_shared = nullptr; // not owned; null-safe
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
    //
    // v1.13 (rev2 §3.1/§3.5): manifestApiMajor/manifestApiMinor carry the
    // plugin's MANIFEST api_version (PluginRegistry's parsed fields) into
    // the context. ≥ 1.13 → domain routing (download install owner,
    // voicePackApi installPath advertisement, instance-control/tuning
    // scope stamping via the per-context proxies); < 1.13 (the DEFAULT —
    // direct-construction tests, legacy manifests) → owner=""/host
    // domain everywhere, byte-identical to pre-v1.13. Appended AFTER
    // parent with defaults so every existing call site keeps compiling.
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
                      QObject* parent,
                      int manifestApiMajor = 0,
                      int manifestApiMinor = 0);

    // pet::IPluginContext
    pet::IInstanceApi& instanceApi() override { return *m_instanceApi; }
    pet::IVoicePackApi& voicePackApi() override
    {
        // v1.13: an injected shared impl is served through the per-context
        // proxy so installPath() honors THIS context's api_version gate;
        // without one, the per-context fallback impl (constructed with the
        // same gate) keeps serving.
        if (m_sharedVoicePackApi != nullptr && m_voicePackProxy != nullptr)
            return *m_voicePackProxy;
        return m_voicePackApi;
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
    void setTuningApi(TuningApiImpl* tuningApi); // v1.13: concrete type — the proxy needs mountVoicePackAs
    void setModelApi(pet::IModelApi* modelApi) { m_modelApi = modelApi; }
    void setSettingsApi(pet::ISettingsApi* settingsApi) { m_settingsApi = settingsApi; }
    void setVoicePackApi(pet::IVoicePackApi* voicePackApi);
    // ── v1.4 (S7) shared-implementation injection ─────────────────────
    // Same seam + semantics as the model family (a read): null → queryApi
    // answers nullptr ("feature absent"); injected → the shared impl.
    void setMonitorApi(pet::IMonitorApi* monitorApi) { m_monitorApi = monitorApi; }
    // ── v1.5 (S8a) shared-implementation injection ─────────────────────
    // The SHARED EventApiImpl served through queryApi("pet.events") —
    // never raw: the context builds its per-plugin EventApiProxy from
    // (m_pluginId, shared) so every plugin publish is prefix-checked and
    // publisher-stamped. Null (the default) → queryApi answers nullptr
    // ("feature absent"); injected → the proxy.
    void setEventApi(EventApiImpl* eventApi);
    // ── v1.6 (S8f) shared-implementation injection ─────────────────────
    // The SHARED RuleApiImpl served through queryApi("pet.rules") —
    // never raw: the context builds its per-plugin RuleApiProxy from
    // (m_pluginId, shared, own log) so every registration is
    // owner-stamped and every rejection reason lands in the plugin's
    // own context log. Null (the default) → queryApi answers nullptr
    // ("feature absent"); injected → the proxy.
    void setRuleApi(RuleApiImpl* ruleApi);
    // ── v1.8 shared-implementation injection ───────────────────────────
    // The SHARED LayoutApiImpl served through queryApi("pet.layout")
    // when the plugin's manifest grants "instance_tuning" AND the write
    // switch is on (the write-family matrix). Null (the default) keeps
    // the capability stub semantics (the frozen-ctor-signature
    // discipline of the v1.3/v1.4 setters).
    void setLayoutApi(pet::ILayoutApi* layoutApi) { m_layoutApi = layoutApi; }
    // ── v1.13 shared-implementation injection ───────────────────────────
    // The SHARED ResourceScopeApiImpl served through
    // queryApi("pet.resource_scope") — never raw: the context builds its
    // per-plugin ResourceScopeApiProxy from (m_pluginId, shared) so every
    // read/write is caller-stamped and owner-checked. Null (the default)
    // → queryApi answers nullptr ("feature absent"); injected → the
    // proxy. Ungated read-family routing (the pet.model precedent).
    void setResourceScopeApi(ResourceScopeApiImpl* resourceScopeApi);

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
    std::unique_ptr<class VoicePackApiProxy> m_voicePackProxy; // v1.13 installPath gate
    DownloadApiAdapter m_downloadApi;
    // v1.3 (S5/S6): shared tuning/model/settings implementations (service
    // tree / host fallback), not owned; null → stub (tuning, settings) or
    // nullptr (model). Tuning/settings are ALSO capability-gated
    // (precomputed below, same discipline as instance_lifecycle); the
    // model family is a read → ungated. v1.13: m_tuningApi is the
    // CONCRETE impl (the per-context TuningApiProxy needs the
    // mountVoicePackAs seam).
    TuningApiImpl* m_tuningApi = nullptr;
    pet::IModelApi* m_modelApi = nullptr;
    pet::ISettingsApi* m_settingsApi = nullptr;
    TuningApiStub m_tuningStub;
    SettingsApiStub m_settingsStub;
    bool m_instanceTuningGranted = false;
    bool m_settingsWriteGranted = false;
    // v1.13 (rev2 §3.1): manifest api_version ≥ 1.13 → per-context scope-
    // stamping proxies wrap the shared control/tuning impls; below → the
    // raw impls (byte-identical legacy routing). Set in the ctor body.
    bool m_domainRouting = false;
    std::unique_ptr<InstanceControlApiProxy> m_instanceControlProxy;
    std::unique_ptr<TuningApiProxy> m_tuningProxy;
    // v1.13: shared resource-scope implementation, not owned. Plugins are
    // served through m_resourceScopeProxy (caller stamping + owner
    // check); null → nullptr ("feature absent").
    ResourceScopeApiImpl* m_resourceScopeApi = nullptr;
    std::unique_ptr<ResourceScopeApiProxy> m_resourceScopeProxy;
    // v1.4 (S7): shared monitor read implementation, not owned; null →
    // nullptr ("feature absent" — a read family has no capability story).
    pet::IMonitorApi* m_monitorApi = nullptr;
    // v1.5 (S8a): shared event-bus implementation (service tree / host
    // fallback), not owned. Plugins are served through m_eventProxy
    // (publisher-prefix enforcement); null → nullptr ("feature absent").
    EventApiImpl* m_sharedEventApi = nullptr;
    std::unique_ptr<EventApiProxy> m_eventProxy;
    // v1.6 (S8f): shared rules-registration implementation (service tree
    // / host fallback), not owned. Plugins are served through m_ruleProxy
    // (owner-prefix stamping); null → nullptr ("feature absent").
    RuleApiImpl* m_sharedRuleApi = nullptr;
    std::unique_ptr<RuleApiProxy> m_ruleProxy;
    // v1.8: shared layout-family implementation (service tree / host
    // fallback), not owned; null → capability stub (the write-family
    // matrix — pet.layout is instance_tuning + write-switch gated, the
    // same routing as pet.instance_tuning).
    pet::ILayoutApi* m_layoutApi = nullptr;
    LayoutApiStub m_layoutStub;
};

} // namespace core
