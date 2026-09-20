// AssetManagerTest 鈥?import pipeline (ok / duplicate / invalid), delete
// blocked when referenced, setLogo/resetLogo, setInstanceIcon +
// instanceIconUrl. Images are generated QImages saved to a temp dir.
//
// QTEST_MAIN: QImage + QFile + QSqlDatabase are synchronous.

#include "core/AssetManager.hpp"
#include "core/DatabaseManager.hpp"

#include <QFile>
#include <QImage>
#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

namespace {

// Solid-color PNG fixture at <base>/<name>.png.
QString makePng(const QTemporaryDir& base, const QString& name, QRgb color)
{
    QImage img(32, 32, QImage::Format_RGB32);
    img.fill(color);
    const QString path = base.path() + QLatin1Char('/') + name;
    if (!img.save(path, "PNG"))
        return {};
    return path;
}

} // namespace

class AssetManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void testImportOkDuplicateInvalid();
    void testDeleteBlockedWhenReferenced();
    void testSetLogoResetLogo();
    void testSetInstanceIconAndUrl();

private:
    struct Ctx {
        QTemporaryDir base;
        DatabaseManager db;
        AssetManager* am = nullptr;
        ~Ctx() { delete am; }
    };
    // Fresh db + manager rooted at the temp dir. Returns false when setup
    // failed (caller QVERIFY2s).
    bool setup(Ctx& c)
    {
        if (!c.base.isValid())
            return false;
        if (!c.db.open(c.base.path() + QStringLiteral("/app.db")))
            return false;
        c.am = new AssetManager(c.db, c.base.path());
        return true;
    }
};

void AssetManagerTest::testImportOkDuplicateInvalid()
{
    Ctx c;
    QVERIFY2(setup(c), "setup failed");

    const QString png = makePng(c.base, QStringLiteral("cat.png"),
                                qRgb(200, 30, 30));
    QCOMPARE(c.am->importImage(QUrl::fromLocalFile(png)),
             QStringLiteral("ok"));
    QCOMPARE(c.am->assetCount(), 1);

    // Same content re-imported (different filename) 鈫?duplicate, no new row.
    const QString png2 = makePng(c.base, QStringLiteral("copy.png"),
                                 qRgb(200, 30, 30));
    QCOMPARE(c.am->importImage(QUrl::fromLocalFile(png2)),
             QStringLiteral("duplicate"));
    QCOMPARE(c.am->assetCount(), 1);

    // Undecodable file 鈫?invalid.
    const QString junk = c.base.path() + QStringLiteral("/junk.png");
    {
        QFile f(junk);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("this is not an image");
    }
    QCOMPARE(c.am->importImage(QUrl::fromLocalFile(junk)),
             QStringLiteral("invalid"));
    // Missing file 鈫?invalid.
    QCOMPARE(c.am->importImage(QUrl::fromLocalFile(
                  c.base.path() + QStringLiteral("/nope.png"))),
             QStringLiteral("invalid"));

    // assets() exposes the grid map with fileUrl + inUse=false.
    const QVariantList list = c.am->assets();
    QCOMPARE(list.size(), 1);
    const QVariantMap m = list.first().toMap();
    QCOMPARE(m.value(QStringLiteral("name")).toString(),
             QStringLiteral("cat.png"));
    QCOMPARE(m.value(QStringLiteral("width")).toInt(), 32);
    QCOMPARE(m.value(QStringLiteral("inUse")).toBool(), false);
    QVERIFY(m.value(QStringLiteral("fileUrl")).toString().startsWith(
        QStringLiteral("file:///")));

    // Unreferenced delete succeeds: file + row gone.
    const int id = m.value(QStringLiteral("id")).toInt();
    QVERIFY(c.am->deleteAsset(id));
    QCOMPARE(c.am->assetCount(), 0);
}

void AssetManagerTest::testDeleteBlockedWhenReferenced()
{
    Ctx c;
    QVERIFY2(setup(c), "setup failed");

    const QString png = makePng(c.base, QStringLiteral("logo.png"),
                                qRgb(30, 30, 200));
    QCOMPARE(c.am->importImage(QUrl::fromLocalFile(png)),
             QStringLiteral("ok"));
    const int id = c.am->assets().first().toMap()
                       .value(QStringLiteral("id")).toInt();

    QVERIFY(c.am->setLogo(id));
    QVERIFY(!c.am->deleteAsset(id)); // referenced 鈫?refused

    c.am->resetLogo();
    QVERIFY(c.am->deleteAsset(id));  // ref cleared 鈫?deleted
}

void AssetManagerTest::testSetLogoResetLogo()
{
    Ctx c;
    QVERIFY2(setup(c), "setup failed");

    QCOMPARE(c.am->logoAssetId(), -1);          // default
    QVERIFY(c.am->logoUrl().isEmpty());

    const QString png = makePng(c.base, QStringLiteral("paw.png"),
                                qRgb(30, 200, 30));
    QCOMPARE(c.am->importImage(QUrl::fromLocalFile(png)),
             QStringLiteral("ok"));
    const int id = c.am->assets().first().toMap()
                       .value(QStringLiteral("id")).toInt();

    QVERIFY(c.am->setLogo(id));
    QCOMPARE(c.am->logoAssetId(), id);
    QVERIFY(c.am->logoUrl().contains(QStringLiteral(".png")));

    // Switching to a second logo detaches the first (single logo ref).
    const QString png2 = makePng(c.base, QStringLiteral("alt.png"),
                                 qRgb(1, 2, 3));
    QCOMPARE(c.am->importImage(QUrl::fromLocalFile(png2)),
             QStringLiteral("ok"));
    const int id2 = c.am->assets().first().toMap()
                        .value(QStringLiteral("id")).toInt();
    QVERIFY(c.am->setLogo(id2));
    QCOMPARE(c.am->logoAssetId(), id2);

    // setLogo on a missing asset fails.
    QVERIFY(!c.am->setLogo(9999));

    // resetLogo 鈫?default.
    c.am->resetLogo();
    QCOMPARE(c.am->logoAssetId(), -1);
    QVERIFY(c.am->logoUrl().isEmpty());

    // logo_sync_tray kv round-trip.
    QVERIFY(!c.am->logoSyncTray());
    c.am->setLogoSyncTray(true);
    QVERIFY(c.am->logoSyncTray());
}

void AssetManagerTest::testSetInstanceIconAndUrl()
{
    Ctx c;
    QVERIFY2(setup(c), "setup failed");

    const QString png = makePng(c.base, QStringLiteral("icon.png"),
                                qRgb(9, 99, 199));
    QCOMPARE(c.am->importImage(QUrl::fromLocalFile(png)),
             QStringLiteral("ok"));
    const int id = c.am->assets().first().toMap()
                       .value(QStringLiteral("id")).toInt();

    const QString uuid = QStringLiteral("u-1");
    QVERIFY(c.am->instanceIconUrl(uuid).isEmpty());
    QVERIFY(c.am->setInstanceIcon(uuid, id));
    QVERIFY(c.am->instanceIconUrl(uuid).contains(QStringLiteral(".png")));

    // The icon ref makes the asset in-use (delete refused).
    QVERIFY(!c.am->deleteAsset(id));

    // Clearing (-1) detaches; the image file survives.
    QVERIFY(c.am->setInstanceIcon(uuid, -1));
    QVERIFY(c.am->instanceIconUrl(uuid).isEmpty());
    QVERIFY(c.am->deleteAsset(id));

    // Missing asset id 鈫?false.
    QVERIFY(!c.am->setInstanceIcon(uuid, 4242));
}

QTEST_MAIN(AssetManagerTest)
#include "AssetManagerTest.moc"
