#include "core/InstanceConfigManager.hpp"

#include "core/ConfigDir.hpp"

// spdlog MUST be included before logging/Logging.hpp — Logging.hpp references
// the SPDLOG_* macros but does NOT include spdlog headers itself.
#include <spdlog/spdlog.h>
#include "logging/Logging.hpp"

InstanceConfigManager::InstanceConfigManager(const QString& basePath,
                                             QObject* parent)
    : QObject(parent)
    , m_basePath(basePath)
{
}

InstanceConfigManager::~InstanceConfigManager()
{
    delete m_ownedDb;
}

DatabaseManager* InstanceConfigManager::db() const
{
    if (m_sharedDb != nullptr)
        return m_sharedDb;
    if (m_ownedOpenAttempted)
        return m_ownedDb;
    m_ownedOpenAttempted = true;
    auto* owned = new DatabaseManager();
    QString root = m_basePath.isEmpty() ? ConfigDir::configDir() : m_basePath;
    if (!root.endsWith(QLatin1Char('/')))
        root += QLatin1Char('/');
    if (!owned->open(root + QStringLiteral("app.db"))) {
        LOG_WARN("InstanceConfigManager: failed to open app.db under \"{}\"",
                 root.toStdString());
        delete owned;
        return nullptr;
    }
    m_ownedDb = owned;
    return m_ownedDb;
}

bool InstanceConfigManager::save(const InstanceConfig& cfg)
{
    DatabaseManager* database = db();
    if (database == nullptr) {
        LOG_WARN("InstanceConfigManager::save: no database available");
        return false;
    }
    return database->saveInstance(cfg);
}

std::optional<InstanceConfig> InstanceConfigManager::load(const QString& id) const
{
    DatabaseManager* database = db();
    if (database == nullptr)
        return std::nullopt;
    return database->loadInstance(id);
}

bool InstanceConfigManager::deleteInstance(const QString& id)
{
    DatabaseManager* database = db();
    if (database == nullptr)
        return false;
    return database->deleteInstance(id);
}

QList<InstanceConfig> InstanceConfigManager::loadAll() const
{
    DatabaseManager* database = db();
    if (database == nullptr)
        return {};
    return database->loadAllInstances();
}
