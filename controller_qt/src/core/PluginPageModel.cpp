#include "core/PluginPageModel.hpp"

#include <QList>

#include <algorithm>

#include <spdlog/spdlog.h>

#include "core/PluginBridge.hpp"
#include "logging/Logging.hpp"

namespace core {

PluginPageModel::PluginPageModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int PluginPageModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant PluginPageModel::data(const QModelIndex& index, int role) const
{
    if (index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const PluginPageRow& row = m_rows.at(index.row());
    switch (role) {
    case PageKeyRole: return row.pluginId;
    case TitleRole:   return row.title;
    case IconUrlRole: return row.iconUrl;
    case QmlUrlRole:  return row.qmlUrl;
    case OrderRole:   return row.order;
    case BridgeRole:  return QVariant::fromValue(row.bridge);
    default:          return {};
    }
    return {};
}

QHash<int, QByteArray> PluginPageModel::roleNames() const
{
    return {
        {PageKeyRole, "pageKey"},
        {TitleRole,   "title"},
        {IconUrlRole, "iconUrl"},
        {QmlUrlRole,  "qmlUrl"},
        {OrderRole,   "order"},
        {BridgeRole,  "bridge"},
    };
}

bool PluginPageModel::addPage(const QString& pluginId, const pet::PageDescriptor& page)
{
    if (!m_acceptingRegistrations) {
        LOG_WARN("PluginPageModel: registerPage AFTER boot window from '{}' — "
                 "logged and ignored (§B.3)",
                 pluginId.toStdString());
        return false;
    }
    if (page.qmlUrl.isEmpty() || !page.qmlUrl.startsWith(QStringLiteral("qrc:/"))) {
        LOG_WARN("PluginPageModel: rejected page from '{}' — qmlUrl must be a "
                 "qrc:/ URL (§B.6), got '{}'",
                 pluginId.toStdString(), page.qmlUrl.toStdString());
        return false;
    }
    for (const PluginPageRow& existing : m_rows) {
        if (existing.pluginId == pluginId) {
            LOG_WARN("PluginPageModel: '{}' already registered a page — ignoring "
                     "duplicate (no unregisterPage, §B.3)",
                     pluginId.toStdString());
            return false;
        }
    }

    PluginPageRow row;
    row.pluginId = pluginId;
    row.title = page.title.isEmpty() ? pluginId : page.title;
    row.iconUrl = page.iconUrl;
    row.qmlUrl = page.qmlUrl;
    row.order = page.order;
    row.bridge = new PluginBridge(pluginId, row.title, this);

    const auto insertIt = std::lower_bound(
        m_rows.begin(), m_rows.end(), row,
        [](const PluginPageRow& a, const PluginPageRow& b) {
            if (a.order != b.order)
                return a.order < b.order;
            return a.title < b.title;
        });
    const int rowIdx = static_cast<int>(insertIt - m_rows.begin());
    beginInsertRows(QModelIndex(), rowIdx, rowIdx);
    m_rows.insert(insertIt, row);
    endInsertRows();
    emit countChanged();
    LOG_INFO("PluginPageModel: page '{}' (order {}) from plugin '{}'",
             row.title.toStdString(), row.order, pluginId.toStdString());
    return true;
}

void PluginPageModel::finalizeRegistrations()
{
    m_acceptingRegistrations = false;
}

QString PluginPageModel::qmlUrlFor(const QString& pageKey) const
{
    for (const PluginPageRow& row : m_rows) {
        if (row.pluginId == pageKey)
            return row.qmlUrl;
    }
    return {};
}

bool PluginPageModel::isPluginPage(const QString& pageKey) const
{
    return !qmlUrlFor(pageKey).isEmpty();
}

} // namespace core
