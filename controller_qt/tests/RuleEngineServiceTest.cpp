// RuleEngineServiceTest (S8e) — the rule-engine HOST SERVICE over the real
// core::EventBus + real RuleEngine core:
//   1. testDifferentialSubscriptions — the desired-set diff (enabled
//      triggers ∪ activity triple, zero rules → zero subscriptions),
//      asserted on the real bus's subscriptionCount().
//   2. testWhereScopeAndConnectionGates — where predicates, the
//      not-connected fail-closed gate, Instance-scope uuid ownership, and
//      the publish_event action (host identity, listener receives).
//   3. testCancelDefault — cancel_default sets the final event.cancelled
//      for a cancellable publish; an entry-cancelled event skips rules
//      (pre-claim at Highest).
//   4. testDryRun — would-fire INFO, zero action-channel calls, no
//      cancel_default side effect.
//   5. testStateTracking — hit/motion_finished advance the tracking
//      tables; idle_for / since_last_motion flip false→true on real wall
//      clocks; consecutive hits re-arm the idle window.
//   6. testLoadingPipeline — real SQLite: panel kv 1 good + 1 bad rule,
//      instance blob rule, corrupt blob → ERROR + empty; roster diff loads
//      new instance rules and sweeps removed ones.
//   7. testNorthStarRule1 — hit(area==head) + idle_for>300s → play_motion
//      flick + notify_bubble + cancel_default (fake session layer, real
//      bus + service).
//   8. testNorthStarRule2 — motion_finished(group==tap) → play_pack_audio:
//      real InstanceManager harness, real mount through the shared
//      TuningApiImpl, QTemporaryDir meta.mko fixture, audio command
//      captured on the fake renderer channel; mounted-mismatch fail-fast.
//
// Fakes: FakeInstanceApi (programmable roster + observer fanout),
// FakeTuningApi (records playMotion/setExpression), ForceConnected wrapper
// (north-star 2: a harness session has no renderer, so connected is forced
// while the runtime cache still flows from the REAL InstanceApiImpl).
//
// Links pet_panel_core (RuleEngineService + EventBus + the API impls live
// there) + spdlog (LOG_* + CapturingSink, same pattern as RuleEngineTest).
// QTEST_MAIN — DatabaseManager's QSqlDatabase + QTest::qWait need the app.

#include "core/RuleEngineService.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPair>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include <QVector>

#include <functional>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>

#include "api/EventIds.hpp"
#include "api/IEventApi.hpp"
#include "api/IInstanceApi.hpp"
#include "api/ITuningApi.hpp"
#include "core/DatabaseManager.hpp"
#include "core/EventBus.hpp"
#include "core/EventCatalog.hpp"
#include "core/InstanceManager.hpp"
#include "core/InstanceSession.hpp"
#include "core/PanelConfig.hpp"
#include "core/PanelStateManager.hpp"
#include "core/PluginContextImpl.hpp"
#include "logging/Logging.hpp"
#include "network/PendingRequests.hpp"
#include "network/WsServer.hpp"

// Capturing spdlog sink — collects every formatted record (RuleEngineTest
// pattern) so WARN/INFO/ERROR assertions work without file logging.
class CapturingSink : public spdlog::sinks::base_sink<std::mutex> {
protected:
    void sink_it_(const spdlog::details::log_msg& msg) override
    {
        spdlog::memory_buf_t formatted;
        formatter_->format(msg, formatted);
        messages.push_back(fmt::to_string(formatted));
    }
    void flush_() override {}

public:
    std::vector<std::string> messages;
};

namespace {

// ── Fakes ────────────────────────────────────────────────────────────────────

// Programmable pet::IInstanceApi: rows served by instances(), roster +
// instance observers fanned on demand (the service subscribes at
// construction).
class FakeInstanceApi : public pet::IInstanceApi {
public:
    QVector<pet::InstanceInfo> instances() override { return m_rows; }
    void subscribeRoster(pet::IRosterObserver*) override {}
    void unsubscribeRoster(pet::IRosterObserver*) override {}
    void subscribeInstances(pet::IInstanceObserver* observer) override
    {
        if (observer != nullptr && !m_observers.contains(observer))
            m_observers.append(observer);
    }
    void unsubscribeInstances(pet::IInstanceObserver* observer) override
    {
        m_observers.removeAll(observer);
    }

    void setRows(const QVector<pet::InstanceInfo>& rows)
    {
        m_rows = rows;
        for (pet::IInstanceObserver* observer : std::as_const(m_observers))
            observer->rosterChanged();
    }
    void pushRuntime(const QString& uuid, const pet::InstanceRuntime& rt)
    {
        for (pet::IInstanceObserver* observer : std::as_const(m_observers))
            observer->instanceStateChanged(uuid, rt);
    }

    QVector<pet::InstanceInfo> m_rows;
    QVector<pet::IInstanceObserver*> m_observers;
};

// Records the two tuning ops the rule engine drives; the other seven are
// inert Ok stubs.
class FakeTuningApi : public pet::ITuningApi {
public:
    struct MotionCall {
        QString uuid;
        QString group;
        int index = 0;
    };
    struct ExpressionCall {
        QString uuid;
        QString id;
    };

    QVector<MotionCall> motions;
    QVector<ExpressionCall> expressions;

    pet::PluginError playMotion(const QString& uuid, const QString& group,
                                int index) override
    {
        motions.append(MotionCall{uuid, group, index});
        return pet::PluginError::Ok;
    }
    pet::PluginError setExpression(const QString& uuid,
                                   const QString& expressionId) override
    {
        expressions.append(ExpressionCall{uuid, expressionId});
        return pet::PluginError::Ok;
    }
    pet::PluginError setOpacity(const QString&, double) override
    {
        return pet::PluginError::Ok;
    }
    pet::PluginError setVolume(const QString&, double) override
    {
        return pet::PluginError::Ok;
    }
    pet::PluginError setMuted(const QString&, bool) override
    {
        return pet::PluginError::Ok;
    }
    pet::PluginError setFps(const QString&, int) override
    {
        return pet::PluginError::Ok;
    }
    pet::PluginError triggerHitArea(const QString&, const QString&) override
    {
        return pet::PluginError::Ok;
    }
    pet::PluginError mountVoicePack(const QString&, const QString&) override
    {
        return pet::PluginError::Ok;
    }
    pet::PluginError unmountVoicePack(const QString&) override
    {
        return pet::PluginError::Ok;
    }
    pet::PluginError setGraphicsBackend(const QString&, const QString&) override
    {
        return pet::PluginError::Ok;
    }
};

// North-star-2 decorator over the REAL InstanceApiImpl: a harness session
// has no renderer (connected == false), so instances() forces the flag on
// while every observer subscription forwards to the real fanout (the
// mountedPackId runtime cache keeps flowing from the real property NOTIFYs).
class ForceConnectedInstanceApi : public pet::IInstanceApi {
public:
    explicit ForceConnectedInstanceApi(pet::IInstanceApi* inner)
        : m_inner(inner)
    {
    }
    QVector<pet::InstanceInfo> instances() override
    {
        QVector<pet::InstanceInfo> rows = m_inner->instances();
        for (pet::InstanceInfo& row : rows)
            row.connected = true;
        return rows;
    }
    void subscribeRoster(pet::IRosterObserver* observer) override
    {
        m_inner->subscribeRoster(observer);
    }
    void unsubscribeRoster(pet::IRosterObserver* observer) override
    {
        m_inner->unsubscribeRoster(observer);
    }
    void subscribeInstances(pet::IInstanceObserver* observer) override
    {
        m_inner->subscribeInstances(observer);
    }
    void unsubscribeInstances(pet::IInstanceObserver* observer) override
    {
        m_inner->unsubscribeInstances(observer);
    }

private:
    pet::IInstanceApi* m_inner; // not owned
};

// Bus listener recorders.
struct RecordingListener : public pet::IEventListener {
    void onEvent(pet::Event& event) override { events.append(event); }
    QVector<pet::Event> events;
};
struct CancellingListener : public pet::IEventListener {
    void onEvent(pet::Event& event) override { event.cancelled = true; }
};

// ── Fixtures / helpers ───────────────────────────────────────────────────────

pet::InstanceInfo makeInfo(const QString& uuid, bool connected,
                           const QString& model = QStringLiteral("Hiyori"))
{
    pet::InstanceInfo info;
    info.uuid = uuid;
    info.label = uuid;
    info.modelName = model;
    info.status = QStringLiteral("running");
    info.connected = connected;
    return info;
}

pet::Event makeEvent(const QString& eventId, const QString& uuid,
                     const QJsonObject& payload, bool cancellable = false)
{
    pet::Event event;
    event.eventId = eventId;
    event.instanceUuid = uuid;
    event.publisherId = QString(pet::kEventPublisherHost);
    event.cancellable = cancellable;
    event.payload = payload;
    return event;
}

QJsonObject actionPlayMotion(const QString& group, int index = 0)
{
    QJsonObject action;
    action.insert(QStringLiteral("kind"), QStringLiteral("play_motion"));
    action.insert(QStringLiteral("group"), group);
    if (index != 0)
        action.insert(QStringLiteral("index"), index);
    return action;
}

QJsonObject actionSetExpression(const QString& id)
{
    QJsonObject action;
    action.insert(QStringLiteral("kind"), QStringLiteral("set_expression"));
    action.insert(QStringLiteral("id"), id);
    return action;
}

QJsonObject actionNotifyBubble(const QString& text, int durationMs)
{
    QJsonObject action;
    action.insert(QStringLiteral("kind"), QStringLiteral("notify_bubble"));
    action.insert(QStringLiteral("text"), text);
    action.insert(QStringLiteral("duration_ms"), durationMs);
    return action;
}

QJsonObject actionPlayPackAudio(const QString& pack, const QString& group)
{
    QJsonObject action;
    action.insert(QStringLiteral("kind"), QStringLiteral("play_pack_audio"));
    action.insert(QStringLiteral("pack"), pack);
    action.insert(QStringLiteral("group"), group);
    return action;
}

QJsonObject actionPublishEvent(const QString& event,
                               const QJsonObject& payload = {})
{
    QJsonObject action;
    action.insert(QStringLiteral("kind"), QStringLiteral("publish_event"));
    action.insert(QStringLiteral("event"), event);
    if (!payload.isEmpty())
        action.insert(QStringLiteral("payload"), payload);
    return action;
}

QJsonObject ruleJson(const QString& id, const QString& event,
                     const QJsonArray& actions)
{
    QJsonObject trigger;
    trigger.insert(QStringLiteral("event"), event);
    QJsonObject rule;
    rule.insert(QStringLiteral("schema_version"), 1);
    rule.insert(QStringLiteral("id"), id);
    rule.insert(QStringLiteral("trigger"), trigger);
    rule.insert(QStringLiteral("actions"), actions);
    return rule;
}

// trigger.where {field: {op: "eq", value}} on an existing rule object.
QJsonObject withWhereEq(const QJsonObject& rule, const QString& field,
                        const QJsonValue& value)
{
    QJsonObject predicate;
    predicate.insert(QStringLiteral("op"), QStringLiteral("eq"));
    predicate.insert(QStringLiteral("value"), value);
    QJsonObject where;
    where.insert(field, predicate);
    QJsonObject trigger = rule.value(QStringLiteral("trigger")).toObject();
    trigger.insert(QStringLiteral("where"), where);
    QJsonObject out = rule;
    out.insert(QStringLiteral("trigger"), trigger);
    return out;
}

// condition [{kind, op, value_s}] appended to a rule object.
QJsonObject withThresholdCondition(const QJsonObject& rule,
                                   const QString& kind, const QString& op,
                                   double valueS)
{
    QJsonObject condition;
    condition.insert(QStringLiteral("kind"), kind);
    condition.insert(QStringLiteral("op"), op);
    condition.insert(QStringLiteral("value_s"), valueS);
    QJsonObject out = rule;
    out.insert(QStringLiteral("condition"), QJsonArray{condition});
    return out;
}

int countMessages(const CapturingSink& sink, const char* needle)
{
    int hits = 0;
    for (const std::string& message : sink.messages) {
        if (message.find(needle) != std::string::npos)
            ++hits;
    }
    return hits;
}

// Minimal protobuf wire-format serializer (same as TuningApiImplTest) —
// builds a meta.mko Bundle whose "tap" group holds ONE audio-only action.
namespace pack_fixture {

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

// meta(name, code) + group("tap", priority 5) + action(id 7, group "tap",
// audio "audio/tap01.ogg", NO motion — audio-only, exactly what
// buildAudioOnlyCommand picks).
QByteArray buildAudioOnlyBundle()
{
    QByteArray meta;
    meta += encodeStringField(1, QStringLiteral("NS Pack"));
    meta += encodeStringField(2, QStringLiteral("ns_pack"));
    QByteArray group;
    group += encodeStringField(1, QStringLiteral("tap"));
    group += encodeVarintField(2, 5);
    group += encodeStringField(3, QStringLiteral("Tap"));
    QByteArray action;
    action += encodeVarintField(1, 7);
    action += encodeStringField(2, QStringLiteral("tap"));
    action += encodeStringField(4, QStringLiteral("audio/tap01.ogg"));

    QByteArray bundle;
    bundle += encodeMessageField(1, meta);
    bundle += encodeMessageField(4, group);
    bundle += encodeMessageField(5, action);
    return bundle;
}

bool writePack(const QString& rendererRoot, const QString& packName,
               const QByteArray& mkoBytes)
{
    const QString packDir = QDir(rendererRoot).absoluteFilePath(
        QStringLiteral("Resources/VoicePacks/") + packName);
    if (!QDir().mkpath(packDir))
        return false;
    QFile file(QDir(packDir).absoluteFilePath(QStringLiteral("meta.mko")));
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(mkoBytes);
    file.close();
    return true;
}

} // namespace pack_fixture

} // namespace

class RuleEngineServiceTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void testDifferentialSubscriptions();
    void testWhereScopeAndConnectionGates();
    void testCancelDefault();
    void testDryRun();
    void testStateTracking();
    void testLoadingPipeline();
    void testNorthStarRule1();
    void testNorthStarRule2();

private:
    std::shared_ptr<CapturingSink> m_sink;
};

void RuleEngineServiceTest::initTestCase()
{
    m_sink = std::make_shared<CapturingSink>();
    auto logger = std::make_shared<spdlog::logger>(
        "test_rule_engine_service", m_sink);
    logger->set_level(spdlog::level::trace);
    logger->flush_on(spdlog::level::trace);
    spdlog::set_default_logger(logger);
}

void RuleEngineServiceTest::cleanupTestCase()
{
    spdlog::shutdown();
}

// ── 1. differential subscriptions ────────────────────────────────────────────

void RuleEngineServiceTest::testDifferentialSubscriptions()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    DatabaseManager db;
    QVERIFY(db.open(base.path() + QStringLiteral("/app.db")));
    core::EventBus bus;
    core::EventApiImpl eventApi(&bus);
    FakeInstanceApi instanceApi;
    instanceApi.setRows({makeInfo(QStringLiteral("u1"), true)});
    core::RuleEngineService service(&eventApi, &instanceApi, nullptr, &db);
    QCOMPARE(bus.subscriptionCount(), 0); // zero rules → zero subscriptions

    // One enabled hit rule → its trigger + the activity triple (hit is a
    // member of BOTH sets → 3 unique ids).
    const auto r1 = service.upsertRuleJson(
        ruleJson(QStringLiteral("r1"), QStringLiteral("pet.instance.hit"),
                 {actionPlayMotion(QStringLiteral("flick"))}),
        core::RuleScope::Panel, QString());
    QVERIFY(r1.has_value());
    QCOMPARE(bus.subscriptionCount(), 3);

    // A second rule on a different event adds just that id.
    const auto r2 = service.upsertRuleJson(
        ruleJson(QStringLiteral("r2"), QStringLiteral("pet.model.loaded"),
                 {actionPlayMotion(QStringLiteral("nod"))}),
        core::RuleScope::Panel, QString());
    QVERIFY(r2.has_value());
    QCOMPARE(bus.subscriptionCount(), 4);

    // Removing the hit rule keeps the triple (r2 still enabled) — hit
    // stays subscribed for tracking.
    QVERIFY(service.removeRuleById(r1->id));
    QCOMPARE(bus.subscriptionCount(), 4);

    // Removing the last rule drops everything.
    QVERIFY(service.removeRuleById(r2->id));
    QCOMPARE(bus.subscriptionCount(), 0);

    // A DISABLED rule contributes nothing (no trigger, no triple).
    QJsonObject disabled = ruleJson(
        QStringLiteral("r3"), QStringLiteral("pet.instance.idle_tick"),
        {actionPlayMotion(QStringLiteral("tap"))});
    disabled.insert(QStringLiteral("enabled"), false);
    QVERIFY(service.upsertRuleJson(disabled, core::RuleScope::Panel, QString())
                .has_value());
    QCOMPARE(service.ruleCount(), 1);
    QCOMPARE(bus.subscriptionCount(), 0);
}

// ── 2. where / scope / connection gates + publish_event ─────────────────────

void RuleEngineServiceTest::testWhereScopeAndConnectionGates()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    DatabaseManager db;
    QVERIFY(db.open(base.path() + QStringLiteral("/app.db")));
    core::EventBus bus;
    core::EventApiImpl eventApi(&bus);
    FakeInstanceApi instanceApi;
    instanceApi.setRows({makeInfo(QStringLiteral("u1"), true),
                         makeInfo(QStringLiteral("u2"), false),
                         makeInfo(QStringLiteral("u3"), true)});
    FakeTuningApi tuning;
    core::RuleEngineService service(&eventApi, &instanceApi, &tuning, &db);

    // Panel rule: hit where area_id == head.
    QVERIFY(service
                .upsertRuleJson(
                    withWhereEq(
                        ruleJson(QStringLiteral("hit-head"),
                                 QStringLiteral("pet.instance.hit"),
                                 {actionPlayMotion(QStringLiteral("flick"))}),
                        QStringLiteral("area_id"), QStringLiteral("head")),
                    core::RuleScope::Panel, QString())
                .has_value());
    // Instance rule OWNED by u1: idle_tick → stretch.
    QVERIFY(service
                .upsertRuleJson(
                    ruleJson(QStringLiteral("u1-idle"),
                             QStringLiteral("pet.instance.idle_tick"),
                             {actionPlayMotion(QStringLiteral("stretch"))}),
                    core::RuleScope::Instance, QStringLiteral("u1"))
                .has_value());

    // Matching where → fires.
    pet::Event event = makeEvent(
        QString(pet::kEventInstanceHit), QStringLiteral("u1"),
        core::makeHitPayload(QStringLiteral("head"), 10.0, 20.0, 0));
    QCOMPARE(eventApi.publish(event), pet::PluginError::Ok);
    QCOMPARE(tuning.motions.size(), 1);
    QCOMPARE(tuning.motions.at(0).group, QStringLiteral("flick"));
    QCOMPARE(tuning.motions.at(0).uuid, QStringLiteral("u1"));

    // Non-matching where → where_predicate_failed.
    event = makeEvent(QString(pet::kEventInstanceHit), QStringLiteral("u1"),
                      core::makeHitPayload(QStringLiteral("body"), 1.0, 2.0,
                                           0));
    QCOMPARE(eventApi.publish(event), pet::PluginError::Ok);
    QCOMPARE(tuning.motions.size(), 1);

    // Disconnected instance → not_connected fail-closed.
    event = makeEvent(QString(pet::kEventInstanceHit), QStringLiteral("u2"),
                      core::makeHitPayload(QStringLiteral("head"), 1.0, 2.0,
                                           0));
    QCOMPARE(eventApi.publish(event), pet::PluginError::Ok);
    QCOMPARE(tuning.motions.size(), 1);

    // Instance-scope ownership: u1's rule does not fire for u3's event…
    event = makeEvent(QString(pet::kEventInstanceIdleTick), QStringLiteral("u3"),
                      core::makeIdleTickPayload(QStringLiteral("Idle"), 0));
    QCOMPARE(eventApi.publish(event), pet::PluginError::Ok);
    QCOMPARE(tuning.motions.size(), 1);
    // …but does for u1's.
    event.instanceUuid = QStringLiteral("u1");
    QCOMPARE(eventApi.publish(event), pet::PluginError::Ok);
    QCOMPARE(tuning.motions.size(), 2);
    QCOMPARE(tuning.motions.at(1).group, QStringLiteral("stretch"));

    // publish_event action: host identity, verbatim payload, back onto the
    // bus where another listener receives it.
    RecordingListener relay;
    QCOMPARE(eventApi.subscribe(QStringLiteral("pet.rules.ping"), &relay,
                                pet::EventPriority::Normal),
             pet::PluginError::Ok);
    QVERIFY(service
                .upsertRuleJson(
                    ruleJson(QStringLiteral("relayer"),
                             QStringLiteral("pet.model.load_failed"),
                             {actionPublishEvent(
                                 QStringLiteral("pet.rules.ping"),
                                 QJsonObject{{QStringLiteral("a"), 1}})}),
                    core::RuleScope::Panel, QString())
                .has_value());
    pet::Event failed = makeEvent(
        QString(pet::kEventModelLoadFailed), QStringLiteral("u1"),
        core::makeModelLoadFailedPayload(5003, QStringLiteral("boom")));
    QCOMPARE(eventApi.publish(failed), pet::PluginError::Ok);
    QCOMPARE(relay.events.size(), 1);
    QCOMPARE(relay.events.at(0).eventId, QStringLiteral("pet.rules.ping"));
    QCOMPARE(relay.events.at(0).publisherId,
             QString(pet::kEventPublisherHost));
    QCOMPARE(relay.events.at(0).payload.value(QStringLiteral("a")).toInt(), 1);
}

// ── 3. cancel_default ────────────────────────────────────────────────────────

void RuleEngineServiceTest::testCancelDefault()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    DatabaseManager db;
    QVERIFY(db.open(base.path() + QStringLiteral("/app.db")));
    core::EventBus bus;
    core::EventApiImpl eventApi(&bus);
    FakeInstanceApi instanceApi;
    instanceApi.setRows({makeInfo(QStringLiteral("u1"), true)});
    FakeTuningApi tuning;
    core::RuleEngineService service(&eventApi, &instanceApi, &tuning, &db);

    // cancel_default = true → the final event.cancelled is true after
    // publish returns (bus-level assertion; the S8b MBE/IH suppression is
    // the parallel task's flagship, not repeated here).
    QJsonObject cancelling = ruleJson(
        QStringLiteral("claimer"), QStringLiteral("pet.instance.hit"),
        {actionPlayMotion(QStringLiteral("flick"))});
    cancelling.insert(QStringLiteral("cancel_default"), true);
    QVERIFY(service.upsertRuleJson(cancelling, core::RuleScope::Panel,
                                   QString())
                .has_value());

    pet::Event event = makeEvent(
        QString(pet::kEventInstanceHit), QStringLiteral("u1"),
        core::makeHitPayload(QStringLiteral("head"), 0.0, 0.0, 0),
        /*cancellable=*/true);
    QCOMPARE(eventApi.publish(event), pet::PluginError::Ok);
    QCOMPARE(tuning.motions.size(), 1); // the rule ran
    QCOMPARE(event.cancelled, true);     // and claimed the default

    // Entry-cancelled: a Highest-tier listener claims first → the rule
    // engine's Low tier sees cancelled at entry and skips every rule.
    CancellingListener preClaimer;
    QCOMPARE(eventApi.subscribe(QString(pet::kEventInstanceHit), &preClaimer,
                                pet::EventPriority::Highest),
             pet::PluginError::Ok);
    const int motionsBefore = tuning.motions.size();
    pet::Event claimed = makeEvent(
        QString(pet::kEventInstanceHit), QStringLiteral("u1"),
        core::makeHitPayload(QStringLiteral("head"), 0.0, 0.0, 0),
        /*cancellable=*/true);
    QCOMPARE(eventApi.publish(claimed), pet::PluginError::Ok);
    QCOMPARE(tuning.motions.size(), motionsBefore); // rules skipped
    QCOMPARE(claimed.cancelled, true);              // the pre-claimer's flag
    QVERIFY(countMessages(*m_sink, "arrived cancelled") >= 1);
}

// ── 4. dry_run ───────────────────────────────────────────────────────────────

void RuleEngineServiceTest::testDryRun()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    DatabaseManager db;
    QVERIFY(db.open(base.path() + QStringLiteral("/app.db")));
    core::EventBus bus;
    core::EventApiImpl eventApi(&bus);
    FakeInstanceApi instanceApi;
    instanceApi.setRows({makeInfo(QStringLiteral("u1"), true)});
    FakeTuningApi tuning;
    QVector<QPair<QString, int>> bubbles;
    core::RuleEngineService service(&eventApi, &instanceApi, &tuning, &db);
    service.setBubbleSink([&bubbles](const QString& text, int durationMs) {
        bubbles.append(qMakePair(text, durationMs));
    });

    QJsonObject dry = ruleJson(
        QStringLiteral("preview"), QStringLiteral("pet.instance.hit"),
        {actionPlayMotion(QStringLiteral("flick")),
         actionNotifyBubble(QStringLiteral("preview"), 1000)});
    dry.insert(QStringLiteral("dry_run"), true);
    dry.insert(QStringLiteral("cancel_default"), true);
    QVERIFY(
        service.upsertRuleJson(dry, core::RuleScope::Panel, QString())
            .has_value());

    pet::Event event = makeEvent(
        QString(pet::kEventInstanceHit), QStringLiteral("u1"),
        core::makeHitPayload(QStringLiteral("head"), 0.0, 0.0, 0),
        /*cancellable=*/true);
    const int sinkBefore = m_sink->messages.size();
    QCOMPARE(eventApi.publish(event), pet::PluginError::Ok);

    QCOMPARE(tuning.motions.size(), 0);  // zero action-channel calls
    QCOMPARE(bubbles.size(), 0);
    QCOMPARE(event.cancelled, false);     // no cancel side effect either
    QVERIFY(countMessages(*m_sink, "would fire") >= 1);
    QVERIFY(m_sink->messages.size() > sinkBefore);
}

// ── 5. state tracking ────────────────────────────────────────────────────────

void RuleEngineServiceTest::testStateTracking()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    DatabaseManager db;
    QVERIFY(db.open(base.path() + QStringLiteral("/app.db")));
    core::EventBus bus;
    core::EventApiImpl eventApi(&bus);
    FakeInstanceApi instanceApi;
    instanceApi.setRows({makeInfo(QStringLiteral("u1"), true)});
    FakeTuningApi tuning;
    core::RuleEngineService service(&eventApi, &instanceApi, &tuning, &db);

    QVERIFY(service
                .upsertRuleJson(
                    withThresholdCondition(
                        ruleJson(QStringLiteral("idle-since"),
                                 QStringLiteral("pet.instance.idle_tick"),
                                 {actionPlayMotion(QStringLiteral("stretch"))}),
                        QStringLiteral("idle_for"), QStringLiteral("gt"),
                        0.4),
                    core::RuleScope::Panel, QString())
                .has_value());
    QVERIFY(service
                .upsertRuleJson(
                    withThresholdCondition(
                        ruleJson(QStringLiteral("motion-since"),
                                 QStringLiteral("pet.instance.idle_tick"),
                                 {actionPlayMotion(QStringLiteral("bounce"))}),
                        QStringLiteral("since_last_motion"), QStringLiteral("gt"),
                        0.4),
                    core::RuleScope::Panel, QString())
                .has_value());

    // Before any tracking data both conditions fail closed (0 = unknown).
    pet::Event tick = makeEvent(QString(pet::kEventInstanceIdleTick),
                                QStringLiteral("u1"),
                                core::makeIdleTickPayload(QStringLiteral("Idle"), 0));
    QCOMPARE(eventApi.publish(tick), pet::PluginError::Ok);
    QCOMPARE(tuning.motions.size(), 0);

    // A hit advances lastActivityMs …
    pet::Event hit = makeEvent(
        QString(pet::kEventInstanceHit), QStringLiteral("u1"),
        core::makeHitPayload(QStringLiteral("body"), 0.0, 0.0, 0));
    QCOMPARE(eventApi.publish(hit), pet::PluginError::Ok);
    QCOMPARE(tuning.motions.size(), 0); // …but idle is ~0 right after it

    // …so after a real 500ms gap the idle_for condition flips TRUE.
    QTest::qWait(500);
    QCOMPARE(eventApi.publish(tick), pet::PluginError::Ok);
    QCOMPARE(tuning.motions.size(), 1);
    QCOMPARE(tuning.motions.at(0).group, QStringLiteral("stretch"));

    // A motion_finished (non-idle group) advances lastMotionMs AND
    // lastActivityMs — so after another 500ms gap BOTH conditions hold.
    pet::Event finished = makeEvent(
        QString(pet::kEventInstanceMotionFinished), QStringLiteral("u1"),
        core::makeMotionFinishedPayload(QStringLiteral("tap"), 0));
    QCOMPARE(eventApi.publish(finished), pet::PluginError::Ok);
    QTest::qWait(500);

    QCOMPARE(eventApi.publish(tick), pet::PluginError::Ok);
    QCOMPARE(tuning.motions.size(), 3);
    QCOMPARE(tuning.motions.at(1).group, QStringLiteral("stretch"));
    QCOMPARE(tuning.motions.at(2).group, QStringLiteral("bounce"));

    // A consecutive hit + motion_finished RE-ARM both clocks: immediately
    // after them the idle/motion deltas are ~0 again.
    QCOMPARE(eventApi.publish(hit), pet::PluginError::Ok);
    QCOMPARE(eventApi.publish(finished), pet::PluginError::Ok);
    QCOMPARE(eventApi.publish(tick), pet::PluginError::Ok);
    QCOMPARE(tuning.motions.size(), 3); // nothing new fired
}

// ── 6. loading pipeline (real SQLite) ────────────────────────────────────────

void RuleEngineServiceTest::testLoadingPipeline()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    DatabaseManager db;
    QVERIFY(db.open(base.path() + QStringLiteral("/app.db")));

    // Panel kv "rules": one good rule + one bad (unknown top-level field).
    QJsonObject good =
        ruleJson(QStringLiteral("panel-good"), QStringLiteral("pet.instance.hit"),
                 {actionPlayMotion(QStringLiteral("flick"))});
    QJsonObject bad =
        ruleJson(QStringLiteral("panel-bad"), QStringLiteral("pet.instance.hit"),
                 {actionPlayMotion(QStringLiteral("flick"))});
    bad.insert(QStringLiteral("bogus_field"), 1);
    QJsonObject wrapper;
    wrapper.insert(QStringLiteral("rules"), QJsonArray{good, bad});
    QVERIFY(db.setValue(QStringLiteral("rules"),
                        QString::fromUtf8(QJsonDocument(wrapper).toJson(
                            QJsonDocument::Compact))));

    // Instance u1 blob with one rule; instance u2 with a corrupt blob.
    // Planted RAW (execRaw) because the InstanceConfig serde drops unknown
    // fields like "rules" — exactly why the service reads raw blobs.
    QJsonObject instanceRule = ruleJson(
        QStringLiteral("u1-rule"), QStringLiteral("pet.instance.idle_tick"),
        {actionPlayMotion(QStringLiteral("stretch"))});
    QJsonObject blob;
    blob.insert(QStringLiteral("id"), QStringLiteral("u1"));
    blob.insert(QStringLiteral("rules"), QJsonArray{instanceRule});
    QVERIFY(db.execRaw(
        QStringLiteral("INSERT OR REPLACE INTO instance_configs (uuid, json) "
                       "VALUES ('u1', '%1')")
            .arg(QString::fromUtf8(
                QJsonDocument(blob).toJson(QJsonDocument::Compact)))));
    QVERIFY(db.execRaw(
        QStringLiteral("INSERT OR REPLACE INTO instance_configs (uuid, json) "
                       "VALUES ('u2', 'this is not json {{{')")));

    core::EventBus bus;
    core::EventApiImpl eventApi(&bus);
    FakeInstanceApi instanceApi;
    instanceApi.setRows({makeInfo(QStringLiteral("u1"), true),
                         makeInfo(QStringLiteral("u2"), true)});
    core::RuleEngineService service(&eventApi, &instanceApi, nullptr, &db);

    // panel-good + inst.u1.u1-rule loaded; panel-bad WARN-skipped; u2's
    // corrupt blob ERRORs to an empty set without crashing.
    QCOMPARE(service.ruleCount(), 2);
    QVERIFY(countMessages(*m_sink, "panel-bad") >= 1);
    QVERIFY(countMessages(*m_sink, "rules blob unreadable") >= 1);

    // A NEW instance row (u3, blob planted before the roster change):
    // the roster diff loads its rules.
    QJsonObject u3Rule = ruleJson(
        QStringLiteral("u3-rule"), QStringLiteral("pet.model.loaded"),
        {actionSetExpression(QStringLiteral("smile"))});
    QJsonObject blob3;
    blob3.insert(QStringLiteral("id"), QStringLiteral("u3"));
    blob3.insert(QStringLiteral("rules"), QJsonArray{u3Rule});
    QVERIFY(db.execRaw(
        QStringLiteral("INSERT OR REPLACE INTO instance_configs (uuid, json) "
                       "VALUES ('u3', '%1')")
            .arg(QString::fromUtf8(
                QJsonDocument(blob3).toJson(QJsonDocument::Compact)))));
    instanceApi.setRows({makeInfo(QStringLiteral("u1"), true),
                         makeInfo(QStringLiteral("u2"), true),
                         makeInfo(QStringLiteral("u3"), true)});
    QCOMPARE(service.ruleCount(), 3);

    // DELETING u1 sweeps its rules (owner-prefix removal).
    instanceApi.setRows({makeInfo(QStringLiteral("u2"), true),
                         makeInfo(QStringLiteral("u3"), true)});
    QCOMPARE(service.ruleCount(), 2);
}

// ── 7. north-star rule 1 ─────────────────────────────────────────────────────

void RuleEngineServiceTest::testNorthStarRule1()
{
    // hit(area_id==head) + idle_for>300s → play_motion(flick) +
    // notify_bubble + cancel_default. Real EventBus + real
    // RuleEngineService; fake session layer (roster + tuning + bubble).
    QTemporaryDir base;
    QVERIFY(base.isValid());
    DatabaseManager db;
    QVERIFY(db.open(base.path() + QStringLiteral("/app.db")));
    core::EventBus bus;
    core::EventApiImpl eventApi(&bus);
    FakeInstanceApi instanceApi;
    instanceApi.setRows({makeInfo(QStringLiteral("u1"), true)});
    FakeTuningApi tuning;
    QVector<QPair<QString, int>> bubbles;
    core::RuleEngineService service(&eventApi, &instanceApi, &tuning, &db);
    service.setBubbleSink([&bubbles](const QString& text, int durationMs) {
        bubbles.append(qMakePair(text, durationMs));
    });

    QJsonObject northStar = withThresholdCondition(
        withWhereEq(
            ruleJson(QStringLiteral("north-star-1"),
                     QStringLiteral("pet.instance.hit"),
                     {actionPlayMotion(QStringLiteral("flick")),
                      actionNotifyBubble(QStringLiteral("好久不见"), 4000)}),
            QStringLiteral("area_id"), QStringLiteral("head")),
        QStringLiteral("idle_for"), QStringLiteral("gt"), 300.0);
    northStar.insert(QStringLiteral("cancel_default"), true);
    const auto stored = service.upsertRuleJson(northStar, core::RuleScope::Panel,
                                               QString());
    QVERIFY(stored.has_value());
    QVERIFY(stored->cancelDefault);
    QCOMPARE(stored->actions.size(), 2);

    // Preset the idle clock 301s in the past (the bus stamps publish-time
    // timestamps, so a stale lastActivityMs cannot come from the wire).
    service.seedActivityTrackingForTest(
        QStringLiteral("u1"), QDateTime::currentMSecsSinceEpoch() - 301 * 1000,
        0);

    pet::Event event = makeEvent(
        QString(pet::kEventInstanceHit), QStringLiteral("u1"),
        core::makeHitPayload(QStringLiteral("head"), 12.0, 34.0, 0),
        /*cancellable=*/true);
    QCOMPARE(eventApi.publish(event), pet::PluginError::Ok);

    QCOMPARE(tuning.motions.size(), 1);
    QCOMPARE(tuning.motions.at(0).uuid, QStringLiteral("u1"));
    QCOMPARE(tuning.motions.at(0).group, QStringLiteral("flick"));
    QCOMPARE(tuning.motions.at(0).index, 0);
    QCOMPARE(bubbles.size(), 1);
    QCOMPARE(bubbles.at(0).first, QStringLiteral("好久不见"));
    QCOMPARE(bubbles.at(0).second, 4000);
    QCOMPARE(event.cancelled, true);
    QVERIFY(countMessages(*m_sink, "rule fired") >= 1);
}

// ── 8. north-star rule 2 ─────────────────────────────────────────────────────

void RuleEngineServiceTest::testNorthStarRule2()
{
    // motion_finished(group==tap) → play_pack_audio(ns_pack, tap): real
    // InstanceManager harness, real shared TuningApiImpl mount (the
    // mountedVoicePackChanged NOTIFY feeds the service's runtime cache
    // through the real InstanceApiImpl), QTemporaryDir meta.mko fixture,
    // fake renderer channel.
    QTemporaryDir base;
    QVERIFY(base.isValid());
    DatabaseManager db;
    QVERIFY(db.open(base.path() + QStringLiteral("/app.db")));

    const QString rendererRoot = base.path() + QStringLiteral("/renderer");
    QVERIFY(pack_fixture::writePack(rendererRoot, QStringLiteral("ns_pack"),
                                    pack_fixture::buildAudioOnlyBundle()));

    WsServer server;
    PendingRequests pending;
    InstanceManager manager(
        base.path(), server, pending,
        [&base](const PanelConfig& cfg) {
            PanelStateManager(base.path()).save(cfg);
        });
    core::InstanceApiImpl realApi(&manager, nullptr);
    core::TuningApiImpl tuning(&manager, nullptr);
    tuning.setPackScanDirs(rendererRoot, QString());
    ForceConnectedInstanceApi instanceApi(&realApi);

    core::EventBus bus;
    core::EventApiImpl eventApi(&bus);
    core::RuleEngineService service(&eventApi, &instanceApi, &tuning, &db);
    service.setPackScanDirs(rendererRoot, QString());
    QVector<QPair<QString, QJsonObject>> sent;
    service.setPackAudioChannel(
        [&sent](const QString& uuid, const QJsonObject& command) {
            sent.append(qMakePair(uuid, command));
            return 0;
        });
    QVector<QPair<QString, int>> bubbles;
    service.setBubbleSink([&bubbles](const QString& text, int durationMs) {
        bubbles.append(qMakePair(text, durationMs));
    });

    const QString uuid = manager.createInstance(QStringLiteral("NS2"));
    QVERIFY(!uuid.isEmpty());

    // Real mount through the SHARED tuning API — the NOTIFY fans a full
    // InstanceRuntime (mountedPackId "ns_pack") into the service cache.
    QCOMPARE(tuning.mountVoicePack(uuid, QStringLiteral("ns_pack")),
             pet::PluginError::Ok);

    QVERIFY(service
                .upsertRuleJson(
                    withWhereEq(
                        ruleJson(QStringLiteral("north-star-2"),
                                 QStringLiteral("pet.instance.motion_finished"),
                                 {actionPlayPackAudio(QStringLiteral("ns_pack"),
                                                      QStringLiteral("tap"))}),
                        QStringLiteral("group"), QStringLiteral("tap")),
                    core::RuleScope::Panel, QString())
                .has_value());

    pet::Event event = makeEvent(
        QString(pet::kEventInstanceMotionFinished), uuid,
        core::makeMotionFinishedPayload(QStringLiteral("tap"), 0));
    QCOMPARE(eventApi.publish(event), pet::PluginError::Ok);

    QCOMPARE(sent.size(), 1);
    QCOMPARE(sent.at(0).first, uuid);
    const QJsonObject command = sent.at(0).second;
    QCOMPARE(command.value(QStringLiteral("type")).toString(),
             QStringLiteral("command"));
    QCOMPARE(command.value(QStringLiteral("action")).toString(),
             QStringLiteral("play_audio"));
    const QString audioPath = command.value(QStringLiteral("payload"))
                                  .toObject()
                                  .value(QStringLiteral("audio_path"))
                                  .toString();
    QVERIFY(audioPath.contains(QStringLiteral("ns_pack")));
    QVERIFY(audioPath.endsWith(QStringLiteral("audio/tap01.ogg")));
    QCOMPARE(event.cancelled, false); // motion_finished is not cancellable

    // Mounted-mismatch + fail-fast: a rule asking for ANOTHER pack fails
    // its play_pack_audio and the follow-up notify_bubble never runs
    // (rule 2 refires — cooldown 0).
    QVERIFY(service
                .upsertRuleJson(
                    ruleJson(QStringLiteral("wrong-pack"),
                             QStringLiteral("pet.instance.motion_finished"),
                             {actionPlayPackAudio(QStringLiteral("other_pack"),
                                                  QStringLiteral("tap")),
                              actionNotifyBubble(QStringLiteral("never"), 0)}),
                    core::RuleScope::Panel, QString())
                .has_value());
    QCOMPARE(eventApi.publish(event), pet::PluginError::Ok);
    QCOMPARE(sent.size(), 2);          // north-star-2 fired again
    QCOMPARE(bubbles.size(), 0);       // wrong-pack aborted before the bubble
    QVERIFY(countMessages(*m_sink, "is not mounted") >= 1);
}

QTEST_MAIN(RuleEngineServiceTest)
#include "RuleEngineServiceTest.moc"