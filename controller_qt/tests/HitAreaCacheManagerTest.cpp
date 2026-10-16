// HitAreaCacheManagerTest — the JSON hit-area cache contract, with the
// v1.13 (resource isolation) scope-keyed isolation as the focus. No prior
// test binary covered this class standalone (it was only exercised
// transitively through InstanceSession's model_loaded flow).
//
// Behaviors locked:
//   1. put/get round-trip (legacy 1-arg form — the bare-name host key).
//   2. v1.13 scope isolation: the SAME model name under two different
//      scopes caches as two INDEPENDENT entries — writing one never
//      returns through the other's lookup (and neither collides with the
//      host entry of the same name).
//   3. Empty scope (host) IS the legacy bare-name key: a put through the
//      scoped form with scope="" is visible to the 1-arg get and vice
//      versa — pre-v1.13 cache files keep working untouched.
//   4. A miss returns std::nullopt (unknown model AND unknown scope).
//
// Hermetic: HitAreaCacheManager takes an injectable basePath — a
// QTemporaryDir keeps the cache file out of the real config dir.
//
// QTEST_APPLESS_MAIN — QFile/QJsonDocument are synchronous, no event loop.

#include "core/HitAreaCacheManager.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

namespace {

QStringList areas(const char* a, const char* b = nullptr)
{
    QStringList out;
    out.append(QString::fromLatin1(a));
    if (b != nullptr)
        out.append(QString::fromLatin1(b));
    return out;
}

} // namespace

class HitAreaCacheManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void testPutGetRoundTrip();
    void testScopeKeyedIsolation();
    void testEmptyScopeIsLegacyKey();
    void testMissReturnsNullopt();
};

void HitAreaCacheManagerTest::testPutGetRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");
    HitAreaCacheManager cache(dir.path());

    const QStringList in = areas("Head", "Body");
    cache.put(QStringLiteral("Hiyori"), in);

    const auto out = cache.get(QStringLiteral("Hiyori"));
    QVERIFY2(out.has_value(), "cached entry missing after put");
    QCOMPARE(*out, in);

    // The persisted JSON keeps the legacy bare-name key form.
    QFile f(QDir(dir.path()).absoluteFilePath(
        QStringLiteral("hit_area_cache.json")));
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonObject root =
        QJsonDocument::fromJson(f.readAll()).object();
    QVERIFY(root.contains(QStringLiteral("Hiyori")));
    QCOMPARE(root.value(QStringLiteral("Hiyori")).toArray().size(), 2);
}

void HitAreaCacheManagerTest::testScopeKeyedIsolation()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");
    HitAreaCacheManager cache(dir.path());

    // SAME model name in the host domain and two plugin domains.
    cache.put(QStringLiteral("Same"), QStringLiteral("plugA"),
              areas("A1", "A2"));
    cache.put(QStringLiteral("Same"), QStringLiteral("plugB"),
              areas("B1", "B2"));
    cache.put(QStringLiteral("Same"), areas("Host1", "Host2"));

    const auto host = cache.get(QStringLiteral("Same"));
    const auto a = cache.get(QStringLiteral("Same"), QStringLiteral("plugA"));
    const auto b = cache.get(QStringLiteral("Same"), QStringLiteral("plugB"));

    QVERIFY(host.has_value());
    QVERIFY(a.has_value());
    QVERIFY(b.has_value());
    QCOMPARE(*host, areas("Host1", "Host2"));
    QCOMPARE(*a, areas("A1", "A2"));
    QCOMPARE(*b, areas("B1", "B2"));

    // Overwriting ONE scope leaves the others untouched.
    cache.put(QStringLiteral("Same"), QStringLiteral("plugA"),
              areas("A3"));
    QCOMPARE(*cache.get(QStringLiteral("Same"), QStringLiteral("plugA")),
             areas("A3"));
    QCOMPARE(*cache.get(QStringLiteral("Same"), QStringLiteral("plugB")),
             areas("B1", "B2"));
    QCOMPARE(*cache.get(QStringLiteral("Same")), areas("Host1", "Host2"));
}

void HitAreaCacheManagerTest::testEmptyScopeIsLegacyKey()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");
    HitAreaCacheManager cache(dir.path());

    // Scoped put with "" IS the legacy bare-name key (byte-identical
    // persisted form) — pre-v1.13 cache files keep working, and the two
    // API shapes interoperate.
    cache.put(QStringLiteral("Legacy"), QStringLiteral(""),
              areas("L1", "L2"));
    QVERIFY(cache.get(QStringLiteral("Legacy")).has_value());
    QCOMPARE(*cache.get(QStringLiteral("Legacy")), areas("L1", "L2"));
    QCOMPARE(*cache.get(QStringLiteral("Legacy"), QString()),
             areas("L1", "L2"));

    // The 1-arg put is visible through the scoped get with "".
    cache.put(QStringLiteral("Other"), areas("O1"));
    QCOMPARE(*cache.get(QStringLiteral("Other"), QString()), areas("O1"));

    // Persisted key has NO scope prefix (legacy form).
    QFile f(QDir(dir.path()).absoluteFilePath(
        QStringLiteral("hit_area_cache.json")));
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonObject root =
        QJsonDocument::fromJson(f.readAll()).object();
    QVERIFY(root.contains(QStringLiteral("Legacy")));
    QVERIFY(root.contains(QStringLiteral("Other")));
    QVERIFY(!root.contains(QStringLiteral("/Legacy")));
}

void HitAreaCacheManagerTest::testMissReturnsNullopt()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory creation failed");
    HitAreaCacheManager cache(dir.path());

    cache.put(QStringLiteral("Known"), areas("K1"));

    QVERIFY(!cache.get(QStringLiteral("Unknown")).has_value());
    QVERIFY(!cache.get(QStringLiteral("Known"),
                      QStringLiteral("plugA")).has_value());
    QVERIFY(!cache.get(QStringLiteral("Unknown"),
                      QStringLiteral("plugA")).has_value());
}

QTEST_APPLESS_MAIN(HitAreaCacheManagerTest)
#include "HitAreaCacheManagerTest.moc"
