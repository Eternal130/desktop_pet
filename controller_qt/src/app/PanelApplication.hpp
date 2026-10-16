#pragma once

#include <QObject>
#include <QPointer>

#include "core/PanelConfig.hpp"
#include "core/PluginRegistry.hpp"

class DatabaseManager;
class PanelStateManager;
class WsServer;
class PendingRequests;
class InstanceManager;
class StartupSalvo;
class ProcessManager;
class QLocalServer;
class QWindow;

namespace core {
class DownloadService;
class EventBus;
class EventApiImpl;
class InstanceApiImpl;
class InstanceControlApiImpl;
class LayoutApiImpl;
class ModelApiImpl;
class MonitorApiImpl;
class ResourceScopeApiImpl;
class RuleApiImpl;
class RuleEngineService;
class SettingsApiImpl;
class TuningApiImpl;
class VoicePackApiImpl;
class PluginHost;
}

// PanelApplication — the application service tree carved out of main()
// (P3/M2, docs/refactor §B.2): DatabaseManager, PanelStateManager, WsServer,
// PendingRequests, InstanceManager plus all wiring between them, constructed
// and connected in main()'s original declaration/connect order.
//
// Lifecycle: a QObject parent tree REPLACES the old stack-reverse discipline
// for these five — each service is heap-allocated with `this` as its Qt
// parent. main() declares the PanelApplication before every UI bridge and
// the engine, so by stack-reverse the whole tree is destroyed after the QML
// context ("services outlive QML" invariant preserved at the main() level).
//
// DESTRUCTION-ORDER FACT (2026-10 teardown diagnosis, ASAN-verified): Qt
// >= 6.6 deletes QObject children in REGISTRATION order — FIRST registered
// dies FIRST. The §B.2 v2 annotation that claimed registration-REVERSE had
// the direction of the Qt 6.6 change backwards. Under the real (forward)
// order the default cascade frees early-registered API impls
// (EventApiImpl/InstanceApiImpl) BEFORE later destructors that still call
// into them (~InstanceSession → unsubscribe, ~RuleEngineService →
// unsubscribeInstances) — a heap-use-after-free at exit (release-build
// flakiness: tcache clobbers the freed vptr slot → sporadic SEGV / heap
// corruption abort). ~PanelApplication() therefore explicitly deletes the
// cross-referencing services FIRST; see its body. Per-member registration
// sites in the .cpp note this where relevant.
//
// No Q_OBJECT: the class adds no signals/slots of its own; it is a pure
// composition + ownership scope (AUTOMOC therefore not required).
class PanelApplication : public QObject
{
public:
    explicit PanelApplication(QObject* parent = nullptr);
    ~PanelApplication() override;

    // Ownership seam for main() (context-property registration stays there
    // until M3). The service objects are parented to this PanelApplication
    // and die with it — callers must not delete them.
    DatabaseManager& databaseManager();
    PanelStateManager& panelStateManager();
    WsServer* wsServer();
    PendingRequests* pendingRequests();
    InstanceManager* instanceManager();

    // S3 roster dogfooding: THE shared pet::IInstanceApi implementation —
    // constructed here (right after the roster it observes) and injected
    // both into every plugin context (via PluginHost) and into the panel's
    // own RosterApiModel (via PanelUiBoot). Host UI and plugins therefore
    // go through the same object and the same vtable; dies AFTER the
    // InstanceManager in the default cascade (Qt ≥ 6.6 registration order —
    // see class comment). Safe: ~InstanceApiImpl makes no cross-calls, and
    // its session NOTIFY connections drop automatically when the sessions
    // die with the manager.
    core::InstanceApiImpl* instanceApi();

    // S2 (v1.2): THE shared pet::IInstanceControlApi implementation —
    // same construction/sharing pattern as instanceApi(): served to
    // plugins through PluginHost → queryApi("pet.instance_control")
    // (capability + plugin_write_enabled gated at the context) and to the
    // panel's own RosterApiModel write bridge (NOT gated — the host UI is
    // the host). Registered after m_instanceApi → dies AFTER it and AFTER
    // the InstanceManager (Qt ≥ 6.6 registration order — the old
    // registration-reverse claim had the direction backwards). Safe:
    // ~InstanceControlApiImpl makes no destructor cross-calls.
    core::InstanceControlApiImpl* instanceControlApi();

    // S5 (v1.3): THE shared pet::ITuningApi implementation — same
    // construction/sharing pattern as instanceControlApi(): served to
    // plugins through PluginHost → queryApi("pet.instance_tuning")
    // (capability + plugin_write_enabled gated at the context) and to
    // the panel's own InstanceControlBridge + VoicePackController mount
    // path (NOT gated — the host UI is the host). Registered after
    // m_instanceControlApi → dies AFTER it (Qt ≥ 6.6 registration order —
    // the old registration-reverse claim had the direction backwards);
    // ~TuningApiImpl makes no destructor cross-calls — safe.
    core::TuningApiImpl* tuningApi();

    // S5 (v1.3): THE shared pet::IModelApi implementation — ONE model
    // scan cache serving the plugin family (queryApi("pet.model"), a
    // read → ungated) AND the panel's own ModelController (the library
    // page consumes this same object). Same lifetime discipline as
    // tuningApi(). The renderer dir is injected later by PanelUiBoot
    // (setRendererDir, mirroring the ModelController seam).
    core::ModelApiImpl* modelApi();

    // S6 (v1.3): THE shared pet::ISettingsApi implementation — the SAME
    // load-modify-save persistence path the panel's own
    // PanelConfigController writes land on (it forwards here since S6).
    // Served to plugins through queryApi("pet.settings")
    // (settings_write + switch gated at the context). Same lifetime
    // discipline as tuningApi().
    core::SettingsApiImpl* settingsApi();

    // S6 (v1.3): THE shared voice-pack read API (core::VoicePackApiImpl
    // — the pet::IVoicePackApi handed to every plugin context). Its pack
    // source + refresh seams are wired by PanelUiBoot onto
    // VoicePackController's single scan; packsChanged fanout reaches the
    // IPackListObserver family. Same lifetime discipline as tuningApi().
    core::VoicePackApiImpl* voicePackApi();

    // S7 (v1.4): THE shared pet::IMonitorApi implementation — wraps each
    // session's MonitorDataModel ring buffer (the same samples the
    // Monitor page renders). Served to plugins through
    // queryApi("pet.monitor") (a read → ungated). The panel's own
    // MonitorPage keeps binding the live model object (approved host
    // boundary). Same lifetime discipline as tuningApi().
    core::MonitorApiImpl* monitorApi();

    // v1.8: THE shared pet::ILayoutApi implementation — config-snapshot
    // layout reads (getLayout) + the resetLayout / setAutoStart writes.
    // Served to plugins through PluginHost → queryApi("pet.layout")
    // (instance_tuning + write-switch gated at the context) and to the
    // panel's own InstanceControlBridge layout Q_INVOKABLEs (NOT gated
    // — the host UI is the host; the pre-v1.8 direct session calls were
    // rebound onto it in the same change). Same lifetime discipline as
    // tuningApi().
    core::LayoutApiImpl* layoutApi();

    // S8a (v1.5): THE shared event bus + its pet::IEventApi face. The
    // bus carries the panel's bypass event publishes (InstanceSession's
    // WS-event handlers, the idle scheduler tick, pack mount/unmount —
    // all pure ADD-ONS with zero behavior change) and every plugin
    // subscription (queryApi("pet.events") → per-context proxy).
    // Registered BEFORE the InstanceManager (unlike the other shared
    // APIs, which follow it): sessions hold the publisher pointer, so
    // the bus must OUTLIVE every session. NOTE (2026-10 teardown fix):
    // registration position does NOT buy that — under the Qt ≥ 6.6
    // forward cascade the bus/API die BEFORE the manager's sessions
    // (the exit-path use-after-free). The guarantee now comes from
    // ~PanelApplication(), which deletes m_instanceManager (whose
    // sessions unsubscribe) while m_eventApi is still alive.
    core::EventBus* eventBus();
    core::EventApiImpl* eventApi();

    // S8e: the rule-engine host service — owns the RuleEngine core plus
    // its bus subscriptions (Low tier), activity tracking, EvalContext
    // assembly, action dispatch and the panel-kv + instance-blob loading
    // pipeline. Constructed after the shared v1.2–v1.4 API impls (it holds
    // eventApi/instanceApi/tuningApi + the DatabaseManager) and BEFORE the
    // plugin host. The bubble sink is late-injected by PanelUiBoot (the
    // NotificationStreamController is a UiBoot child); until then
    // notify_bubble actions fail with WARN, never crash. Registered after
    // m_monitorApi → dies AFTER it in the default cascade (Qt ≥ 6.6
    // registration order); its dtor calls the earlier-registered
    // instanceApi/eventApi (freed first — the exit-path use-after-free),
    // so it is deleted explicitly in ~PanelApplication() while they are
    // still alive.
    core::RuleEngineService* ruleEngineService();

    // S8f (v1.6): THE shared pet::IRuleApi implementation over the
    // RuleEngineService — served to plugins through PluginHost →
    // queryApi("pet.rules") (per-context owner-stamping proxy; the
    // family itself is deliberately ungated at query time — the owner
    // action gate fires at emission time inside the service).
    // Registered after m_ruleEngineService → dies AFTER it in the
    // default cascade (Qt ≥ 6.6 registration order — the borrowed service
    // pointer WOULD dangle); covered by ~PanelApplication(), which deletes
    // m_ruleApi BEFORE m_ruleEngineService explicitly.
    core::RuleApiImpl* ruleApi();

    // Plugin framework (P4, §B.6): the registry (entry store + state
    // machine) is created FIRST in the ctor; the host (initialize/shutdown
    // driver) LAST, so it can observe every earlier service. main()
    // registers static-plugin factories into the registry between
    // construction and PanelUiBoot; the host's initializeAll() runs after
    // the UI boot mounted the page model/manager (see main.cpp).
    core::PluginRegistry& pluginRegistry();
    core::PluginHost* pluginHost();

    // P5 (§B.4): the process-wide download chokepoint — constructed in the
    // service tree BEFORE the plugin host (which hands it to plugin
    // contexts, capability-gated). Staging <dataDir>/downloads/, install
    // root <dataDir>/VoicePacks/ (per-user, never the install tree). Its
    // destructor silently cancels active jobs — the exit path never blocks.
    core::DownloadService* downloadService();

    // v1.13 (rev2 §3.3): THE shared pet::IResourceScopeApi implementation —
    // the union resource scans (models + voice packs over the renderer tree
    // + <dataDir>/PluginData/). Served to plugins through PluginHost →
    // queryApi("pet.resource_scope") (per-context caller-stamping proxy;
    // ungated — the impl's owner check is the boundary). Same lifetime
    // discipline as modelApi(); cross-wired to it (resourceScope refresh
    // rides the shared model cache).
    core::ResourceScopeApiImpl* resourceScopeApi();

    // Snapshot loaded once at construction (the old `const PanelConfig
    // panelCfg = psm.load();` local in main), consumed for the initial*
    // context properties.
    const PanelConfig& panelConfig() const { return m_panelConfig; }

    // Legacy Phase-5 instance-0 holders (StartupSalvo + ProcessManager) and
    // their sender-injection wiring, moved from main() (M4). Call exactly
    // once, right after construction — same position the old main() stack
    // block sat in relative to the five services.
    void wireLegacyInstanceZeroSenders();

    // Auto-start salvo (per-instance config.autoStart), moved from main()
    // (M4). Registers a singleShot(0) — call after engine.loadFromModule so
    // the first frame paints before the (blocking, process-spawning) start()
    // calls run, exactly like the old inline block.
    void launchAutoStartInstances();

    // ── Single-instance activation (Fix 1) ─────────────────────────────
    //
    // Root cause of the settings "regression": two concurrently-running
    // panels share one app.db and each whole-row write clobbers the
    // other's state. main() now holds a QLockFile for the whole process
    // lifetime; a second launch fails the lock, pings this server and
    // exits. The ping brings the RUNNING panel's window back up
    // (showNormal + raise + requestActivate — also restores a window
    // minimized to tray).
    //
    // Per-user server name (USERNAME on Windows / USER elsewhere, both
    // sanitized) so multi-user machines never cross-activate; shared by
    // main.cpp's activation client and startSingleInstanceServer().
    static QString singleInstanceServerName();

    // Start listening for activation pings. Call ONCE from main(), EARLY
    // (before the engine loads) so a second launch during cold start
    // still reaches us; idempotent. A listen failure is WARN-only — the
    // panel runs normally, second launches just cannot activate it.
    void startSingleInstanceServer();

    // Hand the engine root window (Main.qml Window, as its QWindow base)
    // to the activation path. Call after engine.loadFromModule. A ping
    // that arrived BEFORE the window existed is honored here (deferred
    // raise). Stored as QPointer — auto-nulls if the window dies first.
    void setActivationWindow(QWindow* window);

private:
    // Shared raise logic for the newConnection handler and the deferred
    // path in setActivationWindow(): window ready → showNormal + raise +
    // requestActivate; not yet → remember m_pendingActivation.
    void handleActivation();

    QLocalServer* m_activationServer = nullptr; // child — dies with tree
    QPointer<QWindow> m_activationWindow;       // engine-owned, not ours
    bool m_pendingActivation = false;

private:
    // Registration order = old main() declaration order; destruction
    // follows the SAME (forward) list order on Qt ≥ 6.6 — first
    // registered dies first (see the class comment + ~PanelApplication()
    // for where the default cascade needed an explicit-teardown patch).
    DatabaseManager* m_databaseManager;
    PanelStateManager* m_panelStateManager;
    WsServer* m_wsServer;
    PendingRequests* m_pendingRequests;
    InstanceManager* m_instanceManager;
    // S3: shared roster API (see instanceApi()); registered after the
    // manager → destroyed after it (Qt ≥ 6.6 registration order); safe
    // (no destructor cross-calls; session connections drop with the
    // sessions).
    core::InstanceApiImpl* m_instanceApi = nullptr;
    // S2 (v1.2): shared instance-control write API (see
    // instanceControlApi()); registered after m_instanceApi → destroyed
    // after it (Qt ≥ 6.6 registration order); no destructor cross-calls.
    core::InstanceControlApiImpl* m_instanceControlApi = nullptr;
    // S5/S6 (v1.3): shared v1.3-family implementations (see the accessor
    // comments); registered after m_instanceControlApi, in this order →
    // destroyed after it and each other, same forward order (Qt ≥ 6.6);
    // none of their destructors make cross-calls — safe.
    core::TuningApiImpl* m_tuningApi = nullptr;
    core::ModelApiImpl* m_modelApi = nullptr;
    core::SettingsApiImpl* m_settingsApi = nullptr;
    core::VoicePackApiImpl* m_voicePackApi = nullptr;
    core::MonitorApiImpl* m_monitorApi = nullptr; // S7 (v1.4)
    core::LayoutApiImpl* m_layoutApi = nullptr;   // v1.8
    core::ResourceScopeApiImpl* m_resourceScopeApi = nullptr; // v1.13
    // S8e: rule-engine host service (see ruleEngineService()); registered
    // after m_monitorApi → destroyed after it (Qt ≥ 6.6 registration
    // order); its dtor calls earlier-registered APIs (the exit-path UAF)
    // — deleted explicitly in ~PanelApplication().
    core::RuleEngineService* m_ruleEngineService = nullptr;
    // S8f (v1.6): shared rules-registration API (see ruleApi());
    // registered after m_ruleEngineService → destroyed after it (the
    // borrowed service pointer would dangle) — deleted explicitly BEFORE
    // the service in ~PanelApplication().
    core::RuleApiImpl* m_ruleApi = nullptr;
    // Registered last (wireLegacyInstanceZeroSenders) → destroyed last
    // (Qt ≥ 6.6 registration order); neither dtor touches the services.
    StartupSalvo* m_startupSalvo = nullptr;
    ProcessManager* m_processManager = nullptr;
    PanelConfig m_panelConfig;

    // Plugin framework (P4): registry (pure logic class, held by value,
    // created before every consumer) + host (QObject child, created after
    // every service — it dereferences instanceManager at boot). The host
    // is registered LAST in the ctor → destroyed LAST in the default
    // cascade (Qt ≥ 6.6 registration order — the old "destroyed FIRST"
    // claim had the direction backwards). Plugin teardown ordering rests
    // on main()'s explicit pluginHost()->shutdownAll() call before any
    // teardown, not on child order; ~PluginHost makes no cross-calls.
    core::PluginRegistry m_pluginRegistry;
    core::PluginHost* m_pluginHost = nullptr;
    core::DownloadService* m_downloadService = nullptr;

    // S8a (v1.5): shared event bus + API face. Registered in the ctor
    // BEFORE the InstanceManager (see eventBus()) — which under the
    // Qt ≥ 6.6 forward cascade means it dies BEFORE the manager's
    // sessions, NOT after. No session sees a dead publisher at teardown
    // because ~PanelApplication() deletes m_instanceManager (and with it
    // the unsubscribing sessions) while m_eventApi is still alive.
    core::EventBus* m_eventBus = nullptr;
    core::EventApiImpl* m_eventApi = nullptr;
};
