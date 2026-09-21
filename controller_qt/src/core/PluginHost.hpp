#pragma once

// PluginHost (P4, §B.3 lifecycle + §A.1 exception wrapping) — drives
// initialize()/shutdown() for every registered plugin.
//
//   initializeAll(): manifest order (PluginRegistry::orderedEntries);
//     disabled plugins (config bit, PluginManager) are SKIPPED before
//     create(); each initialize() runs inside try/catch ALL — a throwing
//     or error-returning plugin becomes Failed and NEVER poisons the
//     next one (§A.1 "坏处理器不毒化消息泵" discipline).
//
//   shutdownAll(): reverse initialization order, ≤shutdownBudgetMs() TOTAL
//     across all plugins (§B.3; default 200ms). When the budget is
//     exhausted the remaining shutdowns are skipped with a WARN (the
//     panel exits regardless — best effort, never fatal). Exceptions are
//     caught per plugin.
//
// Thread: all methods run on the GUI thread (called from main / app exit).

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>

#include "core/PluginRegistry.hpp"

#include "api/IModelApi.hpp"
#include "api/IMonitorApi.hpp"
#include "api/ISettingsApi.hpp"
#include "api/ITuningApi.hpp"
#include "api/IVoicePackApi.hpp"

// InstanceManager + NotificationStreamController are GLOBAL-namespace
// classes; declared outside namespace core.
class InstanceManager;
class NotificationStreamController;

namespace core {

class DownloadService;
class InstanceApiImpl;
class InstanceControlApiImpl;
class ModelApiImpl;
class MonitorApiImpl;
class PluginContextImpl;
class PluginPageModel;
class SettingsApiImpl;
class TuningApiImpl;
class VoicePackApiImpl;

class PluginHost : public QObject
{
    Q_OBJECT

public:
    explicit PluginHost(PluginRegistry& registry, QObject* parent = nullptr);

    // ── Dependency injection (all optional nullptr-safe; call before
    // initializeAll — PanelApplication/PanelUiBoot own the lifetimes) ──
    void setInstanceManager(InstanceManager* instanceManager);
    // S3 roster dogfooding: the SHARED InstanceApiImpl handed to every
    // plugin context (and to the panel's own RosterApiModel). When null,
    // initializeAll falls back to a per-host InstanceApiImpl over the
    // injected InstanceManager — the pre-S3 behavior (tests, degraded
    // wiring).
    void setInstanceApi(InstanceApiImpl* instanceApi);
    // S2 (v1.2): the SHARED InstanceControlApiImpl served through
    // queryApi("pet.instance_control") when a plugin's manifest grants
    // "instance_lifecycle" and the write switch (below) is on. Same
    // fallback pattern as setInstanceApi: null → initializeAll constructs
    // a per-host implementation over the injected InstanceManager.
    void setInstanceControlApi(InstanceControlApiImpl* instanceControlApi);
    // S5/S6 (v1.3): the SHARED TuningApiImpl / ModelApiImpl /
    // SettingsApiImpl / VoicePackApiImpl served through queryApi (tuning
    // + settings are capability/switch-gated at the context; the model
    // family is a read and ungated). Same fallback pattern as
    // setInstanceApi: null → initializeAll constructs per-host
    // implementations over the injected InstanceManager / configRoot
    // (tests, degraded wiring).
    void setTuningApi(pet::ITuningApi* tuningApi);
    void setModelApi(pet::IModelApi* modelApi);
    void setSettingsApi(pet::ISettingsApi* settingsApi);
    // v1.3 (S6): shared voice-pack read API (observers + the host's
    // single pack scan). Null → the per-context scan-everything impl
    // keeps serving voicePackApi() (the pre-v1.3 behavior).
    void setVoicePackApi(pet::IVoicePackApi* voicePackApi);
    // S7 (v1.4): the SHARED MonitorApiImpl served through
    // queryApi("pet.monitor") — a read family (ungated; same injection
    // pattern as setModelApi). Null → initializeAll constructs a
    // per-host implementation over the injected InstanceManager (the
    // setInstanceApi fallback pattern; tests / degraded wiring).
    void setMonitorApi(pet::IMonitorApi* monitorApi);
    // S2 (v1.2): host-global plugin_write_enabled kill-switch provider
    // (PanelApplication wires it to the panel_config kv row; absent
    // provider = enabled, mirroring the kv default). Consulted LIVE on
    // every queryApi call so revocation needs no restart. std::function is
    // host-internal (core↔app seam), never crosses the plugin boundary.
    void setWriteEnabledProvider(std::function<bool()> provider);
    void setPageModel(PluginPageModel* pageModel);
    void setNotificationStream(NotificationStreamController* stream);
    void setConfigRoot(const QString& configRoot);

    // Enabled lookup at boot (PluginManager kv-backed provider). Plugins
    // the provider marks disabled are skipped entirely (no create(), no
    // initialize() — §B.6 "禁用 = 配置位").
    void setEnabledProvider(std::function<bool(const QString& pluginId)> provider);

    // Test seam for the shutdown-budget path (production: 200ms, §B.3).
    void setShutdownBudgetMs(int ms) { m_shutdownBudgetMs = ms; }

    // P5 seams: the host download service handed to plugin contexts
    // (capability-gated at the context) and the voice-pack refresh hook
    // (wired to VoicePackController::rescan by PanelUiBoot).
    void setDownloadService(DownloadService* service);
    void setVoicePackRefresh(std::function<void()> refresh);

    void initializeAll();
    void shutdownAll();

    // Post-boot queries (tests + exit-QA log scraping).
    QStringList startedIds() const { return m_startedIds; }
    int failedCount() const;

    // ── Static seams ──────────────────────────────────────────────────────
    // Bridge bubble route (PluginBridge::notify) — static so bridges never
    // need a host pointer; null-stream safe.
    static void bridgeNotify(const QString& pluginId, const QString& title,
                             const QString& text, int durationMs);

    // Build-injected entry URL lookup for UiApiImpl's registerPage default
    // (registry lives in the application service tree; the context impls
    // avoid holding a registry pointer for this one lookup).
    static QString entryQmlUrlFor(const QString& pluginId);

    // The registry this host drives (set once at construction; owned by
    // PanelApplication).
    PluginRegistry& registry() const { return m_registry; }

    // Active page model / stream for the static seams (null-safe).
    static PluginPageModel* pageModel() { return s_pageModel; }
    static NotificationStreamController* notificationStream() { return s_stream; }

private:
    PluginRegistry& m_registry;
    InstanceManager* m_instanceManager = nullptr;
    InstanceApiImpl* m_instanceApi = nullptr; // shared (service tree), not owned
    InstanceControlApiImpl* m_instanceControlApi = nullptr; // S2: shared, not owned
    // S5/S6 (v1.3): shared v1.3-family implementations (service tree),
    // not owned. Tuning/model/settings also keep per-host FALLBACKS
    // (constructed lazily in initializeAll when no shared impl was
    // injected — the setInstanceApi pattern) so a host without the
    // PanelApplication service tree still serves the families.
    pet::ITuningApi* m_tuningApi = nullptr;
    pet::IModelApi* m_modelApi = nullptr;
    pet::ISettingsApi* m_settingsApi = nullptr;
    pet::IVoicePackApi* m_voicePackApi = nullptr; // v1.3 shared, not owned
    pet::IMonitorApi* m_monitorApi = nullptr;     // v1.4 (S7), not owned
    std::function<bool()> m_writeEnabledProvider;           // S2: live kill-switch
    PluginPageModel* m_pageModel = nullptr;
    NotificationStreamController* m_notificationStream = nullptr;
    QString m_configRoot;
    DownloadService* m_downloadService = nullptr; // not owned (service tree)
    std::function<void()> m_voicePackRefresh;
    std::function<bool(const QString&)> m_enabledProvider;
    int m_shutdownBudgetMs = 200;

    QStringList m_startedIds; // initialize order (§B.3: shutdown reverses it)
    QList<PluginContextImpl*> m_contexts; // per-plugin, host-owned

    static PluginPageModel* s_pageModel;
    static NotificationStreamController* s_stream;
    static PluginRegistry* s_registry; // set by the (single) host ctor
};

} // namespace core
