// EventBusEmissionTest (S8a, v1.5) — the emission-point wiring + the
// "flagship first half" end-to-end path.
//
// Slots:
//   - hit emission: triggerHitArea drives the REAL handleHitEvent → the
//     recording publisher sees pet.instance.hit with the EventCatalog
//     payload + the host header fields (uuid/publisherId/cancellable);
//     the pre-existing play_motion command stream still fires (zero
//     behavior change).
//   - pack mount/unmount double emission: mountVoicePack/unmountVoicePack
//     publish pet.pack.mounted/unmounted at the mountedVoicePackChanged
//     NOTIFY point (same `changed` gate); the NOTIFY itself still fires.
//   - lifecycle emissions (POSIX fake-sleeper renderer): ready (verbatim
//     payload), model_loaded (host-supplemented motion_groups),
//     drag_end (post-commit), model_load_failed.
//   - idle_tick emission + unchanged original behavior: the scheduler
//     tick publishes pet.instance.idle_tick AND still sends the
//     priority=1 play_motion (Scheduler semantics untouched).
//   - flagship first half: a fake plugin subscribes to
//     pet.instance.hit through queryApi("pet.events") on a REAL
//     PluginHost; a REAL session's triggerHitArea publishes through the
//     shared bus; the plugin's listener receives it.
//
// QTEST_MAIN — the fake-renderer slots launch a QProcess + pump the
// event loop (QTest::qWait). The fake renderer is the POSIX sleeper
// harness from InstanceControlApiImplTest (existence is all
// resolveRendererPath checks); those slots QSKIP on non-POSIX.
// Links pet_panel_core.

#include "api/EventIds.hpp"
#include "api/IEventApi.hpp"
#include "api/IPanelPlugin.hpp"
#include "core/EventBus.hpp"
#include "core/EventCatalog.hpp"
#include "core/InstanceConfig.hpp"
#include "core/InstanceManager.hpp"
#include "core/InstanceSession.hpp"
#include "core/PanelConfig.hpp"
#include "core/PanelStateManager.hpp"
#include "core/PluginContextImpl.hpp"
#include "core/PluginHost.hpp"
#include "core/PluginRegistry.hpp"
#include "network/Envelope.hpp"
#include "network/PendingRequests.hpp"
#include "network/WsServer.hpp"

#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

namespace {

// ── Recording publisher (fake pet::IEventApi) ───────────────────────────────
// Captures every publish; subscribe/unsubscribe are unused by the
// emission sites (host emissions are publish-only).
class RecordingPublisher : public pet::IEventApi
{
public:
    QList<pet::Event> events;

    pet::PluginError subscribe(const QString&, pet::IEventListener*,
                               pet::EventPriority) override
    { return pet::PluginError::Ok; }
    void unsubscribe(const QString&, pet::IEventListener*) override {}
    pet::PluginError publish(pet::Event& event) override
    {
        events.append(event);
        return pet::PluginError::Ok;
    }

    int countOf(const QString& eventId) const
    {
        int n = 0;
        for (const pet::Event& e : events)
            if (e.eventId == eventId)
                ++n;
        return n;
    }
    const pet::Event* first(const QString& eventId) const
    {
        for (const pet::Event& e : events)
            if (e.eventId == eventId)
                return &e;
        return nullptr;
    }
};

// ── Fake-renderer harness (InstanceControlApiImplTest pattern) ─────────────
// A directory whose "desktop-pet-renderer" is a 30s sleeper script —
// existence is all resolveRendererPath checks. POSIX-only.
bool makeFakeRendererDir(const QString& base, QString* outDir)
{
    const QString rendererDir =
        QDir(base).absoluteFilePath(QStringLiteral("renderer"));
    if (!QDir().mkpath(rendererDir))
        return false;
    const QString exe =
        QDir(rendererDir).absoluteFilePath(QStringLiteral("desktop-pet-renderer"));
    QFile f(exe);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(QStringLiteral("#!/bin/sh\nexec sleep 30\n").toUtf8());
    f.close();
    if (!QFile::setPermissions(
            exe, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                     | QFileDevice::ExeOwner | QFileDevice::ReadGroup
                     | QFileDevice::ExeGroup | QFileDevice::ReadOther
                     | QFileDevice::ExeOther)) {
        return false;
    }
    *outDir = rendererDir;
    return true;
}

// A .model3.json fixture with Idle=2 + TapBody=1 + one expression, at
// the exact path resolveModel3JsonPath() computes:
// <rendererDir>/Resources/Models/<Name>/<Name>.model3.json
void writeModel3Json(const QString& rendererDir, const QString& name)
{
    const QString dir = QDir(rendererDir).absoluteFilePath(
        QStringLiteral("Resources/Models/") + name);
    QDir().mkpath(dir);
    const QString path =
        QDir(dir).absoluteFilePath(name + QStringLiteral(".model3.json"));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(QStringLiteral(
        "{\"FileReferences\":{\"Motions\":{"
        "\"Idle\":[{\"File\":\"m1.json\"},{\"File\":\"m2.json\"}],"
        "\"TapBody\":[{\"File\":\"t1.json\"}]},"
        "\"Expressions\":[{\"Name\":\"f01\",\"File\":\"f01.exp3.json\"}]},"
        "\"HitAreas\":[{\"Name\":\"Body\"}]}").toUtf8());
    f.close();
}

// A synthetic event Envelope (the WsServer→InstanceManager route shape).
Envelope makeEvent(const QString& action, const QJsonObject& payload)
{
    Envelope env;
    env.type = QStringLiteral("event");
    env.action = action;
    env.payload = payload;
    return env;
}

// ── Minimal protobuf wire-format serializer (MetaMkoParserTest shape) ──────
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

QByteArray encodeStringField(int fieldNumber, const QString& value)
{
    const QByteArray utf8 = value.toUtf8();
    return encodeVarint((static_cast<quint64>(fieldNumber) << 3) | 2)
        + encodeVarint(utf8.size()) + utf8;
}

QByteArray encodeVarintField(int fieldNumber, quint64 value)
{
    return encodeVarint((static_cast<quint64>(fieldNumber) << 3) | 0)
        + encodeVarint(value);
}

QByteArray encodeMessageField(int fieldNumber, const QByteArray& message)
{
    return encodeVarint((static_cast<quint64>(fieldNumber) << 3) | 2)
        + encodeVarint(message.size()) + message;
}

// Write a minimal VALID meta.mko (meta + one group + one motion action)
// so mountVoicePack's parseMetaMko accepts it.
bool writeMetaMkoFixture(const QString& packDir)
{
    if (!QDir().mkpath(packDir))
        return false;
    QByteArray meta;
    meta += encodeStringField(1, QStringLiteral("Test Voice Pack"));
    meta += encodeStringField(2, QStringLiteral("pack_v1"));
    QByteArray group;
    group += encodeStringField(1, QStringLiteral("tap_head"));
    group += encodeVarintField(2, 5);
    group += encodeStringField(3, QStringLiteral("Tap Head"));
    QByteArray action;
    action += encodeVarintField(1, 42);
    action += encodeStringField(2, QStringLiteral("tap_head"));
    action += encodeStringField(3,
                                QStringLiteral("motions/tap01.motion3.json"));
    QByteArray bundle;
    bundle += encodeMessageField(1, meta);
    bundle += encodeMessageField(4, group);
    bundle += encodeMessageField(5, action);
    QFile mko(QDir(packDir).absoluteFilePath(QStringLiteral("meta.mko")));
    if (!mko.open(QIODevice::WriteOnly))
        return false;
    mko.write(bundle);
    mko.close();
    return true;
}

// ── Flagship fake plugin (PluginQueryApiTest harness shape) ────────────────
struct FlagshipPlugin;
FlagshipPlugin* g_flagshipPlugin = nullptr;

struct FlagshipListener : public pet::IEventListener
{
    int hits = 0;
    QString lastAreaId;
    void onEvent(pet::Event& event) override
    {
        ++hits;
        lastAreaId =
            event.payload.value(QStringLiteral("area_id")).toString();
    }
};

struct FlagshipPlugin final : public pet::IPanelPlugin
{
    pet::IPluginContext* ctx = nullptr;
    FlagshipListener listener;
    pet::IEventApi* eventsApi = nullptr;

    pet::PluginError initialize(pet::IPluginContext& context) override
    {
        ctx = &context;
        // Flagship first half: query the events family and subscribe to
        // the HOST hit channel through the per-plugin proxy.
        auto* api = dynamic_cast<pet::IEventApi*>(
            context.queryApi(pet::kEventsApiId, 1));
        if (api == nullptr)
            return pet::PluginError::Generic;
        eventsApi = api;
        return api->subscribe(pet::kEventInstanceHit.toString(), &listener,
                              pet::EventPriority::Normal);
    }

    void shutdown() override
    {
        if (eventsApi != nullptr)
            eventsApi->unsubscribe(pet::kEventInstanceHit.toString(),
                                   &listener);
    }
};

pet::IPanelPlugin* spawnFlagshipPlugin()
{
    auto* p = new FlagshipPlugin;
    g_flagshipPlugin = p;
    return p;
}

QString flagshipManifest()
{
    // Plain escaped-quote literal, NOT a multi-line raw string (moc's
    // preprocessor mis-lexes those — see PluginQueryApiTest's note).
    // The id must be reverse-DOMAIN (contain '.') — registry rule.
    return QStringLiteral(
        "{\"id\":\"org.test.flagship\",\"version\":\"1.0\","
        "\"api_version\":\"1.5\",\"order\":100,\"capabilities\":[]}");
}

} // namespace

class EventBusEmissionTest : public QObject
{
    Q_OBJECT

private slots:
    void init() { g_flagshipPlugin = nullptr; }

    void testHitEmission();
    void testPackMountEmission();
    void testLifecycleEmissions();
    void testIdleTickEmission();
    void testFlagshipPluginReceivesHostHit();

private:
    InstanceSession* makeSession(const QTemporaryDir& base, WsServer& server,
                                 PendingRequests& pending,
                                 pet::IEventApi* publisher,
                                 const QString& rendererDir = QString());
    void stopSession(InstanceSession* session);
};

InstanceSession* EventBusEmissionTest::makeSession(
    const QTemporaryDir& base, WsServer& server, PendingRequests& pending,
    pet::IEventApi* publisher, const QString& rendererDir)
{
    InstanceConfig cfg = defaultInstanceConfig();
    cfg.label = QStringLiteral("BusPet");
    cfg.rendererPath = rendererDir;
    cfg.idleInterval = 1; // 1s idle cadence for the idle_tick slot
    auto* session = new InstanceSession(cfg, server, pending, base.path(),
                                        nullptr);
    session->setStopTimeoutMs(300); // fast force-kill of the fake renderer
    if (publisher != nullptr)
        session->setEventPublisher(publisher);
    return session;
}

void EventBusEmissionTest::stopSession(InstanceSession* session)
{
    QSignalSpy finishedSpy(session, &InstanceSession::stopFinished);
    session->stop();
    QVERIFY(finishedSpy.wait(5000) || !session->isProcessRunning());
    delete session;
}

// ── 1. hit emission (no process needed — triggerHitArea calls the real
//    handleHitEvent directly) ────────────────────────────────────────────────
void EventBusEmissionTest::testHitEmission()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    WsServer server;
    PendingRequests pending;
    RecordingPublisher publisher;

    InstanceSession* session =
        makeSession(base, server, pending, &publisher);

    // Zero-behavior evidence: the existing play_motion command stream
    // still fires (the InteractionHandler path), THEN the bus event.
    QSignalSpy commandSpy(session, &InstanceSession::commandSent);
    session->triggerHitArea(QStringLiteral("head"));

    QCOMPARE(commandSpy.count(), 1);
    QCOMPARE(commandSpy.at(0).at(0).toString(), QStringLiteral("play_motion"));

    QCOMPARE(publisher.countOf(pet::kEventInstanceHit.toString()), 1);
    const pet::Event* evt = publisher.first(pet::kEventInstanceHit.toString());
    QVERIFY(evt != nullptr);
    QCOMPARE(evt->instanceUuid, session->uuid());
    QCOMPARE(evt->publisherId, QString(pet::kEventPublisherHost));
    QVERIFY(evt->cancellable); // hit is THE cancellable channel
    // (timestampMs is the real bus's stamp — covered by EventBusTest;
    // the recording fake never stamps, by design.)
    // Payload keys per events.md §hit via the EventCatalog builder.
    QCOMPARE(evt->payload.value(QStringLiteral("area_id")).toString(),
             QStringLiteral("head"));
    QVERIFY(evt->payload.contains(QStringLiteral("x")));
    QVERIFY(evt->payload.contains(QStringLiteral("y")));
    QVERIFY(evt->payload.contains(QStringLiteral("button")));

    delete session;
}

// ── 2. pack mount/unmount double emission ──────────────────────────────────
void EventBusEmissionTest::testPackMountEmission()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    WsServer server;
    PendingRequests pending;
    RecordingPublisher publisher;

    InstanceSession* session =
        makeSession(base, server, pending, &publisher);

    // Synthesize a minimal valid meta.mko (the MetaMkoParserTest encoder
    // shape): meta { name, code } + one group + one motion action.
    const QString packRoot =
        QDir(base.path()).absoluteFilePath(QStringLiteral("packs"));
    const QString packDir =
        QDir(packRoot).absoluteFilePath(QStringLiteral("pack_v1"));
    QVERIFY(writeMetaMkoFixture(packDir));

    // Existing behavior: the NOTIFY fires on mount + unmount.
    QSignalSpy notifySpy(session, &InstanceSession::mountedVoicePackChanged);
    QVERIFY(session->mountVoicePack(packDir));
    QCOMPARE(notifySpy.count(), 1);
    QCOMPARE(publisher.countOf(pet::kEventPackMounted.toString()), 1);
    const pet::Event* mounted =
        publisher.first(pet::kEventPackMounted.toString());
    QCOMPARE(mounted->payload.value(QStringLiteral("pack_id")).toString(),
             QStringLiteral("pack_v1")); // DIRECTORY name, not the path
    QCOMPARE(mounted->instanceUuid, session->uuid());
    QVERIFY(!mounted->cancellable);

    // Re-mounting the SAME pack: re-parse but not a transition → the
    // NOTIFY and the bus event both stay quiet (same gate).
    QVERIFY(session->mountVoicePack(packDir));
    QCOMPARE(notifySpy.count(), 1);
    QCOMPARE(publisher.countOf(pet::kEventPackMounted.toString()), 1);

    session->unmountVoicePack();
    QCOMPARE(notifySpy.count(), 2);
    QCOMPARE(publisher.countOf(pet::kEventPackUnmounted.toString()), 1);
    const pet::Event* unmounted =
        publisher.first(pet::kEventPackUnmounted.toString());
    QCOMPARE(unmounted->payload.value(QStringLiteral("pack_id")).toString(),
             QStringLiteral("pack_v1"));

    delete session;
}

// ── 3. lifecycle emissions over the fake-sleeper renderer ──────────────────
void EventBusEmissionTest::testLifecycleEmissions()
{
#ifndef Q_OS_UNIX
    QSKIP("fake-renderer sleeper harness is POSIX-only");
#else
    QTemporaryDir base;
    QVERIFY(base.isValid());
    QString rendererDir;
    QVERIFY2(makeFakeRendererDir(base.path(), &rendererDir),
             "failed to build the fake renderer dir");
    writeModel3Json(rendererDir, QStringLiteral("Hiyori"));

    WsServer server;
    QVERIFY2(server.listen(0), "WsServer failed to listen on port 0");
    PendingRequests pending;
    RecordingPublisher publisher;

    InstanceSession* session =
        makeSession(base, server, pending, &publisher, rendererDir);
    session->start();
    QVERIFY2(session->isProcessRunning(),
             "fake renderer process failed to launch");

    QSignalSpy salvoSpy(session, &InstanceSession::commandSent);

    // ready → running + salvo (existing behavior) + pet.instance.ready.
    QJsonObject readyPayload;
    readyPayload.insert(QStringLiteral("version"), QStringLiteral("1.0.0"));
    readyPayload.insert(
        QStringLiteral("capabilities"),
        QJsonArray{ QStringLiteral("live2d") });
    session->onMessage(makeEvent(QStringLiteral("ready"), readyPayload));
    QCOMPARE(session->status(), QStringLiteral("running")); // unchanged
    QVERIFY(salvoSpy.count() >= 7);                          // salvo intact
    const pet::Event* ready =
        publisher.first(pet::kEventInstanceReady.toString());
    QVERIFY(ready != nullptr);
    QCOMPARE(ready->payload, readyPayload); // VERBATIM WS payload
    QVERIFY(!ready->cancellable);

    // model_loaded → set_hit_areas + scheduler (existing) +
    // pet.model.loaded with the HOST-SUPPLEMENTED metadata halves.
    session->onMessage(makeEvent(
        QStringLiteral("model_loaded"),
        QJsonObject{ { QStringLiteral("model_id"),
                       QStringLiteral("Hiyori") } }));
    QVERIFY(session->modelLoaded()); // unchanged
    QCOMPARE(publisher.countOf(pet::kEventModelLoaded.toString()), 1);
    const pet::Event* loaded =
        publisher.first(pet::kEventModelLoaded.toString());
    QCOMPARE(loaded->payload.value(QStringLiteral("model_id")).toString(),
             QStringLiteral("Hiyori"));
    QCOMPARE((loaded->payload.value(QStringLiteral("motion_groups")).toArray()),
             (QJsonArray{ QStringLiteral("Idle"),
                          QStringLiteral("TapBody") })); // QMap key order
    QCOMPARE((loaded->payload.value(QStringLiteral("expressions")).toArray()),
             (QJsonArray{ QStringLiteral("f01") }));

    // drag_end → persist (existing) + post-commit pet.instance.drag_ended.
    session->onMessage(makeEvent(
        QStringLiteral("drag_end"),
        QJsonObject{ { QStringLiteral("x"), 3.5 },
                     { QStringLiteral("y"), 4.5 },
                     { QStringLiteral("window_x"), 120 },
                     { QStringLiteral("window_y"), 80 } }));
    QCOMPARE(publisher.countOf(pet::kEventDragEnded.toString()), 1);
    const pet::Event* dragEvt =
        publisher.first(pet::kEventDragEnded.toString());
    QCOMPARE(dragEvt->payload.value(QStringLiteral("window_x")).toInt(), 120);
    QCOMPARE(dragEvt->payload.value(QStringLiteral("window_y")).toInt(), 80);
    QCOMPARE(dragEvt->payload.value(QStringLiteral("x")).toDouble(), 3.5);

    // model_load_failed → revision + signal (existing) +
    // pet.model.load_failed with the verbatim wire halves.
    QSignalSpy failSpy(session, &InstanceSession::modelLoadFailed);
    session->onMessage(makeEvent(
        QStringLiteral("model_load_failed"),
        QJsonObject{ { QStringLiteral("error_code"), 5001 },
                     { QStringLiteral("error_message"),
                       QStringLiteral("missing model dir") } }));
    QCOMPARE(failSpy.count(), 1); // existing signal intact
    const pet::Event* failed =
        publisher.first(pet::kEventModelLoadFailed.toString());
    QVERIFY(failed != nullptr);
    QCOMPARE(failed->payload.value(QStringLiteral("error_code")).toInt(),
             5001);
    QCOMPARE(failed->payload.value(QStringLiteral("error_message")).toString(),
             QStringLiteral("missing model dir"));

    // motion_started / drag_start publish-only overrides: default DEBUG
    // log preserved + bus fanout (both delivered via onMessage).
    session->onMessage(makeEvent(
        QStringLiteral("motion_started"),
        QJsonObject{ { QStringLiteral("group"), QStringLiteral("Idle") },
                     { QStringLiteral("index"), 1 } }));
    const pet::Event* motionStarted =
        publisher.first(pet::kEventInstanceMotionStarted.toString());
    QVERIFY(motionStarted != nullptr);
    QCOMPARE(motionStarted->payload.value(QStringLiteral("group")).toString(),
             QStringLiteral("Idle"));
    QCOMPARE(motionStarted->payload.value(QStringLiteral("index")).toInt(), 1);

    session->onMessage(makeEvent(
        QStringLiteral("motion_finished"),
        QJsonObject{ { QStringLiteral("motion_path"),
                       QStringLiteral("motions/tap.motion3.json") } }));
    const pet::Event* motionFinished =
        publisher.first(pet::kEventInstanceMotionFinished.toString());
    QVERIFY(motionFinished != nullptr);
    QCOMPARE(motionFinished->payload.value(QStringLiteral("index")).toInt(),
             -1); // variant-B sentinel (归一)
    QCOMPARE(
        motionFinished->payload.value(QStringLiteral("motion_path")).toString(),
        QStringLiteral("motions/tap.motion3.json"));

    session->onMessage(makeEvent(
        QStringLiteral("drag_start"),
        QJsonObject{ { QStringLiteral("x"), 7.0 },
                     { QStringLiteral("y"), 8.0 } }));
    const pet::Event* dragStarted =
        publisher.first(pet::kEventDragStarted.toString());
    QVERIFY(dragStarted != nullptr);
    QCOMPARE(dragStarted->payload.value(QStringLiteral("x")).toDouble(), 7.0);

    stopSession(session);
#endif
}

// ── 4. idle_tick emission + unchanged original behavior ────────────────────
void EventBusEmissionTest::testIdleTickEmission()
{
#ifndef Q_OS_UNIX
    QSKIP("fake-renderer sleeper harness is POSIX-only");
#else
    QTemporaryDir base;
    QVERIFY(base.isValid());
    QString rendererDir;
    QVERIFY2(makeFakeRendererDir(base.path(), &rendererDir),
             "failed to build the fake renderer dir");
    writeModel3Json(rendererDir, QStringLiteral("Hiyori"));

    WsServer server;
    QVERIFY2(server.listen(0), "WsServer failed to listen on port 0");
    PendingRequests pending;
    RecordingPublisher publisher;

    InstanceSession* session =
        makeSession(base, server, pending, &publisher, rendererDir);
    session->start();
    QVERIFY2(session->isProcessRunning(),
             "fake renderer process failed to launch");

    session->onMessage(makeEvent(
        QStringLiteral("model_loaded"),
        QJsonObject{ { QStringLiteral("model_id"),
                       QStringLiteral("Hiyori") } }));
    QVERIFY(session->modelLoaded()); // scheduler armed by model_loaded

    // idleInterval = 1s → the scheduler ticks within ~1.5s. BOTH the
    // bus event AND the pre-existing priority=1 play_motion command must
    // appear (the publish is a pure append — S8a ignores cancelled).
    QSignalSpy idleSpy(session, &InstanceSession::idleMotionTriggered);
    QTRY_VERIFY_WITH_TIMEOUT(
        publisher.countOf(pet::kEventInstanceIdleTick.toString()) >= 1,
        3000);
    QTRY_COMPARE_WITH_TIMEOUT(idleSpy.count(), 1, 3000);

    const pet::Event* tick =
        publisher.first(pet::kEventInstanceIdleTick.toString());
    QVERIFY(tick != nullptr);
    QVERIFY(tick->cancellable); // idle_tick is cancellable (S8b seam)
    QCOMPARE(tick->instanceUuid, session->uuid());
    QCOMPARE(tick->payload.value(QStringLiteral("group")).toString(),
             QStringLiteral("Idle"));
    QVERIFY(tick->payload.value(QStringLiteral("index")).toInt() >= 0
            && tick->payload.value(QStringLiteral("index")).toInt() <= 1);
    QVERIFY(!tick->cancelled); // nobody兑现取消 — final value false

    stopSession(session);
#endif
}

// ── 5. flagship first half: plugin ← host hit over the real host stack ─────
void EventBusEmissionTest::testFlagshipPluginReceivesHostHit()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    WsServer server;
    PendingRequests pending;

    // The SHARED bus + API the whole flagship path runs on.
    core::EventBus bus;
    core::EventApiImpl eventApi(&bus);

    // Real PluginHost over a real InstanceManager harness; the shared
    // events API injected exactly as PanelApplication does it.
    // (PluginRegistry is an independent store — PanelApplication owns
    // one next to the host; same shape here.)
    InstanceManager mgr(base.path(), server, pending,
                        [](const PanelConfig&) {}, nullptr);
    core::PluginRegistry registry;
    core::PluginHost host(registry);
    host.setInstanceManager(&mgr);
    host.setEventApi(&eventApi);
    registry.addStaticPlugin(flagshipManifest(), &spawnFlagshipPlugin);
    host.initializeAll();

    QVERIFY(g_flagshipPlugin != nullptr);
    QVERIFY(g_flagshipPlugin->eventsApi != nullptr); // queryApi succeeded

    // A REAL session publishing through the shared API (the production
    // PanelApplication wiring shape: setEventPublisher(shared impl)).
    InstanceSession* session =
        makeSession(base, server, pending, &eventApi);
    session->triggerHitArea(QStringLiteral("body"));

    QCOMPARE(g_flagshipPlugin->listener.hits, 1);
    QCOMPARE(g_flagshipPlugin->listener.lastAreaId, QStringLiteral("body"));

    // Unload path: shutdownAll sweeps a plugin that unsubscribes
    // properly (zero leftovers — the sweep WARN stays silent).
    host.shutdownAll();
    QCOMPARE(bus.subscriptionCount(), 0);

    delete session;
}

QTEST_MAIN(EventBusEmissionTest)

#include "EventBusEmissionTest.moc"
