#include "ui/NotificationStreamModel.hpp"

#include <QModelIndex>
#include <QVariant>

#include <utility>

// spdlog MUST be included before logging/Logging.hpp — Logging.hpp references
// the SPDLOG_* macros but does NOT include spdlog headers itself (T5 finding).
#include <spdlog/spdlog.h>
#include "logging/Logging.hpp"

NotificationStreamModel::NotificationStreamModel(QObject* parent)
    : QAbstractListModel(parent)
    , m_expireTimer(new QTimer(this))
{
    m_expireTimer->setInterval(1000);
    connect(m_expireTimer, &QTimer::timeout, this, [this] { expireNow(); });
    m_expireTimer->start();
}

int NotificationStreamModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant NotificationStreamModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
        return {};
    const int row = index.row();
    if (row < 0 || row >= m_entries.size())
        return {};
    const Entry& e = m_entries.at(row);
    switch (role) {
        case AvatarRole:     return e.avatar;
        case NameRole:       return e.name;
        case TextRole:       return e.text;
        case CreatedAtMsRole: return e.createdAtMs;
        case ExpiresAtMsRole: return e.expiresAtMs;
    }
    return {};
}

QHash<int, QByteArray> NotificationStreamModel::roleNames() const
{
    return {
        {AvatarRole,      "avatar"},
        {NameRole,        "name"},
        {TextRole,        "text"},
        {CreatedAtMsRole, "createdAtMs"},
        {ExpiresAtMsRole, "expiresAtMs"},
    };
}

void NotificationStreamModel::push(const QString& name, const QString& avatar,
                                   const QString& text, int durationMs)
{
    Entry entry;
    entry.name = name;
    entry.avatar = avatar;
    entry.text = text;
    entry.createdAtMs = nowMs();
    entry.expiresAtMs = durationMs > 0 ? entry.createdAtMs + durationMs : 0;

    beginInsertRows(QModelIndex(), 0, 0);
    m_entries.prepend(std::move(entry));
    endInsertRows();

    if (m_entries.size() > kMaxEntries) {
        const int last = m_entries.size() - 1;
        beginRemoveRows(QModelIndex(), last, last);
        m_entries.removeLast();
        endRemoveRows();
    }
    emit countChanged();
}

int NotificationStreamModel::expireNow()
{
    const qint64 now = nowMs();
    int removed = 0;
    // Entries are newest-first; expired ones cluster at the tail. Walk from
    // the oldest end so each removal's row index stays valid.
    for (int row = m_entries.size() - 1; row >= 0; --row) {
        const Entry& e = m_entries.at(row);
        if (e.expiresAtMs > 0 && now >= e.expiresAtMs) {
            beginRemoveRows(QModelIndex(), row, row);
            m_entries.removeAt(row);
            endRemoveRows();
            ++removed;
        }
    }
    if (removed > 0)
        emit countChanged();
    return removed;
}

void NotificationStreamModel::dismiss(int row)
{
    if (row < 0 || row >= m_entries.size()) {
        LOG_WARN("NotificationStreamModel::dismiss: invalid row {}", row);
        return;
    }
    beginRemoveRows(QModelIndex(), row, row);
    m_entries.removeAt(row);
    endRemoveRows();
    emit countChanged();
}

QString NotificationStreamModel::nameAt(int row) const
{
    return (row >= 0 && row < m_entries.size()) ? m_entries.at(row).name
                                                : QString();
}

QString NotificationStreamModel::textAt(int row) const
{
    return (row >= 0 && row < m_entries.size()) ? m_entries.at(row).text
                                                : QString();
}

qint64 NotificationStreamModel::expiresAtMsAt(int row) const
{
    return (row >= 0 && row < m_entries.size())
               ? m_entries.at(row).expiresAtMs : 0;
}
