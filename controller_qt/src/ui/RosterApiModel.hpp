#pragma once

// RosterApiModel (S3 roster dogfooding) — QAbstractListModel over the
// plugin SDK's pet::IInstanceApi read path. This is the FIRST panel-UI
// consumer of the panel's own plugin API: the sidebar's roster list
// (Main.qml nav pane) binds here instead of to InstanceManager directly,
// so the host UI and plugins observe the roster through the same
// interface and (in production) the SAME InstanceApiImpl object.
//
// Dependency discipline (S3 hard constraint): this TU depends ONLY on
// api/ headers + Qt. It must NOT include core/ headers (InstanceManager /
// InstanceSession) — the roster arrives exclusively via
// IInstanceApi::instances() snapshots and IRosterObserver notifications.
//
// Update model: full-snapshot reset (beginResetModel/endResetModel) on
// every rosterChanged. N is sidebar-sized (~10), so fine-grained
// dataChanged bookkeeping buys nothing.
//
// Thread contract: GUI thread only — instances()/subscribeRoster/notify
// all run there (IInstanceApi §B.3), and QAbstractListModel resets must
// never race a QML render pass.
//
// Role names are byte-identical to InstanceManager's (label, modelName,
// status, connected, uuid) so existing QML delegates bind unchanged.
// InstanceManager's "avatar" role has NO counterpart here: pet::
// InstanceInfo (the frozen v1.0 SDK struct) does not carry it, and the
// sidebar roster delegate does not consume it.

#include <QAbstractListModel>
#include <QHash>
#include <QVector>

#include "api/IInstanceApi.hpp"
#include "api/IInstanceControlApi.hpp"

class RosterApiModel : public QAbstractListModel, private pet::IRosterObserver
{
    Q_OBJECT
    // Roster size for QML badges/labels (mirrors InstanceManager::count —
    // a plain rowCount() call creates no binding dependency, the property
    // re-evaluates on every reset).
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    // Custom roles exposed to QML via roleNames(). Offset from Qt::UserRole
    // so they never collide with Qt's built-in roles. Names mirror
    // InstanceManager's roster roles byte-for-byte (minus "avatar" — see
    // the file header) so sidebar delegates bind unchanged.
    enum Roles {
        UuidRole      = Qt::UserRole + 1,
        LabelRole,
        ModelNameRole,
        StatusRole,
        ConnectedRole,
    };
    Q_ENUM(Roles)

    // api: the shared pet::IInstanceApi (production: core::InstanceApiImpl
    // from the PanelApplication service tree, which outlives this model —
    // it dies with the engine subtree, before the service tree). Null is
    // tolerated (empty, inert model) so degraded wiring never crashes.
    //
    // S2 (v1.2): controlApi is the shared pet::IInstanceControlApi backing
    // the WRITE path (createInstance/deleteInstance below). The host
    // bridge holds the concrete implementation directly — it does NOT go
    // through queryApi and is deliberately NOT capability-gated (design
    // decision: the host UI is the host; only plugin-side access is
    // gated). Null is tolerated (writes return PluginError::Generic —
    // degraded wiring never crashes). api/ + Qt includes only, same
    // dependency discipline as the read path.
    explicit RosterApiModel(pet::IInstanceApi* api,
                            pet::IInstanceControlApi* controlApi = nullptr,
                            QObject* parent = nullptr);
    ~RosterApiModel() override;

    // ── QAbstractListModel overrides ─────────────────────────────────────────
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_rows.size(); }

    // ── S2 write bridge (dogfooding the plugin write API from the UI) ────────
    // Error reporting: the Q_INVOKABLE returns the pet::PluginError code
    // as a plain int (0 = Ok, 4 = NotFound, 5 = Busy, ...) — the simplest
    // surface QML can branch on; no lastError property is kept (callers
    // that need details can correlate with the roster/status roles). Both
    // are fire-and-forget for QML: results land back in this model via
    // the rosterChanged reset.
    Q_INVOKABLE int createInstance(const QString& label,
                                   const QString& avatar = QStringLiteral("🐱"),
                                   const QString& modelName = QString(),
                                   bool autoStart = false);
    Q_INVOKABLE int deleteInstance(const QString& uuid);

signals:
    void countChanged();

private:
    // pet::IRosterObserver — called by the API on the GUI thread after the
    // roster changed; query instances() inside for the fresh snapshot.
    void rosterChanged() override;

    void reloadFromApi();

    pet::IInstanceApi* m_api;               // not owned (service tree / test fake)
    pet::IInstanceControlApi* m_controlApi; // not owned (S2; service tree / fake)
    QVector<pet::InstanceInfo> m_rows; // last snapshot, roster order
};
