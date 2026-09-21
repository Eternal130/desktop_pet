// TuningApiImplTest (S5, v1.3) — the real pet::ITuningApi implementation
// over a REAL InstanceManager harness (same QTemporaryDir + WsServer +
// PendingRequests + savePanel-lambda pattern as InstanceControlApiImplTest).
// Locks:
//   1. testSettersForward: setOpacity/setVolume/setMuted/setFps forward to
//      the session (observable via the Q_PROPERTY NOTIFYs + config persist)
//      and return Ok.
//   2. testTriggersForward: playMotion/setExpression/triggerHitArea are
//      accepted (Ok); empty group/expression/area → InvalidArgument.
//   3. testUnknownUuidNotFound: every op on a uuid not in the roster →
//      NotFound (incl. the null-manager degraded-wiring case).
//   4. testPendingDeleteIsBusy: after setDeletePending (the public two-
//      phase mark — the same state InstanceManager::deleteInstance sets),
//      EVERY op returns Busy.
//   5. testMountResolvesPackIdByDirectoryName: a synthesized valid meta.mko
//      under the injected scan dirs → mount by DIRECTORY NAME succeeds
//      (absolute dir lands on the session, persisted); an unknown packId →
//      NotFound; a PATH (existing absolute dir with unknown name) →
//      NotFound (paths are never interpreted); empty packId →
//      InvalidArgument; a discovered dir whose meta.mko is garbage →
//      Generic; unmount → Ok + persisted empty.
//
// The meta.mko fixture is serialized with the same minimal protobuf
// wire-format writer MetaMkoParserTest uses (byte-identical to protoc).
//
// QTEST_MAIN — parity with the other InstanceManager-harness tests (no
// async stop is driven here, but WsServer machinery is constructed).
// Links pet_panel_core (TuningApiImpl lives there).

#include "core/DatabaseManager.hpp"
#include "core/InstanceConfig.hpp"
#include "core/InstanceConfigManager.hpp"
#include "core/InstanceManager.hpp"
#include "core/InstanceSession.hpp"
#include "core/PanelConfig.hpp"
#include "core/PanelStateManager.hpp"
#include "core/PluginContextImpl.hpp"
#include "network/PendingRequests.hpp"
#include "network/WsServer.hpp"

#include <QDir>
#include <QFile>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

namespace {

// ── Minimal protobuf wire-format serializer (same as MetaMkoParserTest) ────
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

// One minimal but fully valid Bundle: meta(name/code) + one tap_head group
// + one action with motion+audio+doc.
QByteArray buildMinimalBundle()
{
    QByteArray meta;
    meta += encodeStringField(1, QStringLiteral("Tuning Test Pack"));
    meta += encodeStringField(2, QStringLiteral("tuning_pack"));
    QByteArray group;
    group += encodeStringField(1, QStringLiteral("tap_head"));
    group += encodeVarintField(2, 5);
    group += encodeStringField(3, QStringLiteral("Tap Head"));
    QByteArray action;
    action += encodeVarintField(1, 42);
    action += encodeStringField(2, QStringLiteral("tap_head"));
    action += encodeStringField(3, QStringLiteral("motions/tap01.motion3.json"));
    action += encodeStringField(4, QStringLiteral("audio/tap01.wav"));
    action += encodeStringField(6, QStringLiteral("Tuning tap"));
    action += encodeVarintField(7, 300);
    action += encodeVarintField(8, 500);

    QByteArray bundle;
    bundle += encodeMessageField(1, meta);      // meta
    bundle += encodeMessageField(4, group);     // groups[0]
    bundle += encodeMessageField(5, action);    // actions[0]
    return bundle;
}

// Write a pack directory with the given meta.mko bytes under
// <rendererRoot>/Resources/VoicePacks/<packName>/.
bool writePack(const QString& rendererRoot, const QString& packName,
               const QByteArray& mkoBytes)
{
    const QString packDir = QDir(rendererRoot).absoluteFilePath(
        QStringLiteral("Resources/VoicePacks/") + packName);
    if (!QDir().mkpath(packDir))
        return false;
    QFile f(QDir(packDir).absoluteFilePath(QStringLiteral("meta.mko")));
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(mkoBytes);
    f.close();
    return true;
}

} // namespace

class TuningApiImplTest : public QObject
{
    Q_OBJECT

private slots:
    void testSettersForward();
    void testTriggersForward();
    void testUnknownUuidNotFound();
    void testPendingDeleteIsBusy();
    void testMountResolvesPackIdByDirectoryName();
};

// ── 1. setters forward ──────────────────────────────────────────────────────

void TuningApiImplTest::testSettersForward()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    WsServer server;
    PendingRequests pending;
    InstanceManager mgr(base.path(), server, pending,
                        [&base](const PanelConfig& cfg) {
                            PanelStateManager(base.path()).save(cfg);
                        });
    core::TuningApiImpl api(&mgr, nullptr);

    const QString uuid = mgr.createInstance(QStringLiteral("Tune"));
    QVERIFY(!uuid.isEmpty());
    InstanceSession* session = mgr.instanceAt(0);
    QVERIFY(session != nullptr);

    QSignalSpy opacitySpy(session, &InstanceSession::opacityChanged);
    QSignalSpy volumeSpy(session, &InstanceSession::volumeChanged);
    QSignalSpy mutedSpy(session, &InstanceSession::mutedChanged);
    QSignalSpy fpsSpy(session, &InstanceSession::targetFpsChanged);

    QCOMPARE(api.setOpacity(uuid, 0.55), pet::PluginError::Ok);
    QCOMPARE(api.setVolume(uuid, 0.3), pet::PluginError::Ok);
    QCOMPARE(api.setMuted(uuid, true), pet::PluginError::Ok);
    QCOMPARE(api.setFps(uuid, 45), pet::PluginError::Ok);

    QCOMPARE(opacitySpy.count(), 1);
    QCOMPARE(volumeSpy.count(), 1);
    QCOMPARE(mutedSpy.count(), 1);
    QCOMPARE(fpsSpy.count(), 1);
    QCOMPARE(session->opacity(), 0.55);
    QCOMPARE(session->volume(), 0.3);
    QVERIFY(session->muted());
    QCOMPARE(session->targetFps(), 45);

    // Same-value writes are absorbed by the session (no second NOTIFY) —
    // the API still reports Ok (accepted, idempotent).
    QCOMPARE(api.setFps(uuid, 45), pet::PluginError::Ok);
    QCOMPARE(fpsSpy.count(), 1);

    // The setters mirror into the session's live config snapshot (the
    // runtime values ride the startup salvo on the next launch — the
    // setters deliberately do NOT rewrite the instance file).
    QCOMPARE(session->config().opacity, 0.55);
    QCOMPARE(session->config().targetFps, 45);
    QVERIFY(session->config().muted);
}

// ── 2. motion/expression/hit triggers ───────────────────────────────────────

void TuningApiImplTest::testTriggersForward()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    WsServer server;
    PendingRequests pending;
    InstanceManager mgr(base.path(), server, pending,
                        [&base](const PanelConfig& cfg) {
                            PanelStateManager(base.path()).save(cfg);
                        });
    core::TuningApiImpl api(&mgr, nullptr);

    const QString uuid = mgr.createInstance(QStringLiteral("Trig"));
    QVERIFY(!uuid.isEmpty());
    InstanceSession* session = mgr.instanceAt(0);
    QVERIFY(session != nullptr);

    // Accepted (the renderer ignores unknown ids; the return is "accepted").
    QCOMPARE(api.playMotion(uuid, QStringLiteral("Idle"), 2),
             pet::PluginError::Ok);
    QCOMPARE(api.setExpression(uuid, QStringLiteral("smile")),
             pet::PluginError::Ok);
    QCOMPARE(api.triggerHitArea(uuid, QStringLiteral("Head")),
             pet::PluginError::Ok);

    // Empty strings are malformed arguments.
    QCOMPARE(api.playMotion(uuid, QString(), 0), pet::PluginError::InvalidArgument);
    QCOMPARE(api.setExpression(uuid, QString()), pet::PluginError::InvalidArgument);
    QCOMPARE(api.triggerHitArea(uuid, QString()), pet::PluginError::InvalidArgument);
}

// ── 3. unknown uuid → NotFound (incl. degraded wiring) ──────────────────────

void TuningApiImplTest::testUnknownUuidNotFound()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    WsServer server;
    PendingRequests pending;
    InstanceManager mgr(base.path(), server, pending,
                        [&base](const PanelConfig& cfg) {
                            PanelStateManager(base.path()).save(cfg);
                        });
    core::TuningApiImpl api(&mgr, nullptr);
    const QString gone = QStringLiteral("no-such-uuid");

    QCOMPARE(api.setOpacity(gone, 0.5), pet::PluginError::NotFound);
    QCOMPARE(api.setVolume(gone, 0.5), pet::PluginError::NotFound);
    QCOMPARE(api.setMuted(gone, true), pet::PluginError::NotFound);
    QCOMPARE(api.setFps(gone, 30), pet::PluginError::NotFound);
    QCOMPARE(api.playMotion(gone, QStringLiteral("Idle"), 0),
             pet::PluginError::NotFound);
    QCOMPARE(api.setExpression(gone, QStringLiteral("e")),
             pet::PluginError::NotFound);
    QCOMPARE(api.triggerHitArea(gone, QStringLiteral("Head")),
             pet::PluginError::NotFound);
    QCOMPARE(api.mountVoicePack(gone, QStringLiteral("pack")),
             pet::PluginError::NotFound);
    QCOMPARE(api.unmountVoicePack(gone), pet::PluginError::NotFound);

    // Degraded wiring: null InstanceManager → NotFound everywhere (never
    // a crash).
    core::TuningApiImpl degraded(nullptr, nullptr);
    QCOMPARE(degraded.setOpacity(gone, 0.5), pet::PluginError::NotFound);
    QCOMPARE(degraded.unmountVoicePack(gone), pet::PluginError::NotFound);
}

// ── 4. pending-delete → Busy ────────────────────────────────────────────────

void TuningApiImplTest::testPendingDeleteIsBusy()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    WsServer server;
    PendingRequests pending;
    InstanceManager mgr(base.path(), server, pending,
                        [&base](const PanelConfig& cfg) {
                            PanelStateManager(base.path()).save(cfg);
                        });
    core::TuningApiImpl api(&mgr, nullptr);

    const QString uuid = mgr.createInstance(QStringLiteral("Doomed"));
    QVERIFY(!uuid.isEmpty());
    InstanceSession* session = mgr.instanceAt(0);
    QVERIFY(session != nullptr);
    // The exact mark InstanceManager::deleteInstance's two-phase path sets
    // before initiating the async stop (public seam; no process needed).
    session->setDeletePending();

    QCOMPARE(api.setOpacity(uuid, 0.5), pet::PluginError::Busy);
    QCOMPARE(api.setVolume(uuid, 0.5), pet::PluginError::Busy);
    QCOMPARE(api.setMuted(uuid, true), pet::PluginError::Busy);
    QCOMPARE(api.setFps(uuid, 30), pet::PluginError::Busy);
    QCOMPARE(api.playMotion(uuid, QStringLiteral("Idle"), 0),
             pet::PluginError::Busy);
    QCOMPARE(api.setExpression(uuid, QStringLiteral("e")),
             pet::PluginError::Busy);
    QCOMPARE(api.triggerHitArea(uuid, QStringLiteral("Head")),
             pet::PluginError::Busy);
    QCOMPARE(api.mountVoicePack(uuid, QStringLiteral("pack")),
             pet::PluginError::Busy);
    QCOMPARE(api.unmountVoicePack(uuid), pet::PluginError::Busy);
}

// ── 5. mount packId resolution ──────────────────────────────────────────────

void TuningApiImplTest::testMountResolvesPackIdByDirectoryName()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    // Fixture tree: <base>/renderer/Resources/VoicePacks/{good_pack,
    // corrupt_pack}/meta.mko; the user-packs source stays an empty temp
    // dir (the built-in source is enough).
    const QString rendererRoot = QDir(base.path()).filePath(QStringLiteral("renderer"));
    QVERIFY(writePack(rendererRoot, QStringLiteral("good_pack"),
                      buildMinimalBundle()));
    QVERIFY(writePack(rendererRoot, QStringLiteral("corrupt_pack"),
                      QByteArray("this is not a protobuf bundle")));

    WsServer server;
    PendingRequests pending;
    InstanceManager mgr(base.path(), server, pending,
                        [&base](const PanelConfig& cfg) {
                            PanelStateManager(base.path()).save(cfg);
                        });
    core::TuningApiImpl api(&mgr, nullptr);
    api.setPackScanDirs(rendererRoot,
                        QDir(base.path()).filePath(QStringLiteral("userpacks")));

    const QString uuid = mgr.createInstance(QStringLiteral("Mount"));
    QVERIFY(!uuid.isEmpty());
    InstanceSession* session = mgr.instanceAt(0);
    QVERIFY(session != nullptr);
    QVERIFY(session->mountedVoicePack().isEmpty());

    // Unknown packId → NotFound.
    QCOMPARE(api.mountVoicePack(uuid, QStringLiteral("not_a_pack")),
             pet::PluginError::NotFound);
    QVERIFY(session->mountedVoicePack().isEmpty());

    // A PATH is never interpreted: an absolute directory that exists on
    // disk but is not a DISCOVERED pack directory name → NotFound.
    const QString absPath = QDir(rendererRoot).absoluteFilePath(
        QStringLiteral("Resources/VoicePacks/good_pack"));
    QCOMPARE(api.mountVoicePack(uuid, absPath), pet::PluginError::NotFound);
    QVERIFY(session->mountedVoicePack().isEmpty());

    // Empty packId → InvalidArgument.
    QCOMPARE(api.mountVoicePack(uuid, QString()),
             pet::PluginError::InvalidArgument);

    // Discovered dir with garbage meta.mko, nothing mounted yet → the
    // session refuses (engine stays disabled) → Generic.
    QCOMPARE(api.mountVoicePack(uuid, QStringLiteral("corrupt_pack")),
             pet::PluginError::Generic);
    QVERIFY(session->mountedVoicePack().isEmpty());

    // Legit DIRECTORY NAME → Ok; the session holds the resolved ABSOLUTE
    // dir and persists it.
    QSignalSpy mountedSpy(session, &InstanceSession::mountedVoicePackChanged);
    QCOMPARE(api.mountVoicePack(uuid, QStringLiteral("good_pack")),
             pet::PluginError::Ok);
    QCOMPARE(session->mountedVoicePack(), absPath);
    QCOMPARE(mountedSpy.count(), 1); // the v1.3 NOTIFY seam
    const auto loaded = InstanceConfigManager(base.path()).load(uuid);
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->voicePack, absPath);

    // Corrupt pack with a pack ALREADY mounted: the session leaves the
    // current mount untouched and reports the engine still enabled —
    // accepted (Ok), state unchanged (existing session semantics).
    QCOMPARE(api.mountVoicePack(uuid, QStringLiteral("corrupt_pack")),
             pet::PluginError::Ok);
    QCOMPARE(session->mountedVoicePack(), absPath);
    QCOMPARE(mountedSpy.count(), 1);

    // Unmount → Ok, persisted empty, NOTIFY fired again.
    QCOMPARE(api.unmountVoicePack(uuid), pet::PluginError::Ok);
    QVERIFY(session->mountedVoicePack().isEmpty());
    QCOMPARE(mountedSpy.count(), 2);
    const auto after = InstanceConfigManager(base.path()).load(uuid);
    QVERIFY(after.has_value());
    QVERIFY(after->voicePack.isEmpty());

    // Unmount on an already-unmounted session is idempotent (Ok, no
    // second NOTIFY — the session's early-return guard).
    QCOMPARE(api.unmountVoicePack(uuid), pet::PluginError::Ok);
    QCOMPARE(mountedSpy.count(), 2);
}

QTEST_MAIN(TuningApiImplTest)
#include "TuningApiImplTest.moc"
