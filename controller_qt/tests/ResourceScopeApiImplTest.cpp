// ResourceScopeApiImplTest (v1.13, rev2 §3.3) — the shared pet::
// IResourceScopeApi over the union resource scans. Locks:
//   1. testScopedModelsOwnerFiltering: host + two plugin data domains →
//      scopedModelsAs(caller, ownerId) projects ONLY the owner's entries
//      with parsed metadata; foreign/empty ownerId → empty (owner check
//      is the family's boundary); the raw interface channel (no caller
//      identity) is empty too.
//   2. testScopedPacksOwnerFiltering: same over the pack union; meta.mko
//      displayName/groupCount parsed; the SAME pack name in two plugin
//      domains is visible to BOTH callers (distinct dirPaths — per
//      (owner, name) keying).
//   3. testRemoveScoped: own-domain model + pack deletes (Ok, dir gone,
//      wired ModelApiImpl cache refreshed); traversal names ("../x",
//      "a/b", "a\\b", "..") → InvalidArgument; cross-owner + empty
//      ownerId → InvalidArgument; missing dir → NotFound.
//   4. testRefreshScanPicksUpChanges: lazy reads see new/removed dirs;
//      refreshScan() returns Ok and re-runs the wired ModelApiImpl cache
//      (resolveModelPath for a newly dropped plugin model works after
//      the refresh, pet.model stays host-only).
//   5. testProxyStampsCaller: the per-context ResourceScopeApiProxy
//      stamps callerId — own ownerId serves, foreign ownerId empties /
//      rejects; a null shared impl is tolerated (empty reads, Generic
//      writes, never a crash).
//
// Fixture strategy: QTemporaryDir with the exact shipped layouts —
// <tmp>/renderer/Resources/{Models,VoicePacks}/... (host domain) and
// <tmp>/PluginData/<owner>/{Models,VoicePacks}/... (plugin data
// domains). The meta.mko fixture is the minimal protobuf wire-format
// writer TuningApiImplTest/MetaMkoParserTest use.
//
// QTEST_APPLESS_MAIN — QDir/QFile are synchronous, no event loop needed.
// Links pet_panel_core (ResourceScopeApiImpl lives there).

#include "api/IResourceScopeApi.hpp"
#include "core/ModelScanner.hpp"
#include "core/PluginContextImpl.hpp"

#include <QDir>
#include <QFile>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

namespace {

// A real-shaped .model3.json (the ModelApiImplTest fixture shape).
QByteArray model3Json(int idleCount, int tapCount)
{
    const QStringList idle = QStringList(idleCount, QStringLiteral("{\"File\":\"i.motion3.json\"}"));
    const QStringList tap = QStringList(tapCount, QStringLiteral("{\"File\":\"t.motion3.json\"}"));
    return QStringLiteral(
        "{\"FileReferences\":{\"Motions\":{\"Idle\":[%1],\"TapBody\":[%2]},"
        "\"Expressions\":[{\"Name\":\"f01\"},{\"Name\":\"f02\"}]},"
        "\"HitAreas\":[{\"Name\":\"Head\"},{\"Name\":\"Body\"}]}")
        .arg(idle.join(QLatin1Char(',')), tap.join(QLatin1Char(',')))
        .toUtf8();
}

// ── Minimal protobuf wire-format serializer (same as TuningApiImplTest) ───
QByteArray encodeVarint(quint64 value)
{
    QByteArray out;
    while (value >= 0x80) {
        out.append(static_cast<char>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    out.append(static_cast<char>(value));
    return out;
}
QByteArray encodeTag(int fieldNumber, int wireType)
{
    return encodeVarint((static_cast<quint64>(fieldNumber) << 3)
                        | static_cast<quint64>(wireType));
}
QByteArray encodeStringField(int fieldNumber, const QString& value)
{
    const QByteArray utf8 = value.toUtf8();
    return encodeTag(fieldNumber, 2) + encodeVarint(utf8.size()) + utf8;
}
QByteArray encodeVarintField(int fieldNumber, quint64 value)
{
    return encodeTag(fieldNumber, 0) + encodeVarint(value);
}
QByteArray encodeMessageField(int fieldNumber, const QByteArray& message)
{
    return encodeTag(fieldNumber, 2) + encodeVarint(message.size()) + message;
}

// One minimal but fully valid Bundle: meta(name/code) + one group.
QByteArray buildMinimalBundle(const QString& displayName)
{
    QByteArray meta;
    meta += encodeStringField(1, displayName);
    meta += encodeStringField(2, QStringLiteral("scope_test_code"));
    QByteArray group;
    group += encodeStringField(1, QStringLiteral("tap_head"));
    group += encodeVarintField(2, 5);
    group += encodeStringField(3, QStringLiteral("Tap Head"));
    QByteArray bundle;
    bundle += encodeMessageField(1, meta);   // meta
    bundle += encodeMessageField(4, group);  // groups[0]
    return bundle;
}

// ── Fixture writers ─────────────────────────────────────────────────────────
bool writeHostModel(const QString& rendererRoot, const QString& name)
{
    const QString dir = QDir(rendererRoot).absoluteFilePath(
        QStringLiteral("Resources/Models/") + name);
    if (!QDir().mkpath(dir))
        return false;
    QFile f(QDir(dir).absoluteFilePath(name + QStringLiteral(".model3.json")));
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(model3Json(2, 1));
    f.close();
    return true;
}

bool writePluginModel(const QString& pluginDataRoot, const QString& owner,
                      const QString& name)
{
    const QString dir = QDir(pluginDataRoot).absoluteFilePath(
        owner + QStringLiteral("/Models/") + name);
    if (!QDir().mkpath(dir))
        return false;
    QFile f(QDir(dir).absoluteFilePath(name + QStringLiteral(".model3.json")));
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(model3Json(1, 2));
    f.close();
    return true;
}

bool writePluginPack(const QString& pluginDataRoot, const QString& owner,
                     const QString& packName, const QString& displayName)
{
    const QString dir = QDir(pluginDataRoot).absoluteFilePath(
        owner + QStringLiteral("/VoicePacks/") + packName);
    if (!QDir().mkpath(dir))
        return false;
    QFile f(QDir(dir).absoluteFilePath(QStringLiteral("meta.mko")));
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(buildMinimalBundle(displayName));
    f.close();
    return true;
}

} // namespace

class ResourceScopeApiImplTest : public QObject
{
    Q_OBJECT

private slots:
    void testScopedModelsOwnerFiltering();
    void testScopedPacksOwnerFiltering();
    void testRemoveScoped();
    void testRefreshScanPicksUpChanges();
    void testProxyStampsCaller();
};

// ── 1. scopedModels owner filtering ─────────────────────────────────────────

void ResourceScopeApiImplTest::testScopedModelsOwnerFiltering()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    const QString rendererRoot = QDir(base.path()).filePath(QStringLiteral("renderer"));
    const QString pluginDataRoot = QDir(base.path()).filePath(QStringLiteral("PluginData"));
    QVERIFY(writeHostModel(rendererRoot, QStringLiteral("Hiyori")));
    QVERIFY(writePluginModel(pluginDataRoot, QStringLiteral("org.a"),
                             QStringLiteral("AModel")));
    QVERIFY(writePluginModel(pluginDataRoot, QStringLiteral("org.b"),
                             QStringLiteral("BModel")));

    core::ResourceScopeApiImpl api;
    api.setScanDirs(rendererRoot, QDir(base.path()).filePath(QStringLiteral("userpacks")),
                   pluginDataRoot);

    // Own domain → only that owner's entries, metadata parsed the same
    // way ModelApiImpl parses model3.json summaries.
    const QVector<pet::ScopedModelInfo> own =
        api.scopedModelsAs(QStringLiteral("org.a"), QStringLiteral("org.a"));
    QCOMPARE(own.size(), 1);
    QCOMPARE(own.at(0).name, QStringLiteral("AModel"));
    QCOMPARE(own.at(0).owner, QStringLiteral("org.a"));
    QCOMPARE(own.at(0).motionGroups,
             (QStringList{QStringLiteral("Idle"), QStringLiteral("TapBody")}));
    QCOMPARE(own.at(0).expressions.size(), 2);
    QCOMPARE(own.at(0).hitAreas.size(), 2);

    // Foreign ownerId → empty (the owner check IS the boundary).
    QVERIFY(api.scopedModelsAs(QStringLiteral("org.a"), QStringLiteral("org.b")).isEmpty());
    // Empty ownerId → empty (malformed request).
    QVERIFY(api.scopedModelsAs(QStringLiteral("org.a"), QString()).isEmpty());
    // Host-domain entries are never served through this family.
    QVERIFY(api.scopedModelsAs(QStringLiteral("Hiyori"), QStringLiteral("Hiyori")).isEmpty());
    // The raw interface channel (no caller identity) fails the check too.
    QVERIFY(api.scopedModels(QStringLiteral("org.a")).isEmpty());
}

// ── 2. scopedPacks owner filtering + cross-plugin same name ────────────────

void ResourceScopeApiImplTest::testScopedPacksOwnerFiltering()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    const QString rendererRoot = QDir(base.path()).filePath(QStringLiteral("renderer"));
    const QString pluginDataRoot = QDir(base.path()).filePath(QStringLiteral("PluginData"));
    // The SAME pack name in TWO plugin domains — both callers must see
    // their own copy (per-(owner, name) keying, distinct dirPaths).
    QVERIFY(writePluginPack(pluginDataRoot, QStringLiteral("org.a"),
                            QStringLiteral("shared_pack"),
                            QStringLiteral("Pack A")));
    QVERIFY(writePluginPack(pluginDataRoot, QStringLiteral("org.b"),
                            QStringLiteral("shared_pack"),
                            QStringLiteral("Pack B")));
    QVERIFY(writePluginPack(pluginDataRoot, QStringLiteral("org.b"),
                            QStringLiteral("only_b"),
                            QStringLiteral("Only B")));

    core::ResourceScopeApiImpl api;
    api.setScanDirs(rendererRoot, QDir(base.path()).filePath(QStringLiteral("userpacks")),
                   pluginDataRoot);

    const QVector<pet::ScopedPackInfo> a =
        api.scopedPacksAs(QStringLiteral("org.a"), QStringLiteral("org.a"));
    QCOMPARE(a.size(), 1);
    QCOMPARE(a.at(0).id, QStringLiteral("shared_pack"));
    QCOMPARE(a.at(0).owner, QStringLiteral("org.a"));
    QCOMPARE(a.at(0).displayName, QStringLiteral("Pack A"));
    QCOMPARE(a.at(0).groupCount, 1);
    QVERIFY(a.at(0).version.isEmpty()); // meta.mko has no version field
    QCOMPARE(a.at(0).dirPath,
             QDir(pluginDataRoot).absoluteFilePath(
                 QStringLiteral("org.a/VoicePacks/shared_pack")));

    const QVector<pet::ScopedPackInfo> b =
        api.scopedPacksAs(QStringLiteral("org.b"), QStringLiteral("org.b"));
    QCOMPARE(b.size(), 2); // shared_pack + only_b
    QCOMPARE(b.at(0).id, QStringLiteral("only_b")); // scan order (name sort)
    QCOMPARE(b.at(0).displayName, QStringLiteral("Only B"));
    QCOMPARE(b.at(1).id, QStringLiteral("shared_pack"));
    QCOMPARE(b.at(1).displayName, QStringLiteral("Pack B"));
    QVERIFY(b.at(1).dirPath != a.at(0).dirPath); // distinct domains

    // Cross-owner / empty → empty; raw interface channel → empty.
    QVERIFY(api.scopedPacksAs(QStringLiteral("org.a"), QStringLiteral("org.b")).isEmpty());
    QVERIFY(api.scopedPacksAs(QStringLiteral("org.a"), QString()).isEmpty());
    QVERIFY(api.scopedPacks(QStringLiteral("org.a")).isEmpty());
}

// ── 3. removeScoped ─────────────────────────────────────────────────────────

void ResourceScopeApiImplTest::testRemoveScoped()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    const QString rendererRoot = QDir(base.path()).filePath(QStringLiteral("renderer"));
    const QString pluginDataRoot = QDir(base.path()).filePath(QStringLiteral("PluginData"));
    QVERIFY(writeHostModel(rendererRoot, QStringLiteral("Hiyori")));
    QVERIFY(writePluginModel(pluginDataRoot, QStringLiteral("org.a"),
                             QStringLiteral("AModel")));
    QVERIFY(writePluginPack(pluginDataRoot, QStringLiteral("org.a"),
                            QStringLiteral("pack_a"), QStringLiteral("Pack A")));

    core::ModelApiImpl modelApi;
    modelApi.setRendererDir(rendererRoot);
    modelApi.setPluginDataRoot(pluginDataRoot);
    core::ResourceScopeApiImpl api;
    api.setScanDirs(rendererRoot, QDir(base.path()).filePath(QStringLiteral("userpacks")),
                   pluginDataRoot);
    api.setModelApi(&modelApi);

    // Cross-owner rejection (a DESTRUCTIVE boundary violation).
    QCOMPARE(api.removeScopedAs(QStringLiteral("org.a"), QStringLiteral("org.b"),
                                QStringLiteral("BModel"), /*isModel=*/true),
             pet::PluginError::InvalidArgument);
    // Empty ownerId rejected.
    QCOMPARE(api.removeScopedAs(QStringLiteral("org.a"), QString(),
                                QStringLiteral("AModel"), /*isModel=*/true),
             pet::PluginError::InvalidArgument);
    // Raw interface channel (no caller identity) rejected the same way.
    QCOMPARE(api.removeScoped(QStringLiteral("org.a"), QStringLiteral("AModel"),
                              /*isModel=*/true),
             pet::PluginError::InvalidArgument);
    // v1.13 hardening (oracle follow-up Should#1): an owner outside the
    // documented id charset — even one that PASSES the caller==owner
    // equality — is rejected at the shared gate before any path
    // construction (backslash traversal defense; the canonicalization
    // containment check below is unchanged and still backs this up).
    QCOMPARE(api.removeScopedAs(QStringLiteral("a.b\\..\\.."),
                                QStringLiteral("a.b\\..\\.."),
                                QStringLiteral("AModel"), /*isModel=*/true),
             pet::PluginError::InvalidArgument);
    QCOMPARE(api.removeScopedAs(QStringLiteral("evil owner"),
                                QStringLiteral("evil owner"),
                                QStringLiteral("AModel"), /*isModel=*/true),
             pet::PluginError::InvalidArgument);

    // Traversal-bearing names never reach the filesystem.
    QCOMPARE(api.removeScopedAs(QStringLiteral("org.a"), QStringLiteral("org.a"),
                                QStringLiteral("../x"), /*isModel=*/true),
             pet::PluginError::InvalidArgument);
    QCOMPARE(api.removeScopedAs(QStringLiteral("org.a"), QStringLiteral("org.a"),
                                QStringLiteral("a/b"), /*isModel=*/true),
             pet::PluginError::InvalidArgument);
    QCOMPARE(api.removeScopedAs(QStringLiteral("org.a"), QStringLiteral("org.a"),
                                QStringLiteral("a\\b"), /*isModel=*/true),
             pet::PluginError::InvalidArgument);
    QCOMPARE(api.removeScopedAs(QStringLiteral("org.a"), QStringLiteral("org.a"),
                                QStringLiteral(".."), /*isModel=*/false),
             pet::PluginError::InvalidArgument);
    QCOMPARE(api.removeScopedAs(QStringLiteral("org.a"), QStringLiteral("org.a"),
                                QString(), /*isModel=*/true),
             pet::PluginError::InvalidArgument);
    // Nothing was touched by the rejections above.
    QVERIFY(QFile::exists(QDir(pluginDataRoot).absoluteFilePath(
        QStringLiteral("org.a/Models/AModel/AModel.model3.json"))));

    // Missing resource → NotFound.
    QCOMPARE(api.removeScopedAs(QStringLiteral("org.a"), QStringLiteral("org.a"),
                                QStringLiteral("Ghost"), /*isModel=*/true),
             pet::PluginError::NotFound);

    // Own-domain MODEL delete → Ok + dir gone + the wired ModelApiImpl
    // cache refreshed (the union side index loses the entry).
    QCOMPARE(modelApi.resolveModelPath(QStringLiteral("AModel"),
                                       QStringLiteral("org.a")),
             QDir(pluginDataRoot).absoluteFilePath(
                 QStringLiteral("org.a/Models/AModel/AModel.model3.json")));
    QCOMPARE(api.removeScopedAs(QStringLiteral("org.a"), QStringLiteral("org.a"),
                                QStringLiteral("AModel"), /*isModel=*/true),
             pet::PluginError::Ok);
    QVERIFY(!QDir(pluginDataRoot).exists(
        QStringLiteral("org.a/Models/AModel")));
    QVERIFY(modelApi.resolveModelPath(QStringLiteral("AModel"),
                                      QStringLiteral("org.a")).isEmpty());
    // pet.model stays host-only throughout.
    QCOMPARE(modelApi.availableModels().size(), 1);

    // Own-domain PACK delete → Ok + dir gone.
    QCOMPARE(api.removeScopedAs(QStringLiteral("org.a"), QStringLiteral("org.a"),
                                QStringLiteral("pack_a"), /*isModel=*/false),
             pet::PluginError::Ok);
    QVERIFY(!QDir(pluginDataRoot).exists(
        QStringLiteral("org.a/VoicePacks/pack_a")));
    QVERIFY(api.scopedPacksAs(QStringLiteral("org.a"),
                              QStringLiteral("org.a")).isEmpty());
}

// ── 4. refreshScan picks up new/removed dirs ───────────────────────────────

void ResourceScopeApiImplTest::testRefreshScanPicksUpChanges()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    const QString rendererRoot = QDir(base.path()).filePath(QStringLiteral("renderer"));
    const QString pluginDataRoot = QDir(base.path()).filePath(QStringLiteral("PluginData"));
    QVERIFY(writePluginModel(pluginDataRoot, QStringLiteral("org.a"),
                             QStringLiteral("AModel")));

    core::ModelApiImpl modelApi;
    modelApi.setRendererDir(rendererRoot);
    modelApi.setPluginDataRoot(pluginDataRoot);
    core::ResourceScopeApiImpl api;
    api.setScanDirs(rendererRoot, QDir(base.path()).filePath(QStringLiteral("userpacks")),
                   pluginDataRoot);
    api.setModelApi(&modelApi);
    QCOMPARE(api.scopedModelsAs(QStringLiteral("org.a"),
                                QStringLiteral("org.a")).size(), 1);

    // A second model lands on disk AFTER construction — the lazy read
    // sees it, refreshScan() stays Ok, and the wired ModelApiImpl cache
    // picks the new entry up through the refresh (resolveModelPath hit).
    QVERIFY(writePluginModel(pluginDataRoot, QStringLiteral("org.a"),
                             QStringLiteral("Second")));
    QCOMPARE(api.refreshScan(), pet::PluginError::Ok);
    QVERIFY(!modelApi.resolveModelPath(QStringLiteral("Second"),
                                       QStringLiteral("org.a")).isEmpty());
    QCOMPARE(api.scopedModelsAs(QStringLiteral("org.a"),
                                QStringLiteral("org.a")).size(), 2);

    // A removed dir disappears from both surfaces.
    QVERIFY(QDir(QDir(pluginDataRoot).absoluteFilePath(
                     QStringLiteral("org.a/Models/Second")))
            .removeRecursively());
    QCOMPARE(api.refreshScan(), pet::PluginError::Ok);
    QCOMPARE(api.scopedModelsAs(QStringLiteral("org.a"),
                                QStringLiteral("org.a")).size(), 1);
    QVERIFY(modelApi.resolveModelPath(QStringLiteral("Second"),
                                      QStringLiteral("org.a")).isEmpty());
}

// ── 5. per-context proxy stamps the caller ─────────────────────────────────

void ResourceScopeApiImplTest::testProxyStampsCaller()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    const QString rendererRoot = QDir(base.path()).filePath(QStringLiteral("renderer"));
    const QString pluginDataRoot = QDir(base.path()).filePath(QStringLiteral("PluginData"));
    QVERIFY(writePluginModel(pluginDataRoot, QStringLiteral("org.a"),
                             QStringLiteral("AModel")));
    QVERIFY(writePluginPack(pluginDataRoot, QStringLiteral("org.a"),
                            QStringLiteral("pack_a"), QStringLiteral("Pack A")));

    core::ResourceScopeApiImpl impl;
    impl.setScanDirs(rendererRoot, QDir(base.path()).filePath(QStringLiteral("userpacks")),
                     pluginDataRoot);
    core::ResourceScopeApiProxy proxyA(QStringLiteral("org.a"), &impl);
    core::ResourceScopeApiProxy proxyB(QStringLiteral("org.b"), &impl);

    // Own ownerId serves through the proxy (caller stamped).
    QCOMPARE(proxyA.scopedModels(QStringLiteral("org.a")).size(), 1);
    QCOMPARE(proxyA.scopedPacks(QStringLiteral("org.a")).size(), 1);
    // Foreign ownerId → empty (proxy B cannot even see org.a's pack).
    QVERIFY(proxyB.scopedModels(QStringLiteral("org.a")).isEmpty());
    QVERIFY(proxyB.scopedPacks(QStringLiteral("org.a")).isEmpty());
    QVERIFY(proxyB.scopedPacks(QStringLiteral("org.b")).isEmpty());
    // Destructive foreign attempt → InvalidArgument.
    QCOMPARE(proxyB.removeScoped(QStringLiteral("org.a"),
                                 QStringLiteral("AModel"), /*isModel=*/true),
             pet::PluginError::InvalidArgument);
    QCOMPARE(proxyA.refreshScan(), pet::PluginError::Ok);

    // Null shared impl: tolerated, never a crash (degraded wiring).
    core::ResourceScopeApiProxy dead(QStringLiteral("org.a"), nullptr);
    QVERIFY(dead.scopedModels(QStringLiteral("org.a")).isEmpty());
    QVERIFY(dead.scopedPacks(QStringLiteral("org.a")).isEmpty());
    QCOMPARE(dead.removeScoped(QStringLiteral("org.a"), QStringLiteral("x"),
                               /*isModel=*/true),
             pet::PluginError::Generic);
    QCOMPARE(dead.refreshScan(), pet::PluginError::Generic);
}

QTEST_APPLESS_MAIN(ResourceScopeApiImplTest)
#include "ResourceScopeApiImplTest.moc"
