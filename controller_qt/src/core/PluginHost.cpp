#include "core/PluginHost.hpp"

#include <QTimer>

#include <algorithm>

#include <spdlog/spdlog.h>

#include "core/DownloadService.hpp"
#include "core/PluginContextImpl.hpp"
#include "core/PluginPageModel.hpp"
#include "logging/Logging.hpp"
#include "ui/NotificationStreamController.hpp"

// §A.1 crash semantics apply in full: this host catches every exception it
// can (parse/logic errors). Memory corruption still kills the panel — that
// is the documented, accepted stage-1 model ("第一方插件 = 面板组成部分").

namespace core {

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
        if (entry == nullptr || entry->create == nullptr) {
            m_registry.transition(e.manifest.id, PluginStatus::Failed,
                                  QStringLiteral("no factory function"));
            continue;
        }
        try {
            entry->instance = entry->create();
            auto* ctx = new PluginContextImpl(entry->manifest.id, m_instanceManager,
                                              m_pageModel, m_notificationStream,
                                              m_configRoot, m_downloadService,
                                              entry->manifest.capabilities,
                                              m_voicePackRefresh, this);
            m_contexts.append(ctx);
            const pet::PluginError err = entry->instance->initialize(*ctx);
            if (err != pet::PluginError::Ok) {
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
        } catch (const std::exception& ex) {
            m_registry.transition(e.manifest.id, PluginStatus::Failed,
                                  QStringLiteral("initialize threw: %1")
                                      .arg(QString::fromUtf8(ex.what())));
            LOG_ERROR("PluginHost: plugin '{}' initialize THREW (isolated; "
                      "continuing): {}",
                      e.manifest.id.toStdString(), ex.what());
        } catch (...) {
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
        delete entry->instance; // host-owned (stage 1: same binary, safe)
        entry->instance = nullptr;
        m_registry.transition(*it, PluginStatus::Stopped);
        ++shutDown;
        LOG_INFO("PluginHost: plugin '{}' Stopped", it->toStdString());
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

void PluginHost::bridgeNotify(const QString& pluginId, const QString& title,
                              const QString& text, int durationMs)
{
    if (s_stream == nullptr) {
        LOG_WARN("[plugin-api] bubble from '{}' dropped — no stream mounted",
                 pluginId.toStdString());
        return;
    }
    s_stream->push(title.isEmpty() ? pluginId : title, QString(), text, durationMs);
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
