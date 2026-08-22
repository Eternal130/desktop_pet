#include "core/AssetManager.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSaveFile>
#include <QUrl>
#include <QBuffer>

#include "core/ConfigDir.hpp"

// spdlog MUST be included before logging/Logging.hpp — Logging.hpp references
// the SPDLOG_* macros but does NOT include spdlog headers itself.
#include <spdlog/spdlog.h>
#include "logging/Logging.hpp"

namespace {
const QString kRefTypeLogo = QStringLiteral("logo");
const QString kRefTypeInstanceIcon = QStringLiteral("instance_icon");
const QString kKeyLogoAssetId = QStringLiteral("logo_asset_id");
const QString kKeyLogoSyncTray = QStringLiteral("logo_sync_tray");
} // namespace

AssetManager::AssetManager(DatabaseManager& db, const QString& configDir,
                           QObject* parent)
    : QObject(parent)
    , m_db(db)
    , m_configDir(configDir.isEmpty() ? ConfigDir::configDir() : configDir)
{
}

QString AssetManager::uploadsDir() const
{
    QString root = m_configDir;
    if (!root.endsWith(QLatin1Char('/')))
        root += QLatin1Char('/');
    return root + QStringLiteral("assets/uploads/");
}

QString AssetManager::assetFileUrl(qint64 assetId) const
{
    const QVariantMap asset = m_db.assetById(assetId);
    if (asset.isEmpty())
        return {};
    const QString path = uploadsDir() +
        asset.value(QStringLiteral("sha256")).toString() +
        QStringLiteral(".png");
    if (!QFile::exists(path))
        return {};
    return QUrl::fromLocalFile(path).toString();
}

QString AssetManager::importImage(const QUrl& fileUrl)
{
    const QString source = fileUrl.toLocalFile();
    if (source.isEmpty() || !QFile::exists(source))
        return QStringLiteral("invalid");

    QImage image(source);
    if (image.isNull()) {
        LOG_WARN("AssetManager: importImage cannot decode \"{}\"",
                 source.toStdString());
        return QStringLiteral("invalid");
    }

    // Re-encode as PNG — uniform storage format, hash over the canonical
    // bytes so a re-imported identical image dedupes regardless of the
    // original container (JPG/WEBP/ICO).
    const QByteArray png = [&]() {
        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        return buffer.data();
    }();
    if (png.isEmpty()) {
        LOG_WARN("AssetManager: importImage PNG re-encode failed \"{}\"",
                 source.toStdString());
        return QStringLiteral("encode_failed");
    }
    const QByteArray hashBytes =
        QCryptographicHash::hash(png, QCryptographicHash::Sha256);
    const QString sha256 = QString::fromLatin1(hashBytes.toHex());

    if (!m_db.assetBySha256(sha256).isEmpty())
        return QStringLiteral("duplicate");

    // Atomic write: <uploads>/<sha256>.png via QSaveFile.
    const QString dir = uploadsDir();
    if (!QDir().mkpath(dir)) {
        LOG_WARN("AssetManager: cannot create uploads dir \"{}\"",
                 dir.toStdString());
        return QStringLiteral("write_failed");
    }
    const QString dest = dir + sha256 + QStringLiteral(".png");
    if (!QFile::exists(dest)) {
        QSaveFile f(dest);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
            f.write(png) != png.size() || !f.commit()) {
            LOG_WARN("AssetManager: atomic write failed for \"{}\"",
                     dest.toStdString());
            return QStringLiteral("write_failed");
        }
    }

    const QString uploadedAt =
        QDateTime::currentDateTime().toString(Qt::ISODate);
    const qint64 id = m_db.insertAsset(
        sha256, QFileInfo(source).fileName(), image.width(), image.height(),
        png.size(), uploadedAt);
    if (id < 0) {
        LOG_WARN("AssetManager: insertAsset rejected \"{}\" (hash clash?)",
                 source.toStdString());
        return QStringLiteral("db_failed");
    }
    LOG_INFO("AssetManager: imported \"{}\" as {}x{} sha256={} id={}",
             QFileInfo(source).fileName().toStdString(), image.width(),
             image.height(), sha256.toStdString(), id);
    emit assetsChanged();
    return QStringLiteral("ok");
}

bool AssetManager::deleteAsset(int assetId)
{
    if (m_db.inUse(assetId)) {
        LOG_INFO("AssetManager: deleteAsset({}) refused — asset in use",
                 assetId);
        return false;
    }
    const QVariantMap asset = m_db.assetById(assetId);
    if (asset.isEmpty())
        return false;
    const QString path = uploadsDir() +
        asset.value(QStringLiteral("sha256")).toString() +
        QStringLiteral(".png");
    if (QFile::exists(path) && !QFile::remove(path)) {
        LOG_WARN("AssetManager: cannot remove file \"{}\"",
                 path.toStdString());
        return false;
    }
    if (!m_db.deleteAssetRow(assetId))
        return false;
    LOG_INFO("AssetManager: deleted asset id={}", assetId);
    emit assetsChanged();
    return true;
}

QVariantList AssetManager::assets() const
{
    QVariantList out;
    const QList<QVariantMap> rows = m_db.allAssets();
    for (const QVariantMap& row : rows) {
        const qint64 id = row.value(QStringLiteral("id")).toLongLong();
        const QStringList refs = m_db.refsOf(id);
        QVariantMap m;
        m.insert(QStringLiteral("id"), row.value(QStringLiteral("id")));
        m.insert(QStringLiteral("name"),
                 row.value(QStringLiteral("originalName")));
        m.insert(QStringLiteral("sha256"),
                 row.value(QStringLiteral("sha256")));
        m.insert(QStringLiteral("width"), row.value(QStringLiteral("width")));
        m.insert(QStringLiteral("height"),
                 row.value(QStringLiteral("height")));
        m.insert(QStringLiteral("sizeBytes"),
                 row.value(QStringLiteral("sizeBytes")));
        m.insert(QStringLiteral("uploadedAt"),
                 row.value(QStringLiteral("uploadedAt")));
        m.insert(QStringLiteral("fileUrl"), assetFileUrl(id));
        m.insert(QStringLiteral("inUse"), !refs.isEmpty());
        m.insert(QStringLiteral("refsLabel"), refs.join(QStringLiteral(", ")));
        out.append(m);
    }
    return out;
}

int AssetManager::assetCount() const
{
    return static_cast<int>(m_db.allAssets().size());
}

QVariantMap AssetManager::assetInfo(int assetId) const
{
    QVariantMap m = m_db.assetById(assetId);
    if (m.isEmpty())
        return m;
    const qint64 id = m.value(QStringLiteral("id")).toLongLong();
    m.insert(QStringLiteral("fileUrl"), assetFileUrl(id));
    const QStringList refs = m_db.refsOf(id);
    m.insert(QStringLiteral("inUse"), !refs.isEmpty());
    m.insert(QStringLiteral("refs"), refs);
    return m;
}

void AssetManager::openAssetsDir()
{
    QDir().mkpath(uploadsDir());
    QDesktopServices::openUrl(QUrl::fromLocalFile(uploadsDir()));
}

// ── Logo ──────────────────────────────────────────────────────────────────

QString AssetManager::logoUrl() const
{
    return assetFileUrl(logoAssetId());
}

bool AssetManager::logoSyncTray() const
{
    return m_db.getValue(kKeyLogoSyncTray, QStringLiteral("0")) ==
           QStringLiteral("1");
}

void AssetManager::setLogoSyncTray(bool sync)
{
    m_db.setValue(kKeyLogoSyncTray,
                  sync ? QStringLiteral("1") : QStringLiteral("0"));
    emit logoUrlChanged();
}

int AssetManager::logoAssetId() const
{
    bool ok = false;
    const int id = m_db.getValue(kKeyLogoAssetId, QStringLiteral("-1"))
                       .toInt(&ok);
    return ok ? id : -1;
}

bool AssetManager::setLogo(int assetId)
{
    if (m_db.assetById(assetId).isEmpty()) {
        LOG_WARN("AssetManager::setLogo: asset {} not found", assetId);
        return false;
    }
    m_db.detachRefsOf(kRefTypeLogo, QString());
    if (!m_db.attachRef(assetId, kRefTypeLogo, QString()))
        return false;
    m_db.setValue(kKeyLogoAssetId, QString::number(assetId));
    LOG_INFO("AssetManager: logo set to asset {}", assetId);
    emit logoUrlChanged();
    emit assetsChanged();
    return true;
}

void AssetManager::resetLogo()
{
    m_db.detachRefsOf(kRefTypeLogo, QString());
    m_db.setValue(kKeyLogoAssetId, QStringLiteral("-1"));
    LOG_INFO("AssetManager: logo reset to built-in default");
    emit logoUrlChanged();
    emit assetsChanged();
}

// ── Instance icons ────────────────────────────────────────────────────────

bool AssetManager::setInstanceIcon(const QString& uuid, int assetId)
{
    // Clear any previous icon ref for this instance first.
    m_db.detachRefsOf(kRefTypeInstanceIcon, uuid);
    if (assetId < 0) {
        emit assetsChanged();
        return true;
    }
    if (m_db.assetById(assetId).isEmpty()) {
        LOG_WARN("AssetManager::setInstanceIcon: asset {} not found", assetId);
        return false;
    }
    if (!m_db.attachRef(assetId, kRefTypeInstanceIcon, uuid))
        return false;
    LOG_INFO("AssetManager: instance \"{}\" icon set to asset {}",
             uuid.toStdString(), assetId);
    emit assetsChanged();
    return true;
}

QString AssetManager::instanceIconUrl(const QString& uuid) const
{
    const QList<QVariantMap> rows = m_db.allAssets();
    for (const QVariantMap& row : rows) {
        const qint64 id = row.value(QStringLiteral("id")).toLongLong();
        const QStringList refs = m_db.refsOf(id);
        if (refs.contains(uuid))
            return assetFileUrl(id);
    }
    return {};
}

bool AssetManager::detachInstanceIconRefs(const QString& uuid)
{
    return m_db.detachRefsOf(kRefTypeInstanceIcon, uuid);
}
