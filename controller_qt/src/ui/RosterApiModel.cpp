#include "ui/RosterApiModel.hpp"

// S3 roster dogfooding — see the header for the dependency discipline
// (api/ + Qt only, no core/ includes) and the full-snapshot reset model.

RosterApiModel::RosterApiModel(pet::IInstanceApi* api,
                               pet::IInstanceControlApi* controlApi,
                               QObject* parent)
    : QAbstractListModel(parent), m_api(api), m_controlApi(controlApi)
{
    // Initial snapshot before the first QML binding settles — no reset
    // signals needed (the model is not connected to any view yet).
    reloadFromApi();
    if (m_api != nullptr)
        m_api->subscribeRoster(this);
}

RosterApiModel::~RosterApiModel()
{
    // Unsubscribe BEFORE the QObject base tears down — the API must never
    // fan out into a half-destroyed observer (IInstanceApi §B.3: plugins
    // remove their observers before returning from shutdown; the host UI
    // holds itself to the same discipline).
    if (m_api != nullptr)
        m_api->unsubscribeRoster(this);
}

int RosterApiModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0; // flat list — no children
    return m_rows.size();
}

QVariant RosterApiModel::data(const QModelIndex& index, int role) const
{
    if (index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const pet::InstanceInfo& info = m_rows.at(index.row());
    switch (role) {
    case UuidRole:      return info.uuid;
    case LabelRole:     return info.label;
    case ModelNameRole: return info.modelName;
    case StatusRole:    return info.status;
    case ConnectedRole: return info.connected;
    }
    return {};
}

QHash<int, QByteArray> RosterApiModel::roleNames() const
{
    return {
        {UuidRole,      "uuid"},
        {LabelRole,     "label"},
        {ModelNameRole, "modelName"},
        {StatusRole,    "status"},
        {ConnectedRole, "connected"},
    };
}

void RosterApiModel::rosterChanged()
{
    const int oldCount = m_rows.size();
    beginResetModel();
    reloadFromApi();
    endResetModel();
    if (m_rows.size() != oldCount)
        emit countChanged();
}

void RosterApiModel::reloadFromApi()
{
    m_rows = m_api != nullptr ? m_api->instances()
                              : QVector<pet::InstanceInfo>{};
}

// ── S2 write bridge ─────────────────────────────────────────────────────────

int RosterApiModel::createInstance(const QString& label, const QString& avatar,
                                   const QString& modelName, bool autoStart)
{
    // Thin forward into the shared write API — the roster update comes
    // back through the rosterChanged reset (rowsInserted → fanout), so
    // this model never mutates its own rows here. Null control API
    // (degraded wiring) fails loudly with Generic, never crashes.
    if (m_controlApi == nullptr)
        return static_cast<int>(pet::PluginError::Generic);
    pet::InstanceSpec spec;
    spec.label = label;
    spec.avatar = avatar;
    spec.modelName = modelName;
    spec.autoStart = autoStart;
    return static_cast<int>(m_controlApi->create(spec, /*outUuid=*/nullptr));
}

int RosterApiModel::deleteInstance(const QString& uuid)
{
    if (m_controlApi == nullptr)
        return static_cast<int>(pet::PluginError::Generic);
    return static_cast<int>(m_controlApi->remove(uuid));
}
