#pragma once

// PluginManager (P4, §B.6 management UI) — the QAbstractListModel behind
// the Settings "插件" section. One row per registry entry: id / title /
// version / status / errorMessage / enabled.
//
// Enabled semantics (§B.6 + §A.4): a pure CONFIG BIT persisted through
// DatabaseManager's panel_config kv (key plugin_enabled/<id>, value "1"/"0",
// absent = enabled). Toggling never touches a running plugin — the host
// consults the bit at NEXT boot (restart-to-apply model; hot unload is
// forbidden). Persistence is never-throws: a failed kv write logs a WARN
// and keeps the in-memory bit so the UI stays truthful to the session.

#include <QAbstractListModel>
#include <QObject>
#include <QString>

#include "core/PluginRegistry.hpp"

// DatabaseManager lives in the GLOBAL namespace (mixed-namespace codebase;
// see DatabaseManager.hpp) — declared here, outside namespace core.
class DatabaseManager;

namespace core {

class PluginHost;

class PluginManager : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        VersionRole,
        StatusRole,       // status string (pluginStatusToString)
        ErrorMessageRole,
        EnabledRole,
        OrderRole,
        CapabilitiesRole, // QStringList
    };

    explicit PluginManager(PluginRegistry& registry, DatabaseManager* database,
                           QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_registry.size(); }

    // The boot-time enabled provider handed to PluginHost
    // (setEnabledProvider): true unless kv says "0". Null-database safe.
    bool enabledAtStart(const QString& pluginId) const;

    // QML: toggle + persist. Emits dataChanged; never throws.
    Q_INVOKABLE void setEnabled(const QString& pluginId, bool enabled);

    // QML helper for the "重启后生效" hint: true when the persisted bit
    // differs from the plugin's runtime state (started-but-now-disabled
    // or disabled-but-started) — i.e. a restart would change behavior.
    Q_INVOKABLE bool restartPending(const QString& pluginId) const;

signals:
    void countChanged();

private:
    static QString enabledKey(const QString& pluginId);

    PluginRegistry& m_registry;
    DatabaseManager* m_database; // not owned; may be null (degraded mode)
};

} // namespace core
