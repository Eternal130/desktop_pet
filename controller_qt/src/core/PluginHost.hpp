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

// InstanceManager + NotificationStreamController are GLOBAL-namespace
// classes; declared outside namespace core.
class InstanceManager;
class NotificationStreamController;

namespace core {

class PluginContextImpl;
class PluginPageModel;

class PluginHost : public QObject
{
    Q_OBJECT

public:
    explicit PluginHost(PluginRegistry& registry, QObject* parent = nullptr);

    // ── Dependency injection (all optional nullptr-safe; call before
    // initializeAll — PanelApplication/PanelUiBoot own the lifetimes) ──
    void setInstanceManager(InstanceManager* instanceManager);
    void setPageModel(PluginPageModel* pageModel);
    void setNotificationStream(NotificationStreamController* stream);
    void setConfigRoot(const QString& configRoot);

    // Enabled lookup at boot (PluginManager kv-backed provider). Plugins
    // the provider marks disabled are skipped entirely (no create(), no
    // initialize() — §B.6 "禁用 = 配置位").
    void setEnabledProvider(std::function<bool(const QString& pluginId)> provider);

    // Test seam for the shutdown-budget path (production: 200ms, §B.3).
    void setShutdownBudgetMs(int ms) { m_shutdownBudgetMs = ms; }

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
    PluginPageModel* m_pageModel = nullptr;
    NotificationStreamController* m_notificationStream = nullptr;
    QString m_configRoot;
    std::function<bool(const QString&)> m_enabledProvider;
    int m_shutdownBudgetMs = 200;

    QStringList m_startedIds; // initialize order (§B.3: shutdown reverses it)
    QList<PluginContextImpl*> m_contexts; // per-plugin, host-owned

    static PluginPageModel* s_pageModel;
    static NotificationStreamController* s_stream;
    static PluginRegistry* s_registry; // set by the (single) host ctor
};

} // namespace core
