#include "core/PluginContextImpl.hpp"

#include <QDir>

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

// ── PluginContextImpl ───────────────────────────────────────────────────────

PluginContextImpl::PluginContextImpl(const QString& pluginId,
                                     InstanceManager* instanceManager,
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
      m_instanceApi(instanceManager, this),
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

} // namespace core
