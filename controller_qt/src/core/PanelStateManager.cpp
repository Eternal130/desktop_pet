#include "core/PanelStateManager.hpp"

#include <QJsonDocument>
#include <QJsonObject>

#include "core/ConfigDir.hpp"

// spdlog MUST be included before logging/Logging.hpp — Logging.hpp references
// the SPDLOG_* macros but does NOT include spdlog headers itself.
#include <spdlog/spdlog.h>
#include "logging/Logging.hpp"

namespace {
// The single panel_config key holding the serialized PanelConfig JSON blob.
const QString kPanelKey = QStringLiteral("panel");
} // namespace

PanelStateManager::PanelStateManager(const QString& basePath, QObject* parent)
    : QObject(parent)
    , m_basePath(basePath)
{
}

PanelStateManager::~PanelStateManager()
{
    delete m_ownedDb;
}

DatabaseManager* PanelStateManager::db() const
{
    if (m_sharedDb != nullptr)
        return m_sharedDb;
    if (m_ownedOpenAttempted)
        return m_ownedDb; // may be null after a failed open
    m_ownedOpenAttempted = true;
    auto* owned = new DatabaseManager();
    QString root = m_basePath.isEmpty() ? ConfigDir::configDir() : m_basePath;
    if (!root.endsWith(QLatin1Char('/')))
        root += QLatin1Char('/');
    if (!owned->open(root + QStringLiteral("app.db"))) {
        LOG_WARN("PanelStateManager: failed to open app.db under \"{}\"; "
                 "load() will return defaults, save() will fail",
                 root.toStdString());
        delete owned;
        return nullptr;
    }
    m_ownedDb = owned;
    return m_ownedDb;
}

PanelConfig PanelStateManager::load()
{
    DatabaseManager* database = db();
    if (database == nullptr)
        return defaultPanelConfig();
    const QString blob = database->getValue(kPanelKey);
    if (blob.isEmpty())
        return defaultPanelConfig();
    const QJsonDocument doc = QJsonDocument::fromJson(blob.toUtf8());
    if (!doc.isObject()) {
        LOG_WARN("PanelStateManager: corrupt panel row; using defaults");
        return defaultPanelConfig();
    }
    return panelConfigFromJson(doc.object());
}

bool PanelStateManager::save(const PanelConfig& cfg)
{
    DatabaseManager* database = db();
    if (database == nullptr)
        return false;
    const QByteArray data =
        QJsonDocument(panelConfigToJson(cfg)).toJson(QJsonDocument::Compact);
    return database->setValue(kPanelKey, QString::fromUtf8(data));
}
