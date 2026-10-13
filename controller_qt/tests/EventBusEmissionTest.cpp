// EventBusEmissionTest (S8a/S8b, v1.5; extended v1.7/S9) — the
// emission-point wiring + the "flagship" end-to-end paths.
//
// Slots:
//   - hit emission: triggerHitArea drives the REAL handleHitEvent → a
//     REAL bus (EventBus + EventApiImpl) delivers pet.instance.hit with
//     the EventCatalog payload + the host header fields
//     (uuid/publisherId/cancellable); the S8b decision adapters on the
//     same bus still produce the default play_motion (behavioral
//     equivalence with the pre-S8b direct chain).
//   - pack mount/unmount double emission: mountVoicePack/unmountVoicePack
//     publish pet.pack.mounted/unmounted at the mountedVoicePackChanged
//     NOTIFY point (same `changed` gate); the NOTIFY itself still fires.
//   - lifecycle emissions (POSIX fake-sleeper renderer): ready (verbatim
//     payload), model_loaded (host-supplemented motion_groups),
//     drag_end (post-commit), model_load_failed, audio_finished
//     (S8b publish-only override, verbatim payload).
//   - idle_tick emission + unchanged original behavior: the scheduler
//     tick publishes pet.instance.idle_tick AND still sends the
//     priority=1 play_motion (Scheduler semantics untouched).
//   - flagship first half: a fake plugin subscribes to
//     pet.instance.hit through queryApi("pet.events") on a REAL
//     PluginHost; a REAL session's triggerHitArea publishes through the
//     shared bus; the plugin's listener receives it.
//   - S8b decision-chain slots:
//       hit via bus → MBE      (mounted pack owns the area → play_motion_ext)
//       MBE ⟂ IH mutual exclusion (cancelled传播 — no default play_motion)
//       flagship SECOND half e2e (plugin cancels head + plays a custom
//         motion via ITuningApi → BOTH defaults suppressed; non-head
//         areas still run the IH default)
//       idle_tick cancellation (a cancelling subscriber → no play_motion)
//       adapter unsubscribe on session destruction (no dangling listener)
//   - v1.7 (S9) catalog-completion slots:
//       error / layout_changed / window_resized (WS mirrors, verbatim
//         payloads, malformed early-return publishes nothing)
//       dialogue (MBE sink inflow → pet.instance.dialogue, dual-channel
//         split: the sink STILL receives the text)
//       restart_scheduled / gave_up (RestartController driven through
//         its setBackoffForTest seam on a REAL session's controller)
//       settings_changed (SettingsApiImpl writes → panel-level events;
//         invalid writes emit nothing)
//       plugin_loaded / plugin_unloaded (PluginHost harness, panel-level,
//         instanceUuid == "")
//       the CATALOG COMPLETENESS GUARD: makeKnownEventTable's key set
//         must equal pet::kAllEventIds element-for-element (an
//         audio_finished-style gap fails here).
//   - v1.8 renderer-event bridge slots:
//       expression_applied (§16 receipt, verbatim {expression_id})
//       motion_progress (§17 both variants, progress omission mirrored
//         verbatim — group {group,index,progress} + path {motion_path})
//   - v1.9 boundary-event slots:
//       autoLaunch settings_changed (the HOST-DIRECT
//         PanelConfigController path emits the same channel
//         ISettingsApi writes do — success emits, same-value + persist
//         failure do not)
//       focus_changed (§18 bridge, verbatim {focused} both values)
//
// QTEST_MAIN — the fake-renderer slots launch a QProcess + pump the
// event loop (QTest::qWait). The fake renderer is the POSIX sleeper
// harness from InstanceControlApiImplTest (existence is all
// resolveRendererPath checks); those slots QSKIP on non-POSIX.
// Links pet_panel_core.

#include "api/EventIds.hpp"
#include "api/IEventApi.hpp"
#include "api/IPanelPlugin.hpp"
#include "api/ITuningApi.hpp"
#include "core/EventBus.hpp"
#include "core/EventCatalog.hpp"
#include "core/InstanceConfig.hpp"
#include "core/InstanceManager.hpp"
#include "core/InstanceSession.hpp"
#include "core/PanelConfig.hpp"
#include "core/PanelConfigController.hpp"
#include "core/PanelStateManager.hpp"
#include "core/PluginContextImpl.hpp"
#include "core/PluginHost.hpp"
#include "core/PluginRegistry.hpp"
#include "core/RestartController.hpp"
#include "core/RuleEngineService.hpp"
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
#include <QSet>
#include <QSignalSpy>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

namespace {

// ── Recording publisher (fake pet::IEventApi) ───────────────────────────────
// Captures every publish; subscribe/unsubscribe are unused by the
// emission sites (host emissions are publish-only). NOTE (S8b): this fake
// does NOT dispatch — slots that need the decision chain to RUN mount a
// real core::EventBus + EventApiImpl and capture via RecordingListener
// below instead.
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

// ── Recording / cancelling listeners over a REAL bus (S8b) ──────────────────
// RecordingListener: Highest tier, never cancels — captures the event
// shape at dispatch time. CancellingListener: claims every event it sees.
class RecordingListener : public pet::IEventListener
{
public:
    QList<pet::Event> events;
    void onEvent(pet::Event& event) override { events.append(event); }

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

class CancellingListener : public pet::IEventListener
{
public:
    int calls = 0;
    void onEvent(pet::Event& event) override
    {
        ++calls;
        event.cancelled = true;
    }
};

// Count occurrences of one action string in a commandSent QSignalSpy.
int countAction(const QSignalSpy& spy, const QString& action)
{
    int n = 0;
    for (int i = 0; i < spy.count(); ++i)
        if (spy.at(i).at(0).toString() == action)
            ++n;
    return n;
}

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
// so mountVoicePack's parseMetaMko accepts it. `groupCode` is the group
// key the mounted behavior engine matches hit areas against
// (hasGroupForArea is a case-insensitive areaId == groupCode equality —
// S8b tests use the plain area name, e.g. "head"). `doc` (v1.7/S9
// dialogue slot) optionally populates the action's dialogue-text field
// (bundles.proto Action.doc, wire field 6).
bool writeMetaMkoFixture(const QString& packDir,
                         const QString& groupCode =
                             QStringLiteral("tap_head"),
                         const QString& doc = QString())
{
    if (!QDir().mkpath(packDir))
        return false;
    QByteArray meta;
    meta += encodeStringField(1, QStringLiteral("Test Voice Pack"));
    meta += encodeStringField(2, QStringLiteral("pack_v1"));
    QByteArray group;
    group += encodeStringField(1, groupCode);
    group += encodeVarintField(2, 5);
    group += encodeStringField(3, QStringLiteral("Tap Head"));
    QByteArray action;
    action += encodeVarintField(1, 42);
    action += encodeStringField(2, groupCode);
    action += encodeStringField(3,
                                QStringLiteral("motions/tap01.motion3.json"));
    // v1.7 (S9) dialogue slot: Action.doc (wire field 6) is the dialogue
    // text the MBE surfaces; empty default keeps the fixture
    // byte-identical for the pre-existing slots.
    if (!doc.isEmpty())
        action += encodeStringField(6, doc);
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

// ── Forge plugin (S8b flagship SECOND half — cancel + custom motion) ────────
// Subscribes pet.instance.hit at NORMAL (ahead of the Lowest-tier default
// adapters). For area=="head" it CLAIMS the hit (event.cancelled = true)
// and plays a custom motion through the shared ITuningApi — the exact
// Forge gameplay the cancellable-hit channel exists for. Non-head areas
// pass through untouched (defaults run).
struct ForgePlugin;
ForgePlugin* g_forgePlugin = nullptr;

struct ForgeListener : public pet::IEventListener
{
    pet::ITuningApi* tuning = nullptr; // injected by the plugin after queryApi
    int hits = 0;
    int cancels = 0;
    pet::PluginError lastPlay = pet::PluginError::Generic;

    void onEvent(pet::Event& event) override
    {
        ++hits;
        const QString area =
            event.payload.value(QStringLiteral("area_id")).toString();
        if (area != QStringLiteral("head"))
            return;
        event.cancelled = true;
        ++cancels;
        lastPlay = tuning->playMotion(event.instanceUuid,
                                      QStringLiteral("ForgeWave"), 0);
    }
};

struct ForgePlugin final : public pet::IPanelPlugin
{
    ForgeListener listener;
    pet::IEventApi* eventsApi = nullptr;

    pet::PluginError initialize(pet::IPluginContext& context) override
    {
        // The events family for the subscription + the tuning family for
        // the custom motion (capability-gated: manifest grants
        // "instance_tuning").
        listener.tuning = dynamic_cast<pet::ITuningApi*>(
            context.queryApi(pet::kTuningApiId, 1));
        auto* api = dynamic_cast<pet::IEventApi*>(
            context.queryApi(pet::kEventsApiId, 1));
        if (api == nullptr || listener.tuning == nullptr)
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

pet::IPanelPlugin* spawnForgePlugin()
{
    auto* p = new ForgePlugin;
    g_forgePlugin = p;
    return p;
}

QString forgeManifest()
{
    // "instance_tuning" grants ITuningApi (ITuningApi.hpp's gate).
    return QStringLiteral(
        "{\"id\":\"org.test.forge\",\"version\":\"1.0\","
        "\"api_version\":\"1.5\",\"order\":100,"
        "\"capabilities\":[\"instance_tuning\"]}");
}

} // namespace

class EventBusEmissionTest : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        g_flagshipPlugin = nullptr;
        g_forgePlugin = nullptr;
    }

    void testHitEmission();
    void testPackMountEmission();
    void testLifecycleEmissions();
    void testIdleTickEmission();
    void testFlagshipPluginReceivesHostHit();

    // ── S8b: hit/idle decision chain on the bus + cancellation ───────────
    void testHitRoutedToMountedBehaviorEngine();
    void testMbeMutualExclusionWithIh();
    void testFlagshipPluginCancelsHitDefaults();
    void testIdleTickCancelledSkipsPlayMotion();
    void testHitAdaptersUnsubscribedOnDestruction();
    void testAudioFinishedEmission();

    // ── v1.7 (S9): catalog completion — 9 new channels + the guard ──────
    void testErrorLayoutWindowEmissions();
    void testDialogueEmission();
    void testRestartScheduledAndGaveUpEmissions();
    void testSettingsChangedEmission();
    void testPluginLifecycleEmissions();
    void testExpressionAppliedEmission();
    void testMotionProgressEmission();
    // ── v1.9: boundary events (autoLaunch / focus bridge) ───────────────
    void testAutoLaunchSettingsChangedEmission();
    void testFocusChangedEmission();
    void testKnownEventTableCoversCatalog();

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

// ── 1. hit emission over a REAL bus (no process needed — triggerHitArea
//    calls the real handleHitEvent directly) ─────────────────────────────────
void EventBusEmissionTest::testHitEmission()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    WsServer server;
    PendingRequests pending;

    // S8b: the decision chain runs ON the bus now — a real EventBus +
    // shared EventApiImpl (the PanelApplication wiring shape), with a
    // Highest-tier recorder capturing the event shape at dispatch time.
    core::EventBus bus;
    core::EventApiImpl eventApi(&bus);
    RecordingListener recorder;
    QCOMPARE(eventApi.subscribe(pet::kEventInstanceHit.toString(),
                                &recorder, pet::EventPriority::Highest),
             pet::PluginError::Ok);

    InstanceSession* session =
        makeSession(base, server, pending, &eventApi);

    // Behavioral equivalence with the pre-S8b direct chain: the IH
    // adapter (Lowest, subscribed in setEventPublisher) resolves the
    // default "head" → TapHead mapping and the play_motion command still
    // fires — exactly once, exactly as before.
    QSignalSpy commandSpy(session, &InstanceSession::commandSent);
    session->triggerHitArea(QStringLiteral("head"));

    QCOMPARE(commandSpy.count(), 1);
    QCOMPARE(commandSpy.at(0).at(0).toString(), QStringLiteral("play_motion"));

    QCOMPARE(recorder.countOf(pet::kEventInstanceHit.toString()), 1);
    const pet::Event* evt = recorder.first(pet::kEventInstanceHit.toString());
    QVERIFY(evt != nullptr);
    QCOMPARE(evt->instanceUuid, session->uuid());
    QCOMPARE(evt->publisherId, QString(pet::kEventPublisherHost));
    QVERIFY(evt->cancellable); // hit is THE cancellable channel
    QVERIFY(evt->timestampMs > 0); // real bus stamps at publish
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

    // S8b: the session's two hit adapters are ALSO subscribed on this bus
    // (bound to the session's lifetime, publisherId "" host channel) —
    // delete the session FIRST so the zero-leftovers assertion below
    // exercises exactly what it names (the plugin's own unsubscribe at
    // shutdownAll, plus the adapter unsubscribe at session destruction).
    delete session;
    QCOMPARE(bus.subscriptionCount(), 1); // only the plugin listener remains

    // Unload path: shutdownAll sweeps a plugin that unsubscribes
    // properly (zero leftovers — the sweep WARN stays silent).
    host.shutdownAll();
    QCOMPARE(bus.subscriptionCount(), 0);
}

// ── 6. S8b: hit via the bus routes to the MountedBehaviorEngine ────────────
// A pack owning the hit area ("head") makes the MBE adapter (Lowest,
// subscribed first) build + send the play_motion_ext; a second hit on an
// area the pack does NOT own ("body") falls through to the IH adapter
// (default TapBody play_motion) — the no-match fallback half of the
// pre-S8b if/else semantics.
void EventBusEmissionTest::testHitRoutedToMountedBehaviorEngine()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    WsServer server;
    PendingRequests pending;
    core::EventBus bus;
    core::EventApiImpl eventApi(&bus);

    InstanceSession* session =
        makeSession(base, server, pending, &eventApi);

    const QString packRoot =
        QDir(base.path()).absoluteFilePath(QStringLiteral("packs"));
    const QString packDir =
        QDir(packRoot).absoluteFilePath(QStringLiteral("pack_head"));
    QVERIFY(writeMetaMkoFixture(packDir, QStringLiteral("head")));
    QVERIFY(session->mountVoicePack(packDir));

    QSignalSpy commandSpy(session, &InstanceSession::commandSent);

    // Pack owns "head" → MBE adapter produces the pack behavior command.
    session->triggerHitArea(QStringLiteral("head"));
    QCOMPARE(commandSpy.count(), 1);
    QCOMPARE(commandSpy.at(0).at(0).toString(),
             QStringLiteral("play_motion_ext"));

    // Pack does NOT own "body" → MBE adapter leaves the event uncancelled
    // → IH adapter's default mapping fires ("body" → TapBody).
    session->triggerHitArea(QStringLiteral("body"));
    QCOMPARE(commandSpy.count(), 2);
    QCOMPARE(commandSpy.at(1).at(0).toString(), QStringLiteral("play_motion"));

    delete session;
}

// ── 7. S8b: MBE ⟂ IH mutual exclusion (cancelled propagation) ───────────────
// When the MBE adapter produces a behavior it sets event.cancelled — the
// later same-Lowest-tier IH adapter must then stay SILENT (pre-S8b
// semantics: "MBE handles → IH does not run"). Exactly one command for
// the owned area, and it is play_motion_ext — never a default
// play_motion.
void EventBusEmissionTest::testMbeMutualExclusionWithIh()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    WsServer server;
    PendingRequests pending;
    core::EventBus bus;
    core::EventApiImpl eventApi(&bus);

    InstanceSession* session =
        makeSession(base, server, pending, &eventApi);

    const QString packDir = QDir(base.path()).absoluteFilePath(
        QStringLiteral("packs/pack_mutex"));
    QVERIFY(writeMetaMkoFixture(packDir, QStringLiteral("head")));
    QVERIFY(session->mountVoicePack(packDir));

    QSignalSpy commandSpy(session, &InstanceSession::commandSent);
    session->triggerHitArea(QStringLiteral("head"));

    // ONE command total (the pack behavior); ZERO default play_motion.
    QCOMPARE(commandSpy.count(), 1);
    QCOMPARE(commandSpy.at(0).at(0).toString(),
             QStringLiteral("play_motion_ext"));
    QCOMPARE(countAction(commandSpy, QStringLiteral("play_motion")), 0);

    delete session;
}

// ── 8. S8b flagship SECOND half: a Normal-tier plugin claims the hit ────────
// The Forge plugin cancels area=="head" and plays a custom motion through
// the shared ITuningApi. Both Lowest-tier defaults (MBE with a head-pack
// mounted AND the IH fallback) must stay quiet; a non-head area still
// runs the default chain. Runs over the REAL PluginHost + shared bus +
// shared TuningApiImpl (the PanelApplication service-tree shape).
void EventBusEmissionTest::testFlagshipPluginCancelsHitDefaults()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    WsServer server;
    PendingRequests pending;

    core::EventBus bus;
    core::EventApiImpl eventApi(&bus);

    InstanceManager mgr(base.path(), server, pending,
                        [](const PanelConfig&) {}, nullptr);
    core::PluginRegistry registry;
    core::PluginHost host(registry);
    host.setInstanceManager(&mgr);
    host.setEventApi(&eventApi);
    core::TuningApiImpl tuningApi(&mgr, nullptr);
    host.setTuningApi(&tuningApi);
    registry.addStaticPlugin(forgeManifest(), &spawnForgePlugin);
    host.initializeAll();

    QVERIFY(g_forgePlugin != nullptr);

    // The session must live in the manager's roster (the plugin's
    // playMotion resolves the uuid through ITuningApi) and publish on the
    // shared bus — exactly the production wiring.
    const QString uuid = mgr.createInstance(QStringLiteral("ForgePet"));
    QVERIFY(!uuid.isEmpty());
    InstanceSession* session = mgr.instanceAt(0);
    QVERIFY(session != nullptr);
    session->setEventPublisher(&eventApi);

    // Arm the MBE default TOO (pack owns "head") so the test proves BOTH
    // defaults are suppressed, not just the IH fallback.
    const QString packDir = QDir(base.path()).absoluteFilePath(
        QStringLiteral("packs/pack_forge"));
    QVERIFY(writeMetaMkoFixture(packDir, QStringLiteral("head")));
    QVERIFY(session->mountVoicePack(packDir));

    QSignalSpy commandSpy(session, &InstanceSession::commandSent);

    // head: plugin (Normal) sees it FIRST → cancels + plays ForgeWave.
    // The MBE + IH adapters (Lowest) observe cancelled=true and no-op.
    session->triggerHitArea(QStringLiteral("head"));
    QCOMPARE(g_forgePlugin->listener.hits, 1);
    QCOMPARE(g_forgePlugin->listener.cancels, 1);
    QCOMPARE(g_forgePlugin->listener.lastPlay, pet::PluginError::Ok);
    QCOMPARE(commandSpy.count(), 1); // ONLY the plugin's custom motion
    QCOMPARE(commandSpy.at(0).at(0).toString(), QStringLiteral("play_motion"));
    QCOMPARE(countAction(commandSpy, QStringLiteral("play_motion_ext")), 0);

    // body: the plugin passes it through → defaults run (pack does not
    // own "body" → IH fallback plays the default TapBody motion).
    session->triggerHitArea(QStringLiteral("body"));
    QCOMPARE(g_forgePlugin->listener.hits, 2);
    QCOMPARE(g_forgePlugin->listener.cancels, 1); // only the head claim
    QCOMPARE(commandSpy.count(), 2);               // + the IH default
    QCOMPARE(countAction(commandSpy, QStringLiteral("play_motion_ext")), 0);

    // Plugin unload sweeps its subscription; the session's adapters stay
    // bound until the session dies (the manager destroys them at ~mgr —
    // the bus outlives it by declaration order).
    host.shutdownAll();
    QCOMPARE(bus.subscriptionCount(), 2); // the two session adapters
}

// ── 9. S8b: idle_tick cancellation skips the play_motion ────────────────────
// Phase 1 (control): an uncancelled tick still sends the priority=1
// play_motion. Phase 2: a Normal-tier listener cancels every idle_tick →
// ticks keep firing (idleMotionTriggered) but NO further play_motion.
void EventBusEmissionTest::testIdleTickCancelledSkipsPlayMotion()
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

    core::EventBus bus;
    core::EventApiImpl eventApi(&bus);

    InstanceSession* session =
        makeSession(base, server, pending, &eventApi, rendererDir);
    session->start();
    QVERIFY2(session->isProcessRunning(),
             "fake renderer process failed to launch");

    session->onMessage(makeEvent(
        QStringLiteral("model_loaded"),
        QJsonObject{ { QStringLiteral("model_id"),
                       QStringLiteral("Hiyori") } }));
    QVERIFY(session->modelLoaded()); // scheduler armed by model_loaded

    QSignalSpy commandSpy(session, &InstanceSession::commandSent);
    QSignalSpy idleSpy(session, &InstanceSession::idleMotionTriggered);

    // Phase 1 — control: an uncancelled tick publishes AND sends.
    QTRY_VERIFY_WITH_TIMEOUT(idleSpy.count() >= 1, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(
        countAction(commandSpy, QStringLiteral("play_motion")) >= 1, 3000);
    const int motionsAfterControl =
        countAction(commandSpy, QStringLiteral("play_motion"));

    // Phase 2 — a Normal-tier listener claims every idle_tick.
    CancellingListener canceller;
    QCOMPARE(eventApi.subscribe(pet::kEventInstanceIdleTick.toString(),
                                &canceller, pet::EventPriority::Normal),
             pet::PluginError::Ok);
    const int ticksBefore = idleSpy.count();
    QTRY_VERIFY_WITH_TIMEOUT(idleSpy.count() >= ticksBefore + 1, 3000);
    QVERIFY(canceller.calls >= 1); // the listener actually ran

    // The scheduler kept ticking, but the play_motion stream froze.
    QVERIFY(idleSpy.count() >= ticksBefore + 1);
    QCOMPARE(countAction(commandSpy, QStringLiteral("play_motion")),
             motionsAfterControl);

    eventApi.unsubscribe(pet::kEventInstanceIdleTick.toString(),
                         &canceller);
    stopSession(session);
#endif
}

// ── 10. S8b: the hit adapters unsubscribe at session destruction ───────────
// While alive, the session holds exactly 2 subscriptions (MBE + IH); after
// deletion a direct bus publish of pet.instance.hit must dispatch to a
// fresh listener WITHOUT touching the dead adapters (no dangling-pointer
// crash — the raw-pointer contract of EventBus).
void EventBusEmissionTest::testHitAdaptersUnsubscribedOnDestruction()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    WsServer server;
    PendingRequests pending;

    core::EventBus bus;
    core::EventApiImpl eventApi(&bus);

    InstanceSession* session =
        makeSession(base, server, pending, &eventApi);
    QCOMPARE(bus.subscriptionCount(), 2); // MBE + IH on pet.instance.hit

    const QString uuid = session->uuid();
    delete session; // ~InstanceSession unsubscribes both adapters
    QCOMPARE(bus.subscriptionCount(), 0);

    class ProbeListener : public pet::IEventListener
    {
    public:
        int calls = 0;
        void onEvent(pet::Event&) override { ++calls; }
    };
    ProbeListener probe;
    QCOMPARE(bus.subscribe(pet::kEventInstanceHit.toString(), &probe,
                           pet::EventPriority::Normal, QString()),
             pet::PluginError::Ok);

    pet::Event evt;
    evt.eventId = pet::kEventInstanceHit.toString();
    evt.instanceUuid = uuid; // the DEAD session's uuid — must be ignored
    evt.publisherId = QString(pet::kEventPublisherHost);
    evt.cancellable = true;
    evt.payload.insert(QStringLiteral("area_id"), QStringLiteral("head"));

    QCOMPARE(bus.publish(evt), pet::PluginError::Ok); // no dangling crash
    QCOMPARE(probe.calls, 1);
    QCOMPARE(bus.subscriptionCount(), 1); // only the probe remains
}

// ── 11. S8b: audio_finished publish-only override ───────────────────────────
// The renderer's 14th protocol event (events.md §15) reaches the session
// via onMessage → the override publishes pet.instance.audio_finished
// with the VERBATIM payload, non-cancellable, host-stamped.
void EventBusEmissionTest::testAudioFinishedEmission()
{
#ifndef Q_OS_UNIX
    QSKIP("fake-renderer sleeper harness is POSIX-only");
#else
    QTemporaryDir base;
    QVERIFY(base.isValid());
    QString rendererDir;
    QVERIFY2(makeFakeRendererDir(base.path(), &rendererDir),
             "failed to build the fake renderer dir");

    WsServer server;
    QVERIFY2(server.listen(0), "WsServer failed to listen on port 0");
    PendingRequests pending;

    core::EventBus bus;
    core::EventApiImpl eventApi(&bus);
    RecordingListener recorder;
    QCOMPARE(eventApi.subscribe(pet::kEventInstanceAudioFinished.toString(),
                                &recorder, pet::EventPriority::Highest),
             pet::PluginError::Ok);

    InstanceSession* session =
        makeSession(base, server, pending, &eventApi, rendererDir);
    session->start();
    QVERIFY2(session->isProcessRunning(),
             "fake renderer process failed to launch");

    const QJsonObject wirePayload{
        { QStringLiteral("audio_path"),
          QStringLiteral("Resources/VoicePacks/pack-a/sound_01.ogg") }
    };
    session->onMessage(
        makeEvent(QStringLiteral("audio_finished"), wirePayload));

    QCOMPARE(recorder.countOf(pet::kEventInstanceAudioFinished.toString()), 1);
    const pet::Event* evt =
        recorder.first(pet::kEventInstanceAudioFinished.toString());
    QVERIFY(evt != nullptr);
    QCOMPARE(evt->payload, wirePayload); // VERBATIM audio_path echo
    QCOMPARE(evt->instanceUuid, session->uuid());
    QCOMPARE(evt->publisherId, QString(pet::kEventPublisherHost));
    QVERIFY(!evt->cancellable); // pure telemetry — never cancellable

    // Pure telemetry: no command side effects.
    QSignalSpy commandSpy(session, &InstanceSession::commandSent);
    session->onMessage(
        makeEvent(QStringLiteral("audio_finished"), wirePayload));
    QCOMPARE(commandSpy.count(), 0);

    stopSession(session);
#endif
}

// ── 12. v1.7 (S9): error / layout_changed / window_resized WS mirrors ───────
// All three arrive via onMessage on a started fake-renderer session and
// fan onto the bus with VERBATIM payloads; malformed payloads take the
// persist early-return and publish NOTHING (post-commit discipline).
void EventBusEmissionTest::testErrorLayoutWindowEmissions()
{
#ifndef Q_OS_UNIX
    QSKIP("fake-renderer sleeper harness is POSIX-only");
#else
    QTemporaryDir base;
    QVERIFY(base.isValid());
    QString rendererDir;
    QVERIFY2(makeFakeRendererDir(base.path(), &rendererDir),
             "failed to build the fake renderer dir");

    WsServer server;
    QVERIFY2(server.listen(0), "WsServer failed to listen on port 0");
    PendingRequests pending;

    core::EventBus bus;
    core::EventApiImpl eventApi(&bus);
    RecordingListener recorder;
    QCOMPARE(eventApi.subscribe(pet::kEventInstanceError.toString(),
                                &recorder, pet::EventPriority::Highest),
             pet::PluginError::Ok);
    QCOMPARE(eventApi.subscribe(pet::kEventInstanceLayoutChanged.toString(),
                                &recorder, pet::EventPriority::Highest),
             pet::PluginError::Ok);
    QCOMPARE(eventApi.subscribe(pet::kEventInstanceWindowResized.toString(),
                                &recorder, pet::EventPriority::Highest),
             pet::PluginError::Ok);

    InstanceSession* session =
        makeSession(base, server, pending, &eventApi, rendererDir);
    session->start();
    QVERIFY2(session->isProcessRunning(),
             "fake renderer process failed to launch");

    // error (events.md §9): verbatim {error_code, error_message}.
    const QJsonObject errorPayload{
        { QStringLiteral("error_code"), 6001 },
        { QStringLiteral("error_message"),
          QStringLiteral("shader compile failed") }
    };
    session->onMessage(makeEvent(QStringLiteral("error"), errorPayload));
    QCOMPARE(recorder.countOf(pet::kEventInstanceError.toString()), 1);
    const pet::Event* errEvt =
        recorder.first(pet::kEventInstanceError.toString());
    QVERIFY(errEvt != nullptr);
    QCOMPARE(errEvt->payload, errorPayload); // VERBATIM
    QCOMPARE(errEvt->instanceUuid, session->uuid());
    QCOMPARE(errEvt->publisherId, QString(pet::kEventPublisherHost));
    QVERIFY(!errEvt->cancellable);

    // layout_changed (events.md §12): verbatim {offset_x, offset_y, scale},
    // AFTER the persist. A malformed follow-up publishes nothing.
    const QJsonObject layoutPayload{
        { QStringLiteral("offset_x"), 0.1 },
        { QStringLiteral("offset_y"), -0.05 },
        { QStringLiteral("scale"), 1.2 }
    };
    session->onMessage(makeEvent(QStringLiteral("layout_changed"),
                                 layoutPayload));
    QCOMPARE(
        recorder.countOf(pet::kEventInstanceLayoutChanged.toString()), 1);
    const pet::Event* layoutEvt =
        recorder.first(pet::kEventInstanceLayoutChanged.toString());
    QVERIFY(layoutEvt != nullptr);
    QCOMPARE(layoutEvt->payload, layoutPayload); // VERBATIM
    QCOMPARE(layoutEvt->instanceUuid, session->uuid());
    QVERIFY(!layoutEvt->cancellable);
    session->onMessage(makeEvent(
        QStringLiteral("layout_changed"),
        QJsonObject{ { QStringLiteral("offset_x"), 0.2 } })); // missing halves
    QCOMPARE(
        recorder.countOf(pet::kEventInstanceLayoutChanged.toString()), 1);

    // window_resized (events.md §13): verbatim {window_width,
    // window_height, window_x, window_y}, AFTER the persist.
    const QJsonObject resizePayload{
        { QStringLiteral("window_width"), 400 },
        { QStringLiteral("window_height"), 600 },
        { QStringLiteral("window_x"), 1200 },
        { QStringLiteral("window_y"), 300 }
    };
    session->onMessage(
        makeEvent(QStringLiteral("window_resized"), resizePayload));
    QCOMPARE(
        recorder.countOf(pet::kEventInstanceWindowResized.toString()), 1);
    const pet::Event* resizeEvt =
        recorder.first(pet::kEventInstanceWindowResized.toString());
    QVERIFY(resizeEvt != nullptr);
    QCOMPARE(resizeEvt->payload, resizePayload); // VERBATIM
    QCOMPARE(resizeEvt->instanceUuid, session->uuid());
    QVERIFY(!resizeEvt->cancellable);
    session->onMessage(makeEvent(
        QStringLiteral("window_resized"),
        QJsonObject{ { QStringLiteral("window_width"), 500 } })); // missing
    QCOMPARE(
        recorder.countOf(pet::kEventInstanceWindowResized.toString()), 1);

    stopSession(session);
#endif
}

// ── 13. v1.7 (S9): dialogue — the MBE sink-inflow dual-channel split ────────
// A mounted pack whose action carries dialogue text (Action.doc): the
// hit → MBE adapter calls the session's dialogue sink (DISPLAY channel
// intact) AND publishes pet.instance.dialogue {text, duration_ms}
// (OBSERVABILITY channel) with the session's uuid.
void EventBusEmissionTest::testDialogueEmission()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    WsServer server;
    PendingRequests pending;

    core::EventBus bus;
    core::EventApiImpl eventApi(&bus);
    RecordingListener recorder;
    QCOMPARE(eventApi.subscribe(pet::kEventInstanceDialogue.toString(),
                                &recorder, pet::EventPriority::Highest),
             pet::PluginError::Ok);

    InstanceSession* session =
        makeSession(base, server, pending, &eventApi);

    const QString packDir = QDir(base.path()).absoluteFilePath(
        QStringLiteral("packs/pack_talk"));
    QVERIFY(writeMetaMkoFixture(packDir, QStringLiteral("head"),
                                QStringLiteral("摸摸头～")));
    QVERIFY(session->mountVoicePack(packDir));

    // The DISPLAY channel: the sink still receives the line.
    QStringList sinkTexts;
    QStringList sinkUuids;
    session->setDialogueSink(
        [&](const QString& uuid, const QString&, const QString&,
            const QString& text, int) {
            sinkUuids.append(uuid);
            sinkTexts.append(text);
        });

    QSignalSpy commandSpy(session, &InstanceSession::commandSent);
    session->triggerHitArea(QStringLiteral("head"));
    QCOMPARE(commandSpy.count(), 1); // the pack behavior still fires
    QCOMPARE(sinkTexts, QStringList{ QStringLiteral("摸摸头～") });
    QCOMPARE(sinkUuids, QStringList{ session->uuid() });

    // The OBSERVABILITY channel: same text on the bus.
    QCOMPARE(recorder.countOf(pet::kEventInstanceDialogue.toString()), 1);
    const pet::Event* evt =
        recorder.first(pet::kEventInstanceDialogue.toString());
    QVERIFY(evt != nullptr);
    QCOMPARE(evt->payload.value(QStringLiteral("text")).toString(),
             QStringLiteral("摸摸头～"));
    // No audio track in the fixture → duration degrades to the 0
    // sentinel (the stream applies its default).
    QCOMPARE(evt->payload.value(QStringLiteral("duration_ms")).toInt(), 0);
    QCOMPARE(evt->instanceUuid, session->uuid());
    QCOMPARE(evt->publisherId, QString(pet::kEventPublisherHost));
    QVERIFY(!evt->cancellable);

    delete session;
}

// ── 14. v1.7 (S9): restart_scheduled / gave_up at the session wiring ───────
// Drives the REAL session-owned RestartController through its
// setBackoffForTest seam (the RestartControllerTest precedent) — the
// retry/gaveUp signals fire the very ctor wiring that publishes the
// events. Empty rendererPath keeps each retry's start() a no-process
// failure; the publishes happen before/around it regardless.
void EventBusEmissionTest::testRestartScheduledAndGaveUpEmissions()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    WsServer server;
    PendingRequests pending;

    core::EventBus bus;
    core::EventApiImpl eventApi(&bus);
    RecordingListener recorder;
    QCOMPARE(eventApi.subscribe(
                 pet::kEventInstanceRestartScheduled.toString(), &recorder,
                 pet::EventPriority::Highest),
             pet::PluginError::Ok);
    QCOMPARE(eventApi.subscribe(pet::kEventInstanceGaveUp.toString(),
                                &recorder, pet::EventPriority::Highest),
             pet::PluginError::Ok);

    InstanceSession* session =
        makeSession(base, server, pending, &eventApi);

    // The session's RestartController is a QObject child — reach it via
    // findChild (no new test seam needed on InstanceSession).
    auto* rc = session->findChild<RestartController*>();
    QVERIFY(rc != nullptr);
    rc->setBackoffForTest({ 10, 20, 30, 40, 50 });

    // 5 scheduleRestart calls → 5 retries → 5 restart_scheduled events
    // with the 1-based attempt number + the armed test-ladder delay.
    for (int i = 0; i < RestartController::kMaxAttempts; ++i) {
        rc->scheduleRestart();
        QTRY_COMPARE_WITH_TIMEOUT(
            recorder.countOf(pet::kEventInstanceRestartScheduled.toString()),
            i + 1, 2000);
    }
    // Verify attempt/delay pairs by scanning the captured events in order.
    QList<QPair<int, int>> seen;
    for (const pet::Event& e : recorder.events) {
        if (e.eventId == pet::kEventInstanceRestartScheduled.toString()) {
            seen.append({ e.payload.value(QStringLiteral("attempt")).toInt(),
                          e.payload.value(QStringLiteral("delay_ms")).toInt() });
            QCOMPARE(e.instanceUuid, session->uuid());
            QCOMPARE(e.publisherId, QString(pet::kEventPublisherHost));
            QVERIFY(!e.cancellable);
        }
    }
    QCOMPARE(seen.size(), 5);
    for (int i = 0; i < 5; ++i) {
        QCOMPARE(seen.at(i).first, i + 1);           // attempt 1..5
        QCOMPARE(seen.at(i).second, (i + 1) * 10);   // delay 10..50
    }

    // The 6th schedule (attempts == kMaxAttempts) fires gaveUp
    // synchronously → pet.instance.gave_up {attempts: 5} post-commit.
    rc->scheduleRestart();
    QCOMPARE(recorder.countOf(pet::kEventInstanceGaveUp.toString()), 1);
    const pet::Event* gaveUp =
        recorder.first(pet::kEventInstanceGaveUp.toString());
    QVERIFY(gaveUp != nullptr);
    QCOMPARE(gaveUp->payload.value(QStringLiteral("attempts")).toInt(),
             RestartController::kMaxAttempts);
    QCOMPARE(gaveUp->instanceUuid, session->uuid());
    QVERIFY(!gaveUp->cancellable);

    delete session;
}

// ── 15. v1.7 (S9): pet.panel.settings_changed (panel-level) ─────────────────
// Every SUCCESSFUL SettingsApiImpl write publishes {field, value} with
// instanceUuid == ""; rejected writes (validation) publish nothing.
// autoLaunchSystem has no ISettingsApi surface at all (host-direct
// path), so it structurally cannot emit — asserted by the total count.
void EventBusEmissionTest::testSettingsChangedEmission()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());

    RecordingPublisher publisher;
    core::SettingsApiImpl api(base.path(), nullptr);
    api.setEventPublisher(&publisher);

    QCOMPARE(api.setCloseAction(QStringLiteral("minimize")),
             pet::PluginError::Ok);
    QCOMPARE(api.setConfirmOnExit(true), pet::PluginError::Ok);
    QCOMPARE(api.setStartMinimized(false), pet::PluginError::Ok);
    QCOMPARE(api.setDefaultModelName(QStringLiteral("Hiyori")),
             pet::PluginError::Ok);
    QCOMPARE(api.setDefaultGraphicsBackend(QStringLiteral("vulkan")),
             pet::PluginError::Ok);

    // Rejected writes emit nothing.
    QCOMPARE(api.setCloseAction(QStringLiteral("bogus")),
             pet::PluginError::InvalidArgument);
    QCOMPARE(api.setDefaultModelName(QString()),
             pet::PluginError::InvalidArgument);
    QCOMPARE(api.setDefaultGraphicsBackend(QStringLiteral("dx12")),
             pet::PluginError::InvalidArgument);

    const QString changed = pet::kEventPanelSettingsChanged.toString();
    QCOMPARE(publisher.countOf(changed), 5);
    QCOMPARE(publisher.events.size(), 5); // no other channel fired
    for (const pet::Event& e : publisher.events) {
        QCOMPARE(e.eventId, changed);
        QCOMPARE(e.instanceUuid, QString()); // PANEL-LEVEL
        QCOMPARE(e.publisherId, QString(pet::kEventPublisherHost));
        QVERIFY(!e.cancellable);
    }
    QCOMPARE(publisher.events.at(0).payload.value(QStringLiteral("field"))
                 .toString(),
             QStringLiteral("close_action"));
    QCOMPARE(publisher.events.at(0).payload.value(QStringLiteral("value"))
                 .toString(),
             QStringLiteral("minimize"));
    QCOMPARE(publisher.events.at(1).payload.value(QStringLiteral("field"))
                 .toString(),
             QStringLiteral("confirm_on_exit"));
    QVERIFY(publisher.events.at(1).payload.value(QStringLiteral("value"))
                .isBool());
    QCOMPARE(publisher.events.at(1).payload.value(QStringLiteral("value"))
                 .toBool(),
             true);
    QCOMPARE(publisher.events.at(2).payload.value(QStringLiteral("field"))
                 .toString(),
             QStringLiteral("start_minimized"));
    QCOMPARE(publisher.events.at(2).payload.value(QStringLiteral("value"))
                 .toBool(),
             false);
    QCOMPARE(publisher.events.at(3).payload.value(QStringLiteral("field"))
                 .toString(),
             QStringLiteral("default_model_name"));
    QCOMPARE(publisher.events.at(3).payload.value(QStringLiteral("value"))
                 .toString(),
             QStringLiteral("Hiyori"));
    QCOMPARE(publisher.events.at(4).payload.value(QStringLiteral("field"))
                 .toString(),
             QStringLiteral("default_graphics_backend"));
    QCOMPARE(publisher.events.at(4).payload.value(QStringLiteral("value"))
                 .toString(),
             QStringLiteral("vulkan"));
}

// ── 16. v1.7 (S9): pet.panel.plugin_loaded / plugin_unloaded ────────────────
// A REAL PluginHost over the shared bus: initializeAll publishes
// plugin_loaded per Started plugin, shutdownAll publishes plugin_unloaded
// per swept plugin — both PANEL-LEVEL (instanceUuid == "").
void EventBusEmissionTest::testPluginLifecycleEmissions()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    WsServer server;
    PendingRequests pending;

    core::EventBus bus;
    core::EventApiImpl eventApi(&bus);
    RecordingListener recorder;
    QCOMPARE(eventApi.subscribe(pet::kEventPanelPluginLoaded.toString(),
                                &recorder, pet::EventPriority::Highest),
             pet::PluginError::Ok);
    QCOMPARE(eventApi.subscribe(pet::kEventPanelPluginUnloaded.toString(),
                                &recorder, pet::EventPriority::Highest),
             pet::PluginError::Ok);

    InstanceManager mgr(base.path(), server, pending,
                        [](const PanelConfig&) {}, nullptr);
    core::PluginRegistry registry;
    core::PluginHost host(registry);
    host.setInstanceManager(&mgr);
    host.setEventApi(&eventApi);
    registry.addStaticPlugin(flagshipManifest(), &spawnFlagshipPlugin);
    host.initializeAll();

    QVERIFY(g_flagshipPlugin != nullptr); // the plugin actually started
    QCOMPARE(recorder.countOf(pet::kEventPanelPluginLoaded.toString()), 1);
    QCOMPARE(
        recorder.countOf(pet::kEventPanelPluginUnloaded.toString()), 0);
    const pet::Event* loaded =
        recorder.first(pet::kEventPanelPluginLoaded.toString());
    QVERIFY(loaded != nullptr);
    QCOMPARE(loaded->payload.value(QStringLiteral("plugin_id")).toString(),
             QStringLiteral("org.test.flagship"));
    QCOMPARE(loaded->instanceUuid, QString()); // PANEL-LEVEL
    QCOMPARE(loaded->publisherId, QString(pet::kEventPublisherHost));
    QVERIFY(!loaded->cancellable);

    host.shutdownAll();
    QCOMPARE(recorder.countOf(pet::kEventPanelPluginUnloaded.toString()), 1);
    const pet::Event* unloaded =
        recorder.first(pet::kEventPanelPluginUnloaded.toString());
    QVERIFY(unloaded != nullptr);
    QCOMPARE(unloaded->payload.value(QStringLiteral("plugin_id")).toString(),
             QStringLiteral("org.test.flagship"));
    QCOMPARE(unloaded->instanceUuid, QString()); // PANEL-LEVEL
    QVERIFY(!unloaded->cancellable);
}

// ── 16b. v1.8: expression_applied publish-only override ─────────────────────
// events.md §16 — the set_expression SUCCESS receipt (unknown ids /
// unloaded models never emit it). The override publishes
// pet.instance.expression_applied with the VERBATIM {expression_id}
// payload, non-cancellable, host-stamped; zero command side effects.
void EventBusEmissionTest::testExpressionAppliedEmission()
{
#ifndef Q_OS_UNIX
    QSKIP("fake-renderer sleeper harness is POSIX-only");
#else
    QTemporaryDir base;
    QVERIFY(base.isValid());
    QString rendererDir;
    QVERIFY2(makeFakeRendererDir(base.path(), &rendererDir),
             "failed to build the fake renderer dir");

    WsServer server;
    QVERIFY2(server.listen(0), "WsServer failed to listen on port 0");
    PendingRequests pending;

    core::EventBus bus;
    core::EventApiImpl eventApi(&bus);
    RecordingListener recorder;
    QCOMPARE(eventApi.subscribe(pet::kEventInstanceExpressionApplied.toString(),
                                &recorder, pet::EventPriority::Highest),
             pet::PluginError::Ok);

    InstanceSession* session =
        makeSession(base, server, pending, &eventApi, rendererDir);
    session->start();
    QVERIFY2(session->isProcessRunning(),
             "fake renderer process failed to launch");

    const QJsonObject wirePayload{
        { QStringLiteral("expression_id"), QStringLiteral("f01") }
    };
    session->onMessage(
        makeEvent(QStringLiteral("expression_applied"), wirePayload));

    QCOMPARE(recorder.countOf(pet::kEventInstanceExpressionApplied.toString()),
             1);
    const pet::Event* evt =
        recorder.first(pet::kEventInstanceExpressionApplied.toString());
    QVERIFY(evt != nullptr);
    QCOMPARE(evt->payload, wirePayload); // VERBATIM expression_id echo
    QCOMPARE(evt->payload.size(), 1);    // single key — nothing added
    QCOMPARE(evt->instanceUuid, session->uuid());
    QCOMPARE(evt->publisherId, QString(pet::kEventPublisherHost));
    QVERIFY(!evt->cancellable); // pure telemetry — never cancellable

    // Pure telemetry: no command side effects.
    QSignalSpy commandSpy(session, &InstanceSession::commandSent);
    session->onMessage(
        makeEvent(QStringLiteral("expression_applied"), wirePayload));
    QCOMPARE(commandSpy.count(), 0);

    stopSession(session);
#endif
}

// ── 16c. v1.8: motion_progress publish-only override (both variants) ────────
// events.md §17 — the 500ms-throttled progress snapshot. Group variant
// {group,index,progress} and path variant {motion_path} (progress
// OMITTED — unknown-duration motions; the omission mirrors verbatim)
// both arrive on the bus with their exact wire shape, non-cancellable;
// zero command side effects (the high-frequency handler does nothing
// but pass through).
void EventBusEmissionTest::testMotionProgressEmission()
{
#ifndef Q_OS_UNIX
    QSKIP("fake-renderer sleeper harness is POSIX-only");
#else
    QTemporaryDir base;
    QVERIFY(base.isValid());
    QString rendererDir;
    QVERIFY2(makeFakeRendererDir(base.path(), &rendererDir),
             "failed to build the fake renderer dir");

    WsServer server;
    QVERIFY2(server.listen(0), "WsServer failed to listen on port 0");
    PendingRequests pending;

    core::EventBus bus;
    core::EventApiImpl eventApi(&bus);
    RecordingListener recorder;
    QCOMPARE(eventApi.subscribe(pet::kEventInstanceMotionProgress.toString(),
                                &recorder, pet::EventPriority::Highest),
             pet::PluginError::Ok);

    InstanceSession* session =
        makeSession(base, server, pending, &eventApi, rendererDir);
    session->start();
    QVERIFY2(session->isProcessRunning(),
             "fake renderer process failed to launch");

    QSignalSpy commandSpy(session, &InstanceSession::commandSent);

    // Group variant (play_motion): {group, index, progress}.
    const QJsonObject groupPayload{
        { QStringLiteral("group"), QStringLiteral("TapHead") },
        { QStringLiteral("index"), 0 },
        { QStringLiteral("progress"), 0.42 }
    };
    session->onMessage(
        makeEvent(QStringLiteral("motion_progress"), groupPayload));

    // Path variant (play_motion_ext): {motion_path} only — the OPTIONAL
    // progress key omitted (looping / unresolved duration); the bus
    // payload mirrors the omission verbatim, never fabricates a value.
    const QJsonObject pathPayload{
        { QStringLiteral("motion_path"),
          QStringLiteral("Resources/VoicePacks/pack-a/motions/tap01.motion3.json") }
    };
    session->onMessage(
        makeEvent(QStringLiteral("motion_progress"), pathPayload));

    QCOMPARE(recorder.countOf(pet::kEventInstanceMotionProgress.toString()), 2);
    const pet::Event* groupEvt =
        recorder.first(pet::kEventInstanceMotionProgress.toString());
    QVERIFY(groupEvt != nullptr);
    QCOMPARE(groupEvt->payload, groupPayload); // VERBATIM {group,index,progress}
    QCOMPARE(groupEvt->instanceUuid, session->uuid());
    QCOMPARE(groupEvt->publisherId, QString(pet::kEventPublisherHost));
    QVERIFY(!groupEvt->cancellable);

    // The path-variant sample: the recorder keeps arrival order.
    const pet::Event* pathEvt = nullptr;
    for (const pet::Event& e : recorder.events) {
        if (e.eventId == pet::kEventInstanceMotionProgress.toString()
            && e.payload.contains(QStringLiteral("motion_path"))) {
            pathEvt = &e;
            break;
        }
    }
    QVERIFY(pathEvt != nullptr);
    QCOMPARE(pathEvt->payload, pathPayload); // VERBATIM {motion_path}, no progress
    QVERIFY(!pathEvt->payload.contains(QStringLiteral("progress")));
    QCOMPARE(pathEvt->instanceUuid, session->uuid());
    QVERIFY(!pathEvt->cancellable);

    // Pure pass-through telemetry: no commands sent.
    QCOMPARE(commandSpy.count(), 0);

    stopSession(session);
#endif
}

// ── 16d. v1.9: autoLaunch settings_changed (the host-direct path) ────────────
// PanelConfigController::setAutoLaunchSystem — the ONE settings field
// that deliberately walks the direct host path outside ISettingsApi —
// now emits the SAME pet.panel.settings_changed channel after a
// successful persist (post-commit): {field: "auto_launch_system",
// value: <bool>}, panel-level, non-cancellable. Same-value no-ops and
// persist failures publish NOTHING.
void EventBusEmissionTest::testAutoLaunchSettingsChangedEmission()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());

    RecordingPublisher publisher;
    PanelConfigController ctrl(base.path());
    ctrl.setEventPublisher(&publisher);

    const QString changed = pet::kEventPanelSettingsChanged.toString();

    // Success path: flip off → on → off; each flip persists + publishes.
    QSignalSpy notifySpy(&ctrl, &PanelConfigController::autoLaunchSystemChanged);
    ctrl.setAutoLaunchSystem(true);
    QCOMPARE(notifySpy.count(), 1); // NOTIFY intact (QML binding unaffected)
    QCOMPARE(publisher.countOf(changed), 1);
    QCOMPARE(ctrl.autoLaunchSystem(), true);
    const pet::Event* evt = publisher.first(changed);
    QVERIFY(evt != nullptr);
    QCOMPARE(evt->payload.value(QStringLiteral("field")).toString(),
             QStringLiteral("auto_launch_system"));
    QVERIFY(evt->payload.value(QStringLiteral("value")).isBool());
    QCOMPARE(evt->payload.value(QStringLiteral("value")).toBool(), true);
    QCOMPARE(evt->instanceUuid, QString()); // PANEL-LEVEL
    QCOMPARE(evt->publisherId, QString(pet::kEventPublisherHost));
    QVERIFY(!evt->cancellable);

    ctrl.setAutoLaunchSystem(false);
    QCOMPARE(publisher.countOf(changed), 2);
    const pet::Event* falseEvt = nullptr;
    for (const pet::Event& e : publisher.events)
        if (e.eventId == changed
            && e.payload.value(QStringLiteral("value")).isBool()
            && !e.payload.value(QStringLiteral("value")).toBool())
            falseEvt = &e;
    QVERIFY(falseEvt != nullptr);
    QCOMPARE(falseEvt->payload.value(QStringLiteral("field")).toString(),
             QStringLiteral("auto_launch_system"));

    // Same-value no-op: no write, no NOTIFY, no publish.
    ctrl.setAutoLaunchSystem(false);
    QCOMPARE(notifySpy.count(), 2);
    QCOMPARE(publisher.countOf(changed), 2);

    // Persist failure publishes nothing: a configDir whose parent is a
    // regular FILE cannot be created → PanelStateManager save fails →
    // the setter rolls back + returns before the publish.
    QFile blocker(base.path() + QStringLiteral("/blocker"));
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.close();
    PanelConfigController broken(base.path() + QStringLiteral("/blocker/x"));
    broken.setEventPublisher(&publisher);
    broken.setAutoLaunchSystem(true);
    QCOMPARE(broken.autoLaunchSystem(), false); // rolled back
    QCOMPARE(publisher.countOf(changed), 2);    // nothing new emitted
}

// ── 16e. v1.9: focus_changed publish-only override ──────────────────────────
// events.md §18 — the pet window gained/lost OS-level keyboard focus.
// The override publishes pet.instance.focus_changed with the VERBATIM
// {focused} payload (both values), non-cancellable, host-stamped; zero
// command side effects.
void EventBusEmissionTest::testFocusChangedEmission()
{
#ifndef Q_OS_UNIX
    QSKIP("fake-renderer sleeper harness is POSIX-only");
#else
    QTemporaryDir base;
    QVERIFY(base.isValid());
    QString rendererDir;
    QVERIFY2(makeFakeRendererDir(base.path(), &rendererDir),
             "failed to build the fake renderer dir");

    WsServer server;
    QVERIFY2(server.listen(0), "WsServer failed to listen on port 0");
    PendingRequests pending;

    core::EventBus bus;
    core::EventApiImpl eventApi(&bus);
    RecordingListener recorder;
    QCOMPARE(eventApi.subscribe(pet::kEventInstanceFocusChanged.toString(),
                                &recorder, pet::EventPriority::Highest),
             pet::PluginError::Ok);

    InstanceSession* session =
        makeSession(base, server, pending, &eventApi, rendererDir);
    session->start();
    QVERIFY2(session->isProcessRunning(),
             "fake renderer process failed to launch");

    QSignalSpy commandSpy(session, &InstanceSession::commandSent);

    const QJsonObject gainedPayload{
        { QStringLiteral("focused"), true }
    };
    session->onMessage(
        makeEvent(QStringLiteral("window_focus"), gainedPayload));
    const QJsonObject lostPayload{
        { QStringLiteral("focused"), false }
    };
    session->onMessage(
        makeEvent(QStringLiteral("window_focus"), lostPayload));

    QCOMPARE(recorder.countOf(pet::kEventInstanceFocusChanged.toString()), 2);
    const pet::Event* gained =
        recorder.first(pet::kEventInstanceFocusChanged.toString());
    QVERIFY(gained != nullptr);
    QCOMPARE(gained->payload, gainedPayload); // VERBATIM focused echo
    QCOMPARE(gained->payload.size(), 1);      // single key — nothing added
    QCOMPARE(gained->instanceUuid, session->uuid());
    QCOMPARE(gained->publisherId, QString(pet::kEventPublisherHost));
    QVERIFY(!gained->cancellable); // pure telemetry — never cancellable

    const pet::Event* lost = nullptr;
    for (const pet::Event& e : recorder.events) {
        if (e.eventId == pet::kEventInstanceFocusChanged.toString()
            && e.payload.value(QStringLiteral("focused")).isBool()
            && !e.payload.value(QStringLiteral("focused")).toBool()) {
            lost = &e;
            break;
        }
    }
    QVERIFY(lost != nullptr);
    QCOMPARE(lost->payload, lostPayload); // VERBATIM, both halves pass through
    QCOMPARE(lost->instanceUuid, session->uuid());

    // Pure telemetry: no commands sent.
    QCOMPARE(commandSpy.count(), 0);

    stopSession(session);
#endif
}

// ── 17. v1.7 (S9): the CATALOG COMPLETENESS GUARD ───────────────────────────
// makeKnownEventTable's key set must equal pet::kAllEventIds
// element-for-element. The audio_finished-style gap (constant added to
// EventIds.hpp, table entry forgotten — which once made cancel_default
// WARN on a legal event) fails HERE, at the source: every direction is
// checked (missing table entry, ghost table entry, duplicate array id).
void EventBusEmissionTest::testKnownEventTableCoversCatalog()
{
    const core::RuleEngine::KnownEventTable table =
        core::RuleEngineService::makeKnownEventTable();

    // The catalog side: every id in kAllEventIds, with no duplicates
    // (a duplicated array entry would silently shrink the guard).
    QStringList ids;
    for (const QLatin1StringView& id : pet::kAllEventIds)
        ids.append(QString(id));
    const QSet<QString> catalog(ids.cbegin(), ids.cend());
    QCOMPARE(ids.size(), int(pet::kAllEventIds.size()));
    QCOMPARE(catalog.size(), ids.size());

    // The table side.
    QSet<QString> known;
    for (auto it = table.cbegin(); it != table.cend(); ++it)
        known.insert(it.key());

    // Both directions: no missing entry (the audio_finished failure
    // mode), no ghost entry (a table key with no catalog constant).
    QCOMPARE(known.size(), catalog.size());
    QCOMPARE(known, catalog);

    // The two designated cancellable channels stay cancellable; the
    // v1.7 additions are all non-cancellable telemetry.
    QVERIFY(table.value(QString(pet::kEventInstanceHit), false));
    QVERIFY(table.value(QString(pet::kEventInstanceIdleTick), false));
    QVERIFY(!table.value(QString(pet::kEventInstanceError), true));
    QVERIFY(!table.value(QString(pet::kEventPanelSettingsChanged), true));
}

QTEST_MAIN(EventBusEmissionTest)

#include "EventBusEmissionTest.moc"
