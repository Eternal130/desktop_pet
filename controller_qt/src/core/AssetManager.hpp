#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include "core/DatabaseManager.hpp"

// AssetManager — QML bridge for the image asset library (资源管理 page) and
// the Logo / instance-icon reference system. Images live on the filesystem
// under <configDir>/assets/uploads/<sha256>.png; only metadata + references
// live in app.db (DatabaseManager). Images are REFERENCED, not owned:
// deleting an instance (or resetting the logo) detaches refs but never
// deletes image files; deleteAsset refuses while refs exist.
//
// Import pipeline (importImage): QImage load-validate (reject undecodable) →
// re-encode as PNG → sha256 of the PNG bytes → duplicate check (same hash →
// "duplicate") → atomic write (QSaveFile) → INSERT row → "ok".
//
// Logo: the current logo asset id persists in panel_config kv under
// "logo_asset_id"; "logo_sync_tray" (bool, "1"/"0") gates the tray-icon sync
// main.cpp performs. resetLogo clears the ref + kv (back to built-in 🐾).
//
// Instance icons: ref_type "instance_icon", ref_key = instance uuid.
// setInstanceIcon(uuid, -1) clears the icon (back to the emoji avatar).
//
// Contract: never throws; failures return false / error strings and are
// logged. All state mutations emit assetsChanged so QML refreshes.
class AssetManager : public QObject
{
    Q_OBJECT
    // Current logo file URL (file:///...) or "" for the built-in default 🐾.
    Q_PROPERTY(QString logoUrl READ logoUrl NOTIFY logoUrlChanged)
    Q_PROPERTY(bool logoSyncTray READ logoSyncTray WRITE setLogoSyncTray
                   NOTIFY logoUrlChanged)

public:
    // db must outlive this manager (main.cpp owns it on the stack).
    // configDir roots the assets/uploads/ tree; empty → ConfigDir::configDir().
    explicit AssetManager(DatabaseManager& db, const QString& configDir = {},
                          QObject* parent = nullptr);

    // ── Library ───────────────────────────────────────────────────────────
    // Import one image file. Returns "ok", "duplicate", or a non-empty
    // error string (undecodable / unreadable / write failed).
    Q_INVOKABLE QString importImage(const QUrl& fileUrl);

    // Delete an asset. Refuses (returns false) while refs exist — QML shows
    // refsOf via assetInfo. On success removes file + db row.
    Q_INVOKABLE bool deleteAsset(int assetId);

    // Grid model: list of maps {id, name, sha256, width, height, sizeBytes,
    // uploadedAt, fileUrl, inUse, refsLabel}. Refresh via assetsChanged.
    Q_INVOKABLE QVariantList assets() const;
    Q_INVOKABLE int assetCount() const;

    // Full metadata for the preview dialog (adds refs list + sha256).
    Q_INVOKABLE QVariantMap assetInfo(int assetId) const;

    // Opens <configDir>/assets/uploads in the OS file explorer.
    Q_INVOKABLE void openAssetsDir();

    // ── Logo ──────────────────────────────────────────────────────────────
    QString logoUrl() const;
    bool logoSyncTray() const;
    void setLogoSyncTray(bool sync);
    // Current logo asset id, or -1 when the built-in default is active.
    Q_INVOKABLE int logoAssetId() const;
    // Attach ref_type=logo to the asset (detaching any previous logo ref)
    // and persist logo_asset_id. Returns false when the asset is missing.
    Q_INVOKABLE bool setLogo(int assetId);
    // Back to the built-in default: clear ref + kv.
    Q_INVOKABLE void resetLogo();

    // ── Instance icons ────────────────────────────────────────────────────
    // assetId -1 clears the icon. Returns false on db failure.
    Q_INVOKABLE bool setInstanceIcon(const QString& uuid, int assetId);
    // File URL of the instance's icon, or "" when it uses the emoji avatar.
    Q_INVOKABLE QString instanceIconUrl(const QString& uuid) const;
    // Detach every instance_icon ref of `uuid` (called on instance delete —
    // never deletes the image file).
    bool detachInstanceIconRefs(const QString& uuid);

signals:
    void assetsChanged();
    void logoUrlChanged();

private:
    // <configDir>/assets/uploads/ (created on demand by importImage).
    QString uploadsDir() const;
    // file:/// URL for the asset's stored PNG, or "" when absent.
    QString assetFileUrl(qint64 assetId) const;

    DatabaseManager& m_db;
    QString m_configDir;
};
