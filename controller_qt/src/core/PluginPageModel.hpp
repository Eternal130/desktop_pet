#pragma once

// PluginPageModel (P4, §B.5/§B.6) — the QAbstractListModel Main.qml's nav
// consumes for dynamic plugin pages. Rows are added by the host while
// plugins initialize (IUiApi::registerPage); the model is stable after
// boot (no unregisterPage — §B.3 YAGNI).
//
// pageKey == plugin id == Main.qml switchPage() key, so ScreenshotRunner's
// --pages list can name plugin pages directly (screenshot rotation gate).

#include <QAbstractListModel>
#include <QObject>
#include <QString>
#include <QStringList>

#include "api/PluginTypes.hpp"

namespace core {

class PluginBridge;

struct PluginPageRow {
    QString pluginId;   // == pageKey
    QString title;
    QString iconUrl;
    QString qmlUrl;
    int order = 100;
    PluginBridge* bridge = nullptr; // per-plugin facade (context-chain visible)
};

class PluginPageModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        PageKeyRole = Qt::UserRole + 1,
        TitleRole,
        IconUrlRole,
        QmlUrlRole,
        OrderRole,
        BridgeRole,
    };

    explicit PluginPageModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return static_cast<int>(m_rows.size()); }

    // Host-side registration (called from IUiApi::registerPage). Rows keep
    // manifest (order, title) sort — built-ins occupy 0-99 and never live
    // here, plugin pages sort among themselves. Returns false + logs when
    // registration is closed (after finalize) or the descriptor is invalid.
    bool addPage(const QString& pluginId, const pet::PageDescriptor& page);

    // Closes the registration window (host calls after initializeAll so a
    // stray late registerPage is "logged and ignored", §B.3/IUiApi.hpp).
    void finalizeRegistrations();

    Q_INVOKABLE QString qmlUrlFor(const QString& pageKey) const;
    Q_INVOKABLE bool isPluginPage(const QString& pageKey) const;

signals:
    void countChanged();

private:
    QList<PluginPageRow> m_rows; // kept sorted by (order, title)
    bool m_acceptingRegistrations = true;
};

} // namespace core
