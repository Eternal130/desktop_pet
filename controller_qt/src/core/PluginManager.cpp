#include "core/PluginManager.hpp"

#include <spdlog/spdlog.h>

#include "core/DatabaseManager.hpp"
#include "logging/Logging.hpp"

namespace core {

PluginManager::PluginManager(PluginRegistry& registry, DatabaseManager* database,
                             QObject* parent)
    : QAbstractListModel(parent), m_registry(registry), m_database(database)
{
    // Registry entries are append-only during boot; row notifications ride
    // the modelReset the QML view performs after the host finished
    // initializing (beginResetModel/endResetModel around panel boot is
    // unnecessary — entries are registered before QML binds).
}

QString PluginManager::enabledKey(const QString& pluginId)
{
    return QStringLiteral("plugin_enabled/") + pluginId;
}

int PluginManager::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_registry.size();
}

QVariant PluginManager::data(const QModelIndex& index, int role) const
{
    const QList<PluginEntry> ordered = m_registry.orderedEntries();
    if (index.row() < 0 || index.row() >= ordered.size())
        return {};
    const PluginEntry& e = ordered.at(index.row());
    switch (role) {
    case IdRole:           return e.manifest.id;
    case TitleRole:        return e.manifest.title.isEmpty() ? e.manifest.id : e.manifest.title;
    case VersionRole:      return e.manifest.version;
    case StatusRole:       return pluginStatusToString(e.status);
    case ErrorMessageRole: return e.errorMessage;
    case EnabledRole:      return enabledAtStart(e.manifest.id);
    case OrderRole:        return e.manifest.order;
    case CapabilitiesRole: return e.manifest.capabilities;
    default:               return {};
    }
    return {};
}

QHash<int, QByteArray> PluginManager::roleNames() const
{
    return {
        {IdRole,           "pluginId"},
        {TitleRole,        "title"},
        {VersionRole,      "version"},
        {StatusRole,       "status"},
        {ErrorMessageRole, "errorMessage"},
        {EnabledRole,      "enabled"},
        {OrderRole,        "order"},
        {CapabilitiesRole, "capabilities"},
    };
}

bool PluginManager::enabledAtStart(const QString& pluginId) const
{
    if (m_database == nullptr)
        return true; // degraded mode: treat everything as enabled
    return m_database->getValue(enabledKey(pluginId), QStringLiteral("1"))
        != QStringLiteral("0");
}

void PluginManager::setEnabled(const QString& pluginId, bool enabled)
{
    if (m_registry.entry(pluginId) == nullptr) {
        LOG_WARN("PluginManager: setEnabled for unknown plugin '{}'",
                 pluginId.toStdString());
        return;
    }
    if (m_database != nullptr) {
        // never-throws: a failed write degrades to session-only state
        if (!m_database->setValue(enabledKey(pluginId),
                                  enabled ? QStringLiteral("1") : QStringLiteral("0"))) {
            LOG_WARN("PluginManager: persisting enabled={} for '{}' FAILED — "
                     "session-only until restart",
                     enabled, pluginId.toStdString());
        }
    }
    const QList<PluginEntry> ordered = m_registry.orderedEntries();
    for (int row = 0; row < ordered.size(); ++row) {
        if (ordered.at(row).manifest.id == pluginId) {
            const QModelIndex changed = index(row, 0);
            emit dataChanged(changed, changed, {EnabledRole});
            break;
        }
    }
    LOG_INFO("PluginManager: plugin '{}' enabled={} (takes effect after "
             "restart — §A.4)",
             pluginId.toStdString(), enabled);
}

bool PluginManager::restartPending(const QString& pluginId) const
{
    const PluginEntry* e = m_registry.entry(pluginId);
    if (e == nullptr)
        return false;
    const bool enabled = enabledAtStart(pluginId);
    // Restart would change behavior iff the persisted bit disagrees with
    // the runtime reality: Started-but-disabled (won't start next boot) or
    // skipped-but-enabled (Registered = was skipped at boot, now on again).
    if (e->status == PluginStatus::Started)
        return !enabled;
    if (e->status == PluginStatus::Registered)
        return enabled;
    return false;
}

} // namespace core
