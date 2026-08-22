#include "core/DatabaseManager.hpp"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QVariant>

// spdlog MUST be included before logging/Logging.hpp — Logging.hpp references
// the SPDLOG_* macros but does NOT include spdlog headers itself.
#include <spdlog/spdlog.h>
#include "logging/Logging.hpp"

namespace {
// QSqlQuery binds a NULL (default-constructed) QString as SQL NULL, and
// `col = NULL` never matches — the logo ref uses ref_key = "". Normalize
// null strings to empty at the bind boundary so round-trips work.
QVariant normalizeBindValue(const QVariant& v)
{
    if (v.typeId() == QMetaType::QString && v.toString().isNull())
        return QVariant(QStringLiteral(""));
    return v;
}
} // namespace

DatabaseManager::DatabaseManager(QObject* parent)
    : QObject(parent)
    , m_connectionName(QStringLiteral("desktop_pet_db_%1")
                           .arg(reinterpret_cast<quintptr>(this), 0, 16))
{
}

DatabaseManager::~DatabaseManager()
{
    if (m_open)
        QSqlDatabase::removeDatabase(m_connectionName);
}

bool DatabaseManager::open(const QString& path)
{
    if (m_open) {
        LOG_WARN("DatabaseManager: open() called on an already-open db");
        return false;
    }
    if (path.isEmpty()) {
        LOG_WARN("DatabaseManager: open() called with an empty path");
        return false;
    }
    // Ensure the parent dir exists so a fresh install can create app.db.
    const QString dir = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(dir)) {
        LOG_WARN("DatabaseManager: failed to create db dir \"{}\"",
                 dir.toStdString());
        return false;
    }

    QSqlDatabase db = QSqlDatabase::addDatabase(
        QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(path);
    if (!db.open()) {
        LOG_ERROR("DatabaseManager: failed to open \"{}\": {}",
                  path.toStdString(),
                  db.lastError().text().toStdString());
        QSqlDatabase::removeDatabase(m_connectionName);
        return false;
    }

    // Schema creation — CREATE TABLE IF NOT EXISTS is idempotent.
    const QStringList schema = {
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS panel_config ("
            "key TEXT PRIMARY KEY, value TEXT)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS instance_configs ("
            "uuid TEXT PRIMARY KEY, json TEXT)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS assets ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "sha256 TEXT UNIQUE, original_name TEXT, "
            "width INTEGER, height INTEGER, size_bytes INTEGER, "
            "uploaded_at TEXT)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS asset_refs ("
            "asset_id INTEGER REFERENCES assets(id), "
            "ref_type TEXT, ref_key TEXT, "
            "PRIMARY KEY(asset_id, ref_type, ref_key))"),
    };
    for (const QString& sql : schema) {
        if (!exec(sql, {})) {
            LOG_ERROR("DatabaseManager: schema creation failed for \"{}\"",
                      path.toStdString());
            db.close();
            QSqlDatabase::removeDatabase(m_connectionName);
            return false;
        }
    }
    m_open = true;
    LOG_INFO("DatabaseManager: opened \"{}\"", path.toStdString());
    return true;
}

// ── panel_config key-value ────────────────────────────────────────────────

QString DatabaseManager::getValue(const QString& key,
                                  const QString& fallback) const
{
    if (!m_open)
        return fallback;
    const QStringList row = queryRow(
        QStringLiteral("SELECT value FROM panel_config WHERE key = ?"),
        {key});
    return row.isEmpty() ? fallback : row.first();
}

bool DatabaseManager::setValue(const QString& key, const QString& value)
{
    if (!m_open)
        return false;
    return exec(QStringLiteral(
                    "INSERT OR REPLACE INTO panel_config (key, value) "
                    "VALUES (?, ?)"),
                {key, value});
}

// ── instance_configs ──────────────────────────────────────────────────────

bool DatabaseManager::saveInstance(const InstanceConfig& cfg)
{
    if (!m_open)
        return false;
    const QByteArray json = QJsonDocument(instanceConfigToJson(cfg))
                                .toJson(QJsonDocument::Compact);
    return exec(QStringLiteral(
                    "INSERT OR REPLACE INTO instance_configs (uuid, json) "
                    "VALUES (?, ?)"),
                {cfg.id, QString::fromUtf8(json)});
}

std::optional<InstanceConfig>
DatabaseManager::loadInstance(const QString& uuid) const
{
    if (!m_open)
        return std::nullopt;
    const QStringList row = queryRow(
        QStringLiteral("SELECT json FROM instance_configs WHERE uuid = ?"),
        {uuid});
    if (row.isEmpty())
        return std::nullopt;
    const QJsonDocument doc = QJsonDocument::fromJson(row.first().toUtf8());
    if (!doc.isObject()) {
        LOG_WARN("DatabaseManager: corrupt instance row \"{}\"; skipping",
                 uuid.toStdString());
        return std::nullopt;
    }
    return instanceConfigFromJson(doc.object());
}

QList<InstanceConfig> DatabaseManager::loadAllInstances() const
{
    QList<InstanceConfig> out;
    if (!m_open)
        return out;
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    if (!query.exec(QStringLiteral(
            "SELECT uuid, json FROM instance_configs ORDER BY uuid"))) {
        LOG_WARN("DatabaseManager: loadAllInstances query failed: {}",
                 query.lastError().text().toStdString());
        return out;
    }
    while (query.next()) {
        const QJsonDocument doc =
            QJsonDocument::fromJson(query.value(1).toString().toUtf8());
        if (!doc.isObject()) {
            LOG_WARN("DatabaseManager: skipping corrupt instance row \"{}\"",
                     query.value(0).toString().toStdString());
            continue;
        }
        out.append(instanceConfigFromJson(doc.object()));
    }
    return out;
}

bool DatabaseManager::deleteInstance(const QString& uuid)
{
    if (!m_open)
        return false;
    return exec(QStringLiteral(
                    "DELETE FROM instance_configs WHERE uuid = ?"),
                {uuid});
}

// ── assets ────────────────────────────────────────────────────────────────

qint64 DatabaseManager::insertAsset(const QString& sha256,
                                    const QString& originalName, int width,
                                    int height, qint64 sizeBytes,
                                    const QString& uploadedAt)
{
    if (!m_open)
        return -1;
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    query.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO assets "
        "(sha256, original_name, width, height, size_bytes, uploaded_at) "
        "VALUES (?, ?, ?, ?, ?, ?)"));
    query.addBindValue(sha256);
    query.addBindValue(originalName);
    query.addBindValue(width);
    query.addBindValue(height);
    query.addBindValue(sizeBytes);
    query.addBindValue(uploadedAt);
    if (!query.exec()) {
        LOG_WARN("DatabaseManager: insertAsset failed: {}",
                 query.lastError().text().toStdString());
        return -1;
    }
    if (query.numRowsAffected() == 0) {
        // sha256 already present — duplicate reject.
        return -1;
    }
    return query.lastInsertId().toLongLong();
}

QVariantMap DatabaseManager::assetById(qint64 assetId) const
{
    QVariantMap out;
    if (!m_open)
        return out;
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    query.prepare(QStringLiteral(
        "SELECT id, sha256, original_name, width, height, size_bytes, "
        "uploaded_at FROM assets WHERE id = ?"));
    query.addBindValue(assetId);
    if (!query.exec() || !query.next()) {
        return out;
    }
    out.insert(QStringLiteral("id"), query.value(0));
    out.insert(QStringLiteral("sha256"), query.value(1));
    out.insert(QStringLiteral("originalName"), query.value(2));
    out.insert(QStringLiteral("width"), query.value(3));
    out.insert(QStringLiteral("height"), query.value(4));
    out.insert(QStringLiteral("sizeBytes"), query.value(5));
    out.insert(QStringLiteral("uploadedAt"), query.value(6));
    return out;
}

QVariantMap DatabaseManager::assetBySha256(const QString& sha256) const
{
    QVariantMap out;
    if (!m_open)
        return out;
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    query.prepare(QStringLiteral(
        "SELECT id, sha256, original_name, width, height, size_bytes, "
        "uploaded_at FROM assets WHERE sha256 = ?"));
    query.addBindValue(sha256);
    if (!query.exec() || !query.next()) {
        return out;
    }
    out.insert(QStringLiteral("id"), query.value(0));
    out.insert(QStringLiteral("sha256"), query.value(1));
    out.insert(QStringLiteral("originalName"), query.value(2));
    out.insert(QStringLiteral("width"), query.value(3));
    out.insert(QStringLiteral("height"), query.value(4));
    out.insert(QStringLiteral("sizeBytes"), query.value(5));
    out.insert(QStringLiteral("uploadedAt"), query.value(6));
    return out;
}

QList<QVariantMap> DatabaseManager::allAssets() const
{
    QList<QVariantMap> out;
    if (!m_open)
        return out;
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    if (!query.exec(QStringLiteral(
            "SELECT id, sha256, original_name, width, height, size_bytes, "
            "uploaded_at FROM assets ORDER BY uploaded_at DESC"))) {
        LOG_WARN("DatabaseManager: allAssets query failed: {}",
                 query.lastError().text().toStdString());
        return out;
    }
    while (query.next()) {
        QVariantMap m;
        m.insert(QStringLiteral("id"), query.value(0));
        m.insert(QStringLiteral("sha256"), query.value(1));
        m.insert(QStringLiteral("originalName"), query.value(2));
        m.insert(QStringLiteral("width"), query.value(3));
        m.insert(QStringLiteral("height"), query.value(4));
        m.insert(QStringLiteral("sizeBytes"), query.value(5));
        m.insert(QStringLiteral("uploadedAt"), query.value(6));
        out.append(m);
    }
    return out;
}

bool DatabaseManager::deleteAssetRow(qint64 assetId)
{
    if (!m_open)
        return false;
    return exec(QStringLiteral("DELETE FROM assets WHERE id = ?"),
                {assetId});
}

// ── asset_refs ────────────────────────────────────────────────────────────

bool DatabaseManager::attachRef(qint64 assetId, const QString& refType,
                                const QString& refKey)
{
    if (!m_open)
        return false;
    return exec(QStringLiteral(
                    "INSERT OR IGNORE INTO asset_refs "
                    "(asset_id, ref_type, ref_key) VALUES (?, ?, ?)"),
                {assetId, refType, refKey});
}

bool DatabaseManager::detachRef(qint64 assetId, const QString& refType,
                                const QString& refKey)
{
    if (!m_open)
        return false;
    return exec(QStringLiteral(
                    "DELETE FROM asset_refs "
                    "WHERE asset_id = ? AND ref_type = ? AND ref_key = ?"),
                {assetId, refType, refKey});
}

bool DatabaseManager::detachRefsOf(const QString& refType,
                                   const QString& refKey)
{
    if (!m_open)
        return false;
    return exec(QStringLiteral(
                    "DELETE FROM asset_refs WHERE ref_type = ? AND ref_key = ?"),
                {refType, refKey});
}

bool DatabaseManager::inUse(qint64 assetId) const
{
    if (!m_open)
        return false;
    const QStringList row = queryRow(
        QStringLiteral(
            "SELECT COUNT(*) FROM asset_refs WHERE asset_id = ?"),
        {assetId});
    return !row.isEmpty() && row.first().toInt() > 0;
}

QStringList DatabaseManager::refsOf(qint64 assetId) const
{
    QStringList out;
    if (!m_open)
        return out;
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    query.prepare(QStringLiteral(
        "SELECT ref_type, ref_key FROM asset_refs WHERE asset_id = ?"));
    query.addBindValue(assetId);
    if (!query.exec()) {
        return out;
    }
    while (query.next()) {
        const QString type = query.value(0).toString();
        out.append(type == QStringLiteral("logo")
                       ? QStringLiteral("logo")
                       : query.value(1).toString());
    }
    return out;
}

// ── private helpers ───────────────────────────────────────────────────────

bool DatabaseManager::exec(const QString& sql, const QVariantList& args) const
{
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    query.prepare(sql);
    for (const QVariant& arg : args)
        query.addBindValue(normalizeBindValue(arg));
    if (!query.exec()) {
        LOG_WARN("DatabaseManager: exec failed (\"{}\"): {}",
                 sql.left(60).toStdString(),
                 query.lastError().text().toStdString());
        return false;
    }
    return true;
}

QStringList DatabaseManager::queryRow(const QString& sql,
                                      const QVariantList& args) const
{
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    query.prepare(sql);
    for (const QVariant& arg : args)
        query.addBindValue(normalizeBindValue(arg));
    if (!query.exec() || !query.next()) {
        return {};
    }
    QStringList out;
    const int n = query.record().count();
    for (int i = 0; i < n; ++i)
        out.append(query.value(i).toString());
    return out;
}
