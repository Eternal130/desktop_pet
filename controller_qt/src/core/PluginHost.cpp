#include "core/PluginHost.hpp"

#include <QTimer>

#include <algorithm>

#include <spdlog/spdlog.h>

#include "api/EventIds.hpp"
#include "core/DownloadService.hpp"
#include "core/EventBus.hpp"
#include "core/EventCatalog.hpp"
#include "core/PluginContextImpl.hpp"
#include "core/PluginPageModel.hpp"
#include "core/RuleEngineService.hpp" // complete type for the S8f fallback
#include "logging/Logging.hpp"
#include "ui/NotificationStreamController.hpp"

// §A.1 crash semantics apply in full: this host catches every exception it
// can (parse/logic errors). Memory corruption still kills the panel — that
// is the documented, accepted stage-1 model ("第一方插件 = 面板组成部分").

namespace core {

namespace {

// v1.7 (S9): publish one PANEL-LEVEL host event (pet.panel.* —
// instanceUuid stays "", publisherId = host, never cancellable). No-op on
// a null/unwired API (degraded wiring — the fallback pair below resolves
// in both callers before this runs).
void publishPanelEvent(EventApiImpl* eventApi, const QString& eventId,
                       const QJsonObject& payload)
{
    if (eventApi == nullptr)
        return;
    pet::Event evt;
    evt.eventId = eventId;
    evt.instanceUuid = QString(); // panel-level — no per-pet identity
    evt.publisherId = QString(pet::kEventPublisherHost);
    evt.cancellable = false;
    evt.payload = payload;
    eventApi->publish(evt); // synchronous; errors are bus-level
}

} // namespace

PluginPageModel* PluginHost::s_pageModel = nullptr;
NotificationStreamController* PluginHost::s_stream = nullptr;
PluginRegistry* PluginHost::s_registry = nullptr;

PluginHost::PluginHost(PluginRegistry& registry, QObject* parent)
    : QObject(parent), m_registry(registry)
{
    // Single-host application: the ctor registration doubles as the
    // lookup seam for entryQmlUrlFor (contexts never hold registry
    // pointers for this one read).
    s_registry = &registry;
}

void PluginHost::setInstanceManager(InstanceManager* instanceManager)
{
    m_instanceManager = instanceManager;
}

void PluginHost::setInstanceApi(InstanceApiImpl* instanceApi)
{
    m_instanceApi = instanceApi;
}

void PluginHost::setInstanceControlApi(InstanceControlApiImpl* instanceControlApi)
{
    m_instanceControlApi = instanceControlApi;
}

void PluginHost::setTuningApi(pet::ITuningApi* tuningApi)
{
    m_tuningApi = tuningApi;
}

void PluginHost::setModelApi(pet::IModelApi* modelApi)
{
    m_modelApi = modelApi;
}

void PluginHost::setSettingsApi(pet::ISettingsApi* settingsApi)
{
    m_settingsApi = settingsApi;
}

void PluginHost::setVoicePackApi(pet::IVoicePackApi* voicePackApi)
{
    m_voicePackApi = voicePackApi;
}

void PluginHost::setMonitorApi(pet::IMonitorApi* monitorApi)
{
    m_monitorApi = monitorApi;
}

void PluginHost::setLayoutApi(pet::ILayoutApi* layoutApi)
{
    m_layoutApi = layoutApi;
}

void PluginHost::setEventApi(EventApiImpl* eventApi)
{
    m_eventApi = eventApi;
}

void PluginHost::setRuleApi(RuleApiImpl* ruleApi)
{
    m_ruleApi = ruleApi;
}

void PluginHost::setWriteEnabledProvider(std::function<bool()> provider)
{
    m_writeEnabledProvider = std::move(provider);
}

void PluginHost::setPageModel(PluginPageModel* pageModel)
{
    m_pageModel = pageModel;
    s_pageModel = pageModel; // static seam for PluginBridge/UiApiImpl
}

void PluginHost::setNotificationStream(NotificationStreamController* stream)
{
    m_notificationStream = stream;
    s_stream = stream;
}

void PluginHost::setEnabledProvider(std::function<bool(const QString& pluginId)> provider)
{
    m_enabledProvider = std::move(provider);
}

void PluginHost::setConfigRoot(const QString& configRoot)
{
    m_configRoot = configRoot;
}

void PluginHost::setDownloadService(DownloadService* service)
{
    m_downloadService = service;
}

void PluginHost::setVoicePackRefresh(std::function<void()> refresh)
{
    m_voicePackRefresh = std::move(refresh);
}

void PluginHost::initializeAll()
{
    // S8a (v1.5): resolve the event-bus API once, BEFORE any context is
    // created — the per-host fallback pair (bus first, then impl;
    // registration order keeps the bus alive past the impl) covers a
    // host without the PanelApplication service tree, the same
    // degraded-wiring pattern as the roster/control fallbacks above.
    if (m_eventApi == nullptr) {
        m_eventBusFallback = new EventBus(this);
        m_eventApiFallback = new EventApiImpl(m_eventBusFallback, this);
    }
    EventApiImpl* const eventApi =
        m_eventApi != nullptr ? m_eventApi : m_eventApiFallback;

    // S8f (v1.6): resolve the rules API the same way — the per-host
    // fallback pair (a degraded RuleEngineService over the resolved
    // event API above: no roster / tuning / db → loads nothing at
    // startup, runtime registrations still subscribe + evaluate on the
    // bus) covers a host without the PanelApplication service tree.
    // Created BEFORE any context is built → registered earlier as a
    // host child → destroyed after every context (registration-reverse),
    // so the injected/borrowed pointers outlive each context's proxy.
    if (m_ruleApi == nullptr) {
        m_ruleServiceFallback = new RuleEngineService(
            eventApi, /*instanceApi=*/nullptr, /*tuningApi=*/nullptr,
            /*db=*/nullptr, this);
        m_ruleApiFallback = new RuleApiImpl(m_ruleServiceFallback, this);
    }
    RuleApiImpl* const ruleApi =
        m_ruleApi != nullptr ? m_ruleApi : m_ruleApiFallback;

    const QList<PluginEntry> entries = m_registry.orderedEntries();
    LOG_INFO("PluginHost: initializing {} plugin(s) in manifest order",
             entries.size());
    for (const PluginEntry& e : entries) {
        // Disabled = config bit + skip BEFORE create (§B.6): no code from
        // a disabled plugin executes at all.
        if (m_enabledProvider && !m_enabledProvider(e.manifest.id)) {
            LOG_INFO("PluginHost: plugin '{}' disabled by config — skipping "
                     "initialize (restart-to-apply model, §A.4)",
                     e.manifest.id.toStdString());
            continue;
        }
        if (e.status != PluginStatus::Registered) {
            // Failed-at-validation entries surface in the UI; Started/
            // Stopped would mean double init.
            LOG_WARN("PluginHost: plugin '{}' in state {} — not initializable",
                     e.manifest.id.toStdString(),
                     pluginStatusToString(e.status).toStdString());
            continue;
        }
        PluginEntry* entry = m_registry.entry(e.manifest.id);
        // Stage-1 static entries create through the CMake factory table;
        // stage-2 dynamic entries through the module-side IPluginFactory
        // (either missing → the same Failed path).
        if (entry == nullptr ||
            (entry->source == QStringLiteral("static") &&
             entry->create == nullptr) ||
            (entry->source == QStringLiteral("dynamic") &&
             entry->factory == nullptr)) {
            m_registry.transition(e.manifest.id, PluginStatus::Failed,
                                  QStringLiteral("no factory function"));
            continue;
        }
        try {
            // Static: host-binary create() (safe — same binary). Dynamic:
            // construction INSIDE the plugin module via factory->create()
            // (the host must never construct plugin code across the module
            // boundary); same try/catch isolation as everything below.
            entry->instance = entry->source == QStringLiteral("dynamic")
                ? entry->factory->create()
                : entry->create();
            // P0-1: a factory (static CreateFn or dynamic IPluginFactory)
            // returning null must fail the entry cleanly — no context
            // construction, no initialize() on a null this-pointer.
            if (entry->instance == nullptr) {
                m_registry.transition(e.manifest.id, PluginStatus::Failed,
                                      QStringLiteral("factory returned null "
                                                     "instance"));
                LOG_ERROR("PluginHost: plugin '{}' factory returned a null "
                          "instance — marking Failed",
                          e.manifest.id.toStdString());
                continue;
            }
            // S3 roster dogfooding: prefer the SHARED InstanceApiImpl from
            // the service tree (PanelApplication) so plugins and the panel's
            // own RosterApiModel observe the same object. Degraded wiring
            // (tests / no service tree): per-host fallback over the injected
            // InstanceManager — the pre-S3 per-context behavior, parented
            // here so ownership stays local and registration order keeps it
            // alive past the contexts created after it.
            InstanceApiImpl* api = m_instanceApi;
            if (api == nullptr)
                api = new InstanceApiImpl(m_instanceManager, this);
            // S2 (v1.2): same per-host fallback for the instance-control
            // write API — a host without the PanelApplication service tree
            // (tests, degraded wiring) still serves queryApi with a local
            // implementation over the injected InstanceManager.
            InstanceControlApiImpl* controlApi = m_instanceControlApi;
            if (controlApi == nullptr)
                controlApi = new InstanceControlApiImpl(m_instanceManager, this);
            // S5/S6 (v1.3): same per-host fallback for the three new
            // families. Constructed BEFORE the context below → registered
            // earlier as host children → destroyed AFTER the contexts
            // (registration-reverse), so the injected pointers outlive
            // every context that holds them. The voice-pack family keeps
            // its per-context impl when no shared one is injected.
            pet::ITuningApi* tuningApi = m_tuningApi;
            if (tuningApi == nullptr)
                tuningApi = new TuningApiImpl(m_instanceManager, this);
            pet::IModelApi* modelApi = m_modelApi;
            if (modelApi == nullptr)
                modelApi = new ModelApiImpl(this);
            pet::ISettingsApi* settingsApi = m_settingsApi;
            if (settingsApi == nullptr) {
                // v1.7 (S9): the fallback settings impl also emits
                // pet.panel.settings_changed onto the resolved event API
                // (the same bus the shared impl publishes on).
                auto* fallbackSettings = new SettingsApiImpl(m_configRoot,
                                                             this);
                fallbackSettings->setEventPublisher(eventApi);
                settingsApi = fallbackSettings;
            }
            // S7 (v1.4): same per-host fallback for the monitor read
            // family (a host without the PanelApplication service tree
            // still serves queryApi("pet.monitor") over the injected
            // InstanceManager).
            pet::IMonitorApi* monitorApi = m_monitorApi;
            if (monitorApi == nullptr)
                monitorApi = new MonitorApiImpl(m_instanceManager, this);
            // v1.8: same per-host fallback for the layout family (the
            // setTuningApi pattern — a host without the service tree
            // still serves queryApi("pet.layout") over the injected
            // InstanceManager; the context gates it per manifest
            // capability + the live write switch).
            pet::ILayoutApi* layoutApi = m_layoutApi;
            if (layoutApi == nullptr)
                layoutApi = new LayoutApiImpl(m_instanceManager, this);
            auto* ctx = new PluginContextImpl(entry->manifest.id, api,
                                              controlApi,
                                              m_writeEnabledProvider,
                                              m_pageModel, m_notificationStream,
                                              m_configRoot, m_downloadService,
                                              entry->manifest.capabilities,
                                              m_voicePackRefresh, this);
            // v1.3 shared-family injection (post-construction seam keeps
            // the frozen ctor signature for the direct-construction tests).
            ctx->setTuningApi(tuningApi);
            ctx->setModelApi(modelApi);
            ctx->setSettingsApi(settingsApi);
            ctx->setMonitorApi(monitorApi); // v1.4 (S7)
            ctx->setLayoutApi(layoutApi);   // v1.8
            // S8a (v1.5): every context gets its per-plugin proxy over
            // the shared bus (prefix enforcement + publisher stamping
            // live in the proxy — see EventApiProxy).
            ctx->setEventApi(eventApi);
            // S8f (v1.6): every context gets its per-plugin rules proxy
            // (owner-prefix + scope stamping live in the proxy + the
            // shared impl's plugin seam — see RuleApiProxy).
            ctx->setRuleApi(ruleApi);
            if (m_voicePackApi != nullptr)
                ctx->setVoicePackApi(m_voicePackApi);
            m_contexts.append(ctx);
            const pet::PluginError err = entry->instance->initialize(*ctx);
            if (err != pet::PluginError::Ok) {
                // P1-3: the instance dies NOW (exactly-once destroy
                // contract) — destroy first, then record the failure
                // reason, so a throwing destroy cannot mask it.
                destroyInstanceNow(*entry);
                m_registry.transition(entry->manifest.id, PluginStatus::Failed,
                                      QStringLiteral("initialize returned error %1")
                                          .arg(static_cast<int>(err)));
                LOG_ERROR("PluginHost: plugin '{}' initialize failed (code {})",
                          entry->manifest.id.toStdString(), static_cast<int>(err));
                continue;
            }
            m_registry.transition(entry->manifest.id, PluginStatus::Started);
            m_startedIds.append(entry->manifest.id);
            LOG_INFO("PluginHost: plugin '{}' Started", entry->manifest.id.toStdString());
            // v1.7 (S9): panel-level observability — every successful
            // initialize fans pet.panel.plugin_loaded {plugin_id} onto
            // the resolved bus (shared or fallback). Failures above
            // continue past this point, so they publish nothing.
            publishPanelEvent(eventApi,
                              QString(pet::kEventPanelPluginLoaded),
                              core::makePluginLifecyclePayload(
                                  entry->manifest.id));
        } catch (const std::exception& ex) {
            // P1-3: same immediate teardown when initialize() THREW (the
            // instance exists unless the throw came from create() itself —
            // destroyInstanceNow null-checks). Destroy before the Failed
            // transition so the recorded reason stays the original throw.
            if (entry != nullptr && entry->instance != nullptr)
                destroyInstanceNow(*entry);
            m_registry.transition(e.manifest.id, PluginStatus::Failed,
                                  QStringLiteral("initialize threw: %1")
                                      .arg(QString::fromUtf8(ex.what())));
            LOG_ERROR("PluginHost: plugin '{}' initialize THREW (isolated; "
                      "continuing): {}",
                      e.manifest.id.toStdString(), ex.what());
        } catch (...) {
            if (entry != nullptr && entry->instance != nullptr)
                destroyInstanceNow(*entry);
            m_registry.transition(e.manifest.id, PluginStatus::Failed,
                                  QStringLiteral("initialize threw an unknown exception"));
            LOG_ERROR("PluginHost: plugin '{}' initialize threw an UNKNOWN "
                      "exception (isolated; continuing)",
                      e.manifest.id.toStdString());
        }
    }
    if (m_pageModel != nullptr)
        m_pageModel->finalizeRegistrations(); // late registerPage → logged+ignored
}

void PluginHost::shutdownAll()
{
    if (m_startedIds.isEmpty()) {
        LOG_DEBUG("PluginHost: shutdown — no started plugins");
        return;
    }
    QElapsedTimer budget;
    budget.start();
    // Reverse initialization order (§B.3). Iterate a copy: registry state
    // changes inside the loop, m_startedIds stays the record.
    const QStringList ids = m_startedIds;
    int shutDown = 0;
    for (auto it = ids.crbegin(); it != ids.crend(); ++it) {
        if (budget.elapsed() >= m_shutdownBudgetMs) {
            LOG_WARN("PluginHost: shutdown budget ({}ms) exhausted after {}/{} "
                     "plugins — skipping the rest (best-effort, §B.3)",
                     m_shutdownBudgetMs, shutDown, ids.size());
            break;
        }
        PluginEntry* entry = m_registry.entry(*it);
        if (entry == nullptr || entry->instance == nullptr)
            continue;
        try {
            entry->instance->shutdown();
        } catch (const std::exception& ex) {
            LOG_WARN("PluginHost: plugin '{}' shutdown threw (isolated): {}",
                     it->toStdString(), ex.what());
        } catch (...) {
            LOG_WARN("PluginHost: plugin '{}' shutdown threw an unknown "
                     "exception (isolated)",
                     it->toStdString());
        }
        // S8a (v1.5): event-bus unload sweep — a plugin that failed to
        // unsubscribe its bus listeners in shutdown() would leave
        // dangling pointers the instant the instance is deleted below.
        // Drop everything booked under its id BEFORE the delete; a
        // non-zero count is WARNed (the plugin should have cleaned up
        // itself — the sweep is the safety net, not the contract).
        {
            EventApiImpl* const eventApi =
                m_eventApi != nullptr ? m_eventApi : m_eventApiFallback;
            if (eventApi != nullptr) {
                const int swept = eventApi->unsubscribeByPublisher(*it);
                if (swept > 0) {
                    LOG_WARN("PluginHost: plugin '{}' left {} event-bus "
                             "subscription(s) at unload — swept (no dangling "
                             "listeners)",
                             it->toStdString(), swept);
                }
            }
        }
        // S8f (v1.6): plugin-rule unload sweep (same safety-net shape as
        // the event sweep above) — a plugin that failed to unregister
        // its rules in shutdown() would leave live rules (bus
        // subscriptions + gated actions) firing forever. Drop every rule
        // stored under its owner prefix BEFORE the delete; a non-zero
        // count is WARNed (the plugin should have cleaned up itself —
        // the sweep is the safety net, not the contract).
        {
            RuleApiImpl* const ruleApi =
                m_ruleApi != nullptr ? m_ruleApi : m_ruleApiFallback;
            if (ruleApi != nullptr) {
                const int swept = ruleApi->sweepPluginRules(*it);
                if (swept > 0) {
                    LOG_WARN("PluginHost: plugin '{}' left {} registered "
                             "rule(s) at unload — swept",
                             it->toStdString(), swept);
                }
            }
        }
        // Ownership (stage 2, P6a criterion 5): a DYNAMIC plugin's
        // instance is destroyed INSIDE its module via factory->destroy() —
        // the host must never delete across the module boundary (UB).
        // Static plugins share the host binary; direct delete stays safe.
        // (P1-3: single-sourced with the initialize-failure paths in
        // destroyInstanceNow.)
        destroyInstanceNow(*entry);
        m_registry.transition(*it, PluginStatus::Stopped);
        ++shutDown;
        LOG_INFO("PluginHost: plugin '{}' Stopped", it->toStdString());
        // v1.7 (S9): panel-level observability — pet.panel.plugin_unloaded
        // {plugin_id} fires per plugin AFTER its event/rule sweeps + the
        // delete complete (the same "cleaned up" position as the sweeps).
        publishPanelEvent(
            m_eventApi != nullptr ? m_eventApi : m_eventApiFallback,
            QString(pet::kEventPanelPluginUnloaded),
            core::makePluginLifecyclePayload(*it));
    }
    m_startedIds.clear();
}

int PluginHost::failedCount() const
{
    int failed = 0;
    const QList<PluginEntry> entries = m_registry.orderedEntries();
    for (const PluginEntry& e : entries) {
        if (e.status == PluginStatus::Failed)
            ++failed;
    }
    return failed;
}

void PluginHost::destroyInstanceNow(PluginEntry& entry)
{
    if (entry.instance == nullptr)
        return;

    if (entry.source == QStringLiteral("dynamic")) {
        if (entry.factory != nullptr) {
            try {
                entry.factory->destroy(entry.instance);
            } catch (const std::exception& ex) {
                LOG_WARN("PluginHost: plugin '{}' destroy threw (isolated): {}",
                         entry.manifest.id.toStdString(), ex.what());
            } catch (...) {
                LOG_WARN("PluginHost: plugin '{}' destroy threw an unknown "
                         "exception (isolated)",
                         entry.manifest.id.toStdString());
            }
        } else {
            // Unreachable from initializeAll/shutdownAll entry states
            // (both require a factory); leak + WARN rather than a
            // cross-module delete.
            LOG_WARN("PluginHost: dynamic plugin '{}' has no factory at "
                     "teardown — instance leaked (never cross-module delete)",
                     entry.manifest.id.toStdString());
        }
    } else {
        delete entry.instance; // host-owned (stage 1: same binary, safe)
    }
    entry.instance = nullptr;
}

void PluginHost::bridgeNotify(const QString& pluginId, const QString& title,
                              const QString& text, int durationMs)
{
    if (s_stream == nullptr) {
        LOG_WARN("[plugin-api] bubble from '{}' dropped — no stream mounted",
                 pluginId.toStdString());
        return;
    }
    // Plugin bridge bubble: title (or plugin id) stays the bubble NAME;
    // no pet identity → instanceId "" (BubbleCard glyph fallback).
    s_stream->push(QString(), title.isEmpty() ? pluginId : title, QString(),
                   text, durationMs);
}

QString PluginHost::entryQmlUrlFor(const QString& pluginId)
{
    // Build-injected URL lives on the registry entry (CMake-generated
    // bootstrap passes it through addStaticPlugin's qmlUrl plumbing).
    if (s_registry == nullptr)
        return {};
    const PluginEntry* e = s_registry->entry(pluginId);
    return e != nullptr ? e->manifest.qmlUrl : QString();
}

} // namespace core
