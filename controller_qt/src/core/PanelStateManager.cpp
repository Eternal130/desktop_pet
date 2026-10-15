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
    // Fix 2: clear at entry — the flag reflects THIS manager's last load.
    // (PanelConfigController/SettingsApiImpl build a fresh manager per
    // load-modify-save, so a transient failure blocks exactly that write;
    // the next write re-attempts the load.)
    m_lastLoadFailed = false;

    DatabaseManager* database = db();
    if (database == nullptr) {
        // db() already logged the open failure — defaults are the only
        // honest answer, but a subsequent save() must NOT persist them
        // over state we could not read.
        m_lastLoadFailed = true;
        return defaultPanelConfig();
    }
    bool ok = false;
    const QString blob = database->getValue(kPanelKey, &ok);
    if (!ok) {
        // QUERY FAILED (SQL error on an open db) — NOT "row absent". Same
        // discipline as the open-failure path: defaults out, save refused.
        LOG_WARN("PanelStateManager: panel_config query failed — returning "
                 "defaults; save() will refuse until a load succeeds");
        m_lastLoadFailed = true;
        return defaultPanelConfig();
    }
    if (blob.isEmpty())
        return defaultPanelConfig(); // genuinely absent row — first-run
    const QJsonDocument doc = QJsonDocument::fromJson(blob.toUtf8());
    if (!doc.isObject()) {
        LOG_WARN("PanelStateManager: corrupt panel row; using defaults");
        return defaultPanelConfig();
    }
    return panelConfigFromJson(doc.object());
}

bool PanelStateManager::save(const PanelConfig& cfg)
{
    if (m_lastLoadFailed) {
        LOG_WARN("PanelStateManager: refusing to save — the previous "
                 "load() failed (db open/query error), so writing would "
                 "persist a defaults-seeded blob over the on-disk state");
        return false;
    }
    DatabaseManager* database = db();
    if (database == nullptr)
        return false;
    const QByteArray data =
        QJsonDocument(panelConfigToJson(cfg)).toJson(QJsonDocument::Compact);
    return database->setValue(kPanelKey, QString::fromUtf8(data));
}
