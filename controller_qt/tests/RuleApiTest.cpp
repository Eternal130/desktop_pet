// RuleApiTest (S8f, v1.6) — the pet.rules family end to end:
//   1. testProxyStampsOwnerAndScope — queryApi("pet.rules") hands the
//      context a per-plugin proxy; registerRule stores the rule under
//      "plugin.<ownId>." with RuleScope::Plugin + ownerUuid=<ownId>;
//      re-registering the same bare id atomically replaces.
//   2. testRegisterRejectionKeepsEngineUnchanged — an illegal schema
//      rejects with InvalidArgument (reason through the plugin's own
//      context log), the engine state untouched; a NEW id past the
//      200-rule ceiling rejects the same way while replacing an
//      existing id at capacity stays legal.
//   3. testUnregisterIsOwnerScoped — unregisterRule(bareId) reaches only
//      the plugin's own prefixed rule; another plugin's identically
//      named rule is untouchable; unknown ids are no-ops.
//   4. testQueryApiRouting — injected → the per-plugin proxy (distinct
//      from the raw shared impl); minVersion above the family version →
//      nullptr; not wired → nullptr ("feature absent").
//   5. testOwnerGateMatrix — plugin-owned play_motion/set_expression
//      denied at EMISSION time without instance_tuning (WARN, tuning
//      untouched, fire still recorded), allowed with the grant, denied
//      with the write switch off; notify_bubble + play_pack_audio are
//      NOT owner-gated (the latter reaches the mount check instead);
//      host rules never gated even with zero gate wiring.
//   5b. testOwnerGateNestedPluginIds — audit P1 regression: nested ids
//      "org.a" (granted) vs "org.a.b" (NOT granted) in ONE registry;
//      org.a.b's rule is DENIED (pre-fix prefix oracle borrowed org.a's
//      grant = false authorization), org.a's own rule still passes.
//   6. testShutdownSweep — PluginHost::shutdownAll sweeps a sloppy
//      plugin's leftover rules (WARN) while a plugin that unregistered
//      in shutdown() produces no sweep.
//   7. testNorthStarPluginRule — the plugin-rule flagship: a fake
//      plugin (granted instance_tuning) registers
//      on pet.pack.mounted(pack_id==ns_pack) → set_expression(smile) in
//      initialize(); the host mounts the pack through the REAL
//      TuningApiImpl on a REAL InstanceManager harness; the session's
//      S8a bus publish fans out to the rule engine (Low) whose gated
//      action passes the LIVE owner check and lands on the recording
//      seam over the real ITuningApi.
//
// Fakes: FakeInstanceApi / FakeTuningApi / CapturingSink (the
// RuleEngineServiceTest trio), FakeRulesPlugin (file-static records —
// factories are plain function pointers, PluginHostTest pattern),
// RecordingTuningApi (decorator over the REAL TuningApiImpl).
//
// Links pet_panel_core (the rule stack + plugin framework live there) +
// spdlog (LOG_* + CapturingSink). QTEST_MAIN — QSqlDatabase (real
// SQLite harnesses) wants a QCoreApplication.

#include "api/EventIds.hpp"
#include "api/IEventApi.hpp"
#include "api/IInstanceApi.hpp"
#include "api/IPanelPlugin.hpp"
#include "api/IRuleApi.hpp"
#include "api/ITuningApi.hpp"
#include "core/DatabaseManager.hpp"
#include "core/EventBus.hpp"
#include "core/EventCatalog.hpp"
#include "core/InstanceManager.hpp"
#include "core/InstanceSession.hpp"
#include "core/PanelConfig.hpp"
#include "core/PanelStateManager.hpp"
#include "core/PluginContextImpl.hpp"
#include "core/PluginHost.hpp"
#include "core/PluginRegistry.hpp"
#include "core/RuleEngineService.hpp"
#include "logging/Logging.hpp"
#include "network/PendingRequests.hpp"
#include "network/WsServer.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QVector>

#include <memory>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>

// Capturing spdlog sink — collects every formatted record (the
// RuleEngineServiceTest pattern) so WARN/INFO assertions work without
// file logging.
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

    void setRows(const QVector<pet::InstanceInfo>& rows) { m_rows = rows; }

    QVector<pet::InstanceInfo> m_rows;
    QVector<pet::IInstanceObserver*> m_observers;
};

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
};

// Slot-7 decorator over the REAL TuningApiImpl: records the gated
// set_expression before forwarding (proves the action went through the
// pet::ITuningApi surface while the real mount path stays live).
class RecordingTuningApi : public pet::ITuningApi {
public:
    struct ExpressionCall {
        QString uuid;
        QString id;
    };

    explicit RecordingTuningApi(pet::ITuningApi* inner)
        : m_inner(inner)
    {
    }

    QVector<ExpressionCall> expressions;

    pet::PluginError setExpression(const QString& uuid,
                                   const QString& id) override
    {
        expressions.append(ExpressionCall{uuid, id});
        return m_inner->setExpression(uuid, id);
    }
    pet::PluginError playMotion(const QString& uuid, const QString& group,
                                int index) override
    {
        return m_inner->playMotion(uuid, group, index);
    }
    pet::PluginError setOpacity(const QString& uuid, double v) override
    {
        return m_inner->setOpacity(uuid, v);
    }
    pet::PluginError setVolume(const QString& uuid, double v) override
    {
        return m_inner->setVolume(uuid, v);
    }
    pet::PluginError setMuted(const QString& uuid, bool m) override
    {
        return m_inner->setMuted(uuid, m);
    }
    pet::PluginError setFps(const QString& uuid, int fps) override
    {
        return m_inner->setFps(uuid, fps);
    }
    pet::PluginError triggerHitArea(const QString& uuid,
                                    const QString& area) override
    {
        return m_inner->triggerHitArea(uuid, area);
    }
    pet::PluginError mountVoicePack(const QString& uuid,
                                    const QString& packId) override
    {
        return m_inner->mountVoicePack(uuid, packId);
    }
    pet::PluginError unmountVoicePack(const QString& uuid) override
    {
        return m_inner->unmountVoicePack(uuid);
    }

private:
    pet::ITuningApi* m_inner; // not owned
};

// North-star harness decorator: a harness session has no renderer
// (connected == false), so instances() forces the flag on while the
// observer subscriptions forward to the real fanout (the mountedPackId
// runtime cache keeps flowing from the real property NOTIFYs).
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

struct RecordingListener : public pet::IEventListener {
    void onEvent(pet::Event& event) override { events.append(event); }
    QVector<pet::Event> events;
};

// ── Helpers ──────────────────────────────────────────────────────────────────

pet::InstanceInfo makeInfo(const QString& uuid, bool connected)
{
    pet::InstanceInfo info;
    info.uuid = uuid;
    info.label = uuid;
    info.modelName = QStringLiteral("Hiyori");
    info.status = QStringLiteral("running");
    info.connected = connected;
    return info;
}

pet::Event makeEvent(const QString& eventId, const QString& uuid,
                     const QJsonObject& payload)
{
    pet::Event event;
    event.eventId = eventId;
    event.instanceUuid = uuid;
    event.publisherId = QString(pet::kEventPublisherHost);
    event.payload = payload;
    return event;
}

QJsonObject actionPlayMotionFor(const QString& group)
{
    QJsonObject action;
    action.insert(QStringLiteral("kind"), QStringLiteral("play_motion"));
    action.insert(QStringLiteral("group"), group);
    return action;
}

QJsonObject actionSetExpressionFor(const QString& id)
{
    QJsonObject action;
    action.insert(QStringLiteral("kind"), QStringLiteral("set_expression"));
    action.insert(QStringLiteral("id"), id);
    return action;
}

QJsonObject actionNotifyBubbleFor(const QString& text)
{
    QJsonObject action;
    action.insert(QStringLiteral("kind"), QStringLiteral("notify_bubble"));
    action.insert(QStringLiteral("text"), text);
    return action;
}

QJsonObject actionPlayPackAudioFor(const QString& pack, const QString& group)
{
    QJsonObject action;
    action.insert(QStringLiteral("kind"), QStringLiteral("play_pack_audio"));
    action.insert(QStringLiteral("pack"), pack);
    action.insert(QStringLiteral("group"), group);
    return action;
}

QJsonObject ruleJsonFor(const QString& id, const QString& event,
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

int countMessages(const CapturingSink& sink, const char* needle)
{
    int hits = 0;
    for (const std::string& message : sink.messages) {
        if (message.find(needle) != std::string::npos)
            ++hits;
    }
    return hits;
}

// NOTE: plain escaped-quote literal, NOT a multi-line R"json(...)json"
// raw string — moc's preprocessor mis-lexes multi-line raw string
// literals and then fails to find the Q_OBJECT class below
// (PluginQueryApiTest pitfall note).
QString manifestJson(const char* id, const char* capabilities)
{
    return QStringLiteral(
        "{\"id\":\"%1\",\"version\":\"1.0\",\"api_version\":\"1.6\","
        "\"order\":100,\"capabilities\":[%2]}")
        .arg(QLatin1String(id), QLatin1String(capabilities));
}

// ── Fake static plugins (file-static records — factories are plain
// function pointers, no capturing state possible; PluginHostTest
// pattern). Each factory fully configures its instance BEFORE
// initialize() runs inside PluginHost::initializeAll. ────────────────

struct FakeRulesPlugin final : public pet::IPanelPlugin
{
    QJsonObject ruleToRegister;   // registered in initialize() when set
    bool hasRule = false;
    QString bareId;               // unregister target in shutdown()
    bool unregisterInShutdown = false;
    pet::PluginError registerResult = pet::PluginError::Generic;
    pet::IPluginContext* ctx = nullptr;

    pet::PluginError initialize(pet::IPluginContext& context) override
    {
        ctx = &context;
        pet::IExtApi* api = context.queryApi(pet::kRulesApiId, 1);
        if (api == nullptr)
            return pet::PluginError::Generic;
        auto* rules = dynamic_cast<pet::IRuleApi*>(api);
        if (rules == nullptr)
            return pet::PluginError::Generic;
        if (hasRule)
            registerResult = rules->registerRule(ruleToRegister);
        return pet::PluginError::Ok;
    }

    void shutdown() override
    {
        if (!unregisterInShutdown || ctx == nullptr)
            return;
        pet::IExtApi* api = ctx->queryApi(pet::kRulesApiId, 1);
        if (auto* rules = dynamic_cast<pet::IRuleApi*>(api))
            rules->unregisterRule(bareId);
    }
};

FakeRulesPlugin* g_sloppy = nullptr;
FakeRulesPlugin* g_tidy = nullptr;
FakeRulesPlugin* g_north = nullptr;

pet::IPanelPlugin* spawnSloppyPlugin()
{
    auto* p = new FakeRulesPlugin;
    p->hasRule = true;
    p->ruleToRegister =
        ruleJsonFor(QStringLiteral("sloppy-rule"),
                    QStringLiteral("pet.instance.hit"),
                    {actionPlayMotionFor(QStringLiteral("flick"))});
    p->unregisterInShutdown = false; // the leak the sweep must catch
    g_sloppy = p;
    return p;
}

pet::IPanelPlugin* spawnTidyPlugin()
{
    auto* p = new FakeRulesPlugin;
    p->hasRule = true;
    p->ruleToRegister =
        ruleJsonFor(QStringLiteral("tidy-rule"),
                    QStringLiteral("pet.model.loaded"),
                    {actionSetExpressionFor(QStringLiteral("smile"))});
    p->bareId = QStringLiteral("tidy-rule");
    p->unregisterInShutdown = true; // cleans up: no sweep expected
    g_tidy = p;
    return p;
}

pet::IPanelPlugin* spawnNorthStarPlugin()
{
    auto* p = new FakeRulesPlugin;
    p->hasRule = true;
    QJsonObject trigger;
    trigger.insert(
        QStringLiteral("event"),
        QString(pet::kEventPackMounted));
    QJsonObject predicate;
    predicate.insert(QStringLiteral("op"), QStringLiteral("eq"));
    predicate.insert(QStringLiteral("value"), QStringLiteral("ns_pack"));
    QJsonObject where;
    where.insert(QStringLiteral("pack_id"), predicate);
    trigger.insert(QStringLiteral("where"), where);
    QJsonObject action;
    action.insert(QStringLiteral("kind"), QStringLiteral("set_expression"));
    action.insert(QStringLiteral("id"), QStringLiteral("smile"));
    QJsonObject rule;
    rule.insert(QStringLiteral("schema_version"), 1);
    rule.insert(QStringLiteral("id"), QStringLiteral("pack-greet"));
    rule.insert(QStringLiteral("trigger"), trigger);
    rule.insert(QStringLiteral("actions"), QJsonArray{action});
    p->ruleToRegister = rule;
    p->unregisterInShutdown = false;
    g_north = p;
    return p;
}

// Per-slot harness: everything the direct-context slots need, in
// dependency order (members destruct in reverse — the service dies
// before its deps, the context api before the manager).
struct RulesHarness
{
    QTemporaryDir base;
    DatabaseManager db;
    core::EventBus bus;
    core::EventApiImpl eventApi;
    FakeInstanceApi instanceApi;
    FakeTuningApi tuning;
    core::RuleEngineService service;
    core::RuleApiImpl ruleApi;
    WsServer server;
    PendingRequests pending;
    InstanceManager manager;
    core::InstanceApiImpl ctxInstanceApi;

    explicit RulesHarness()
        : db(),
          eventApi(&bus),
          service(&eventApi, &instanceApi, &tuning, &db),
          ruleApi(&service),
          manager(base.path(), server, pending,
                  [dir = base.path()](const PanelConfig& cfg) {
                      PanelStateManager(dir).save(cfg);
                  }),
          ctxInstanceApi(&manager, nullptr)
    {
        db.open(base.path() + QStringLiteral("/app.db"));
        instanceApi.setRows({makeInfo(QStringLiteral("u1"), true)});
    }

    std::unique_ptr<core::PluginContextImpl>
    makeContext(const QString& pluginId)
    {
        return std::unique_ptr<core::PluginContextImpl>(
            new core::PluginContextImpl(
                pluginId, &ctxInstanceApi,
                /*sharedControl=*/nullptr, []() { return true; },
                /*pageModel=*/nullptr, /*stream=*/nullptr, base.path(),
                /*downloadService=*/nullptr, QStringList(),
                /*voicePackRefresh=*/{}, /*parent=*/nullptr));
    }
};

// Minimal protobuf wire-format serializer (the TuningApiImplTest /
// RuleEngineServiceTest pattern) — a meta.mko Bundle with one group and
// one audio-only action; only MOUNTABILITY matters for the north-star
// slot (the rule fires set_expression, no audio needed).
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

QByteArray buildMountableBundle()
{
    QByteArray meta;
    meta += encodeStringField(1, QStringLiteral("NS Pack"));
    meta += encodeStringField(2, QStringLiteral("ns_pack"));
    QByteArray group;
    group += encodeStringField(1, QStringLiteral("tap"));
    group += encodeVarintField(2, 5);
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

class RuleApiTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init()
    {
        g_sloppy = nullptr;
        g_tidy = nullptr;
        g_north = nullptr;
    }

    void testProxyStampsOwnerAndScope();
    void testRegisterRejectionKeepsEngineUnchanged();
    void testUnregisterIsOwnerScoped();
    void testQueryApiRouting();
    void testOwnerGateMatrix();
    void testOwnerGateNestedPluginIds();
    void testShutdownSweep();
    void testNorthStarPluginRule();

private:
    std::shared_ptr<CapturingSink> m_sink;
};

void RuleApiTest::initTestCase()
{
    m_sink = std::make_shared<CapturingSink>();
    auto logger = std::make_shared<spdlog::logger>("test_rule_api", m_sink);
    logger->set_level(spdlog::level::trace);
    logger->flush_on(spdlog::level::trace);
    spdlog::set_default_logger(logger);
}

void RuleApiTest::cleanupTestCase()
{
    spdlog::shutdown();
}

// ── 1. proxy stamps owner prefix + scope ─────────────────────────────────────

void RuleApiTest::testProxyStampsOwnerAndScope()
{
    RulesHarness h;
    auto ctx = h.makeContext(QStringLiteral("org.test.plug"));
    ctx->setRuleApi(&h.ruleApi);

    pet::IExtApi* api = ctx->queryApi(pet::kRulesApiId, 1);
    QVERIFY(api != nullptr);
    QVERIFY(api != static_cast<pet::IExtApi*>(&h.ruleApi)); // proxy, not raw
    auto* rules = dynamic_cast<pet::IRuleApi*>(api);
    QVERIFY(rules != nullptr);

    QCOMPARE(h.service.ruleCount(), 0);
    QCOMPARE(rules->registerRule(
                 ruleJsonFor(QStringLiteral("greet"),
                             QStringLiteral("pet.instance.hit"),
                             {actionPlayMotionFor(QStringLiteral("flick"))})),
             pet::PluginError::Ok);

    // Stored under the plugin's OWN prefix with Plugin scope + owner.
    QCOMPARE(h.service.ruleCount(), 1);
    const auto stored = h.service.ruleById(
        QStringLiteral("plugin.org.test.plug.greet"));
    QVERIFY(stored.has_value());
    QCOMPARE(stored->scope, core::RuleScope::Plugin);
    QCOMPARE(stored->ownerUuid, QStringLiteral("org.test.plug"));
    QCOMPARE(stored->triggerEvent, QStringLiteral("pet.instance.hit"));
    // The plugin's bare id is NOT reachable un-prefixed.
    QVERIFY(!h.service.ruleById(QStringLiteral("greet")).has_value());

    // Same bare id re-registered → atomic REPLACE, still one rule.
    QCOMPARE(rules->registerRule(
                 ruleJsonFor(QStringLiteral("greet"),
                             QStringLiteral("pet.model.loaded"),
                             {actionSetExpressionFor(QStringLiteral("smile"))})),
             pet::PluginError::Ok);
    QCOMPARE(h.service.ruleCount(), 1);
    const auto replaced = h.service.ruleById(
        QStringLiteral("plugin.org.test.plug.greet"));
    QVERIFY(replaced.has_value());
    QCOMPARE(replaced->triggerEvent, QStringLiteral("pet.model.loaded"));
    QCOMPARE(replaced->actions.size(), 1);
    QCOMPARE(replaced->actions.at(0).kind, core::RuleActionKind::SetExpression);

    // The proxy also subscribed the trigger on the real bus: the
    // replaced rule's pet.model.loaded + the activity triple (hit,
    // drag_ended, motion_finished) = 4 unique ids.
    QCOMPARE(h.bus.subscriptionCount(), 4);
}

// ── 2. rejection semantics — engine unchanged ────────────────────────────────

void RuleApiTest::testRegisterRejectionKeepsEngineUnchanged()
{
    RulesHarness h;
    auto ctx = h.makeContext(QStringLiteral("org.test.plug"));
    ctx->setRuleApi(&h.ruleApi);
    auto* rules =
        dynamic_cast<pet::IRuleApi*>(ctx->queryApi(pet::kRulesApiId, 1));
    QVERIFY(rules != nullptr);

    // Good seed rule so "unchanged" is observable.
    QCOMPARE(rules->registerRule(
                 ruleJsonFor(QStringLiteral("seed"),
                             QStringLiteral("pet.instance.hit"),
                             {actionPlayMotionFor(QStringLiteral("flick"))})),
             pet::PluginError::Ok);
    QCOMPARE(h.service.ruleCount(), 1);

    // Illegal schema (unknown top-level field): InvalidArgument, engine
    // untouched, reason routed through the PLUGIN's context log.
    QJsonObject bad = ruleJsonFor(
        QStringLiteral("bad"), QStringLiteral("pet.instance.hit"),
        {actionPlayMotionFor(QStringLiteral("flick"))});
    bad.insert(QStringLiteral("bogus_field"), 1);
    const int sinkBefore = m_sink->messages.size();
    QCOMPARE(rules->registerRule(bad), pet::PluginError::InvalidArgument);
    QCOMPARE(h.service.ruleCount(), 1);
    QVERIFY(countMessages(*m_sink,
                          "rulesApi().registerRule: rejected") >= 1);
    QVERIFY(countMessages(*m_sink, "[plugin/org.test.plug]") >= 1);
    QVERIFY(m_sink->messages.size() > sinkBefore);

    // MAX_RULES: fill to the 200 ceiling, then a NEW id rejects while
    // replacing an existing id at capacity stays legal.
    for (int i = 0; i < 199; ++i) {
        QCOMPARE(rules->registerRule(
                     ruleJsonFor(QStringLiteral("r%1").arg(i),
                                 QStringLiteral("pet.model.loaded"),
                                 {actionPlayMotionFor(QStringLiteral("x"))})),
                 pet::PluginError::Ok);
    }
    QCOMPARE(h.service.ruleCount(), 200);
    QCOMPARE(rules->registerRule(
                 ruleJsonFor(QStringLiteral("overflow"),
                             QStringLiteral("pet.model.loaded"),
                             {actionPlayMotionFor(QStringLiteral("x"))})),
             pet::PluginError::InvalidArgument);
    QCOMPARE(h.service.ruleCount(), 200);
    QCOMPARE(rules->registerRule(
                 ruleJsonFor(QStringLiteral("seed"), // existing id
                             QStringLiteral("pet.model.loaded"),
                             {actionPlayMotionFor(QStringLiteral("x"))})),
             pet::PluginError::Ok);
    QCOMPARE(h.service.ruleCount(), 200);
}

// ── 3. unregister is owner-scoped ────────────────────────────────────────────

void RuleApiTest::testUnregisterIsOwnerScoped()
{
    RulesHarness h;
    auto ctxA = h.makeContext(QStringLiteral("org.test.a"));
    ctxA->setRuleApi(&h.ruleApi);
    auto ctxB = h.makeContext(QStringLiteral("org.test.b"));
    ctxB->setRuleApi(&h.ruleApi);

    auto* rulesA =
        dynamic_cast<pet::IRuleApi*>(ctxA->queryApi(pet::kRulesApiId, 1));
    auto* rulesB =
        dynamic_cast<pet::IRuleApi*>(ctxB->queryApi(pet::kRulesApiId, 1));
    QVERIFY(rulesA != nullptr && rulesB != nullptr);

    QCOMPARE(rulesA->registerRule(
                 ruleJsonFor(QStringLiteral("shared-name"),
                             QStringLiteral("pet.instance.hit"),
                             {actionPlayMotionFor(QStringLiteral("flick"))})),
             pet::PluginError::Ok);
    QCOMPARE(rulesB->registerRule(
                 ruleJsonFor(QStringLiteral("shared-name"),
                             QStringLiteral("pet.model.loaded"),
                             {actionSetExpressionFor(QStringLiteral("smile"))})),
             pet::PluginError::Ok);
    QCOMPARE(h.service.ruleCount(), 2);

    // B unregistering "shared-name" removes ONLY B's own stored rule.
    rulesB->unregisterRule(QStringLiteral("shared-name"));
    QCOMPARE(h.service.ruleCount(), 1);
    QVERIFY(h.service
                .ruleById(QStringLiteral("plugin.org.test.a.shared-name"))
                .has_value());
    QVERIFY(!h.service
                 .ruleById(QStringLiteral("plugin.org.test.b.shared-name"))
                 .has_value());

    // A cannot touch B's namespace either — a crafted bare id carrying
    // B's prefix lands under A's OWN prefix (no such rule → no-op).
    rulesA->unregisterRule(QStringLiteral("org.test.b.shared-name"));
    QCOMPARE(h.service.ruleCount(), 1);

    // Unknown id → legal no-op.
    rulesA->unregisterRule(QStringLiteral("never-registered"));
    QCOMPARE(h.service.ruleCount(), 1);

    // Own bare id removes cleanly.
    rulesA->unregisterRule(QStringLiteral("shared-name"));
    QCOMPARE(h.service.ruleCount(), 0);
}

// ── 4. queryApi routing (three states) ───────────────────────────────────────

void RuleApiTest::testQueryApiRouting()
{
    RulesHarness h;

    // 1) Injected → the per-plugin proxy (served, non-null, distinct
    //    per context and from the raw shared impl).
    auto ctx1 = h.makeContext(QStringLiteral("org.test.one"));
    ctx1->setRuleApi(&h.ruleApi);
    pet::IExtApi* api1 = ctx1->queryApi(pet::kRulesApiId, 1);
    QVERIFY(api1 != nullptr);
    QVERIFY(api1 != static_cast<pet::IExtApi*>(&h.ruleApi));
    auto ctx2 = h.makeContext(QStringLiteral("org.test.two"));
    ctx2->setRuleApi(&h.ruleApi);
    pet::IExtApi* api2 = ctx2->queryApi(pet::kRulesApiId, 1);
    QVERIFY(api2 != nullptr);
    QVERIFY(api2 != api1); // per-context proxies

    // 2) minVersion above the family version → nullptr ("feature absent").
    QCOMPARE(ctx1->queryApi(pet::kRulesApiId, pet::kRulesApiVersion + 1),
             nullptr);
    QVERIFY(ctx1->queryApi(pet::kRulesApiId, pet::kRulesApiVersion)
            != nullptr); // exact version served

    // 3) Not wired (no setRuleApi) → nullptr, never a crash.
    auto ctx3 = h.makeContext(QStringLiteral("org.test.three"));
    QCOMPARE(ctx3->queryApi(pet::kRulesApiId, 1), nullptr);
    QCOMPARE(ctx3->queryApi("pet.does_not_exist", 1), nullptr);
}

// ── 5. owner action gate matrix ──────────────────────────────────────────────

void RuleApiTest::testOwnerGateMatrix()
{
    // Part A — the gate WIRED (grants + switch both live-toggled).
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
        core::RuleEngineService service(&eventApi, &instanceApi, &tuning,
                                        &db);

        QStringList granted;
        bool writeSwitch = true;
        service.setOwnerWriteEnabled(
            [&writeSwitch]() { return writeSwitch; });
        service.setOwnerCapabilityQuery(
            [&granted](const QString& ownerPluginId,
                       QLatin1StringView capability) {
                return ownerPluginId == QStringLiteral("org.test.plug")
                    && granted.contains(QString(capability));
            });

        // (a) plugin play_motion, NO grant → denied WARN, tuning
        //     untouched, the fire still recorded ("rule fired" logged).
        QVERIFY(service
                    .upsertRuleJson(
                        ruleJsonFor(
                            QStringLiteral("deny-motion"),
                            QStringLiteral("pet.instance.hit"),
                            {actionPlayMotionFor(QStringLiteral("flick"))}),
                        core::RuleScope::Plugin,
                        QStringLiteral("org.test.plug"))
                    .has_value());
        pet::Event hit = makeEvent(
            QString(pet::kEventInstanceHit), QStringLiteral("u1"),
            core::makeHitPayload(QStringLiteral("head"), 0.0, 0.0, 0));
        QCOMPARE(eventApi.publish(hit), pet::PluginError::Ok);
        QCOMPARE(tuning.motions.size(), 0);
        QVERIFY(countMessages(*m_sink, "action denied") >= 1);
        QVERIFY(countMessages(*m_sink, "rule fired") >= 1);

        // (b) grant + switch on → BOTH gated channels pass.
        granted << QString(pet::kCapabilityInstanceTuning);
        QVERIFY(service
                    .upsertRuleJson(
                        ruleJsonFor(
                            QStringLiteral("allow-motion"),
                            QStringLiteral("pet.model.loaded"),
                            {actionPlayMotionFor(QStringLiteral("nod"))}),
                        core::RuleScope::Plugin,
                        QStringLiteral("org.test.plug"))
                    .has_value());
        QVERIFY(service
                    .upsertRuleJson(
                        ruleJsonFor(
                            QStringLiteral("allow-expression"),
                            QStringLiteral("pet.model.load_failed"),
                            {actionSetExpressionFor(QStringLiteral("smile"))}),
                        core::RuleScope::Plugin,
                        QStringLiteral("org.test.plug"))
                    .has_value());
        pet::Event loaded = makeEvent(
            QString(pet::kEventModelLoaded), QStringLiteral("u1"),
            core::makeModelLoadedPayload(QStringLiteral("Hiyori"), QStringList(), QStringList()));
        QCOMPARE(eventApi.publish(loaded), pet::PluginError::Ok);
        QCOMPARE(tuning.motions.size(), 1);
        QCOMPARE(tuning.motions.at(0).group, QStringLiteral("nod"));
        pet::Event failed = makeEvent(
            QString(pet::kEventModelLoadFailed), QStringLiteral("u1"),
            core::makeModelLoadFailedPayload(5003, QStringLiteral("boom")));
        QCOMPARE(eventApi.publish(failed), pet::PluginError::Ok);
        QCOMPARE(tuning.expressions.size(), 1);
        QCOMPARE(tuning.expressions.at(0).id, QStringLiteral("smile"));

        // (c) grant + switch OFF → denied again (the LIVE kill-switch:
        //     the rule was registered while enabled, no re-register).
        writeSwitch = false;
        pet::Event loaded2 = loaded;
        QCOMPARE(eventApi.publish(loaded2), pet::PluginError::Ok);
        QCOMPARE(tuning.motions.size(), 1); // unchanged — denied
        QVERIFY(countMessages(*m_sink, "write switch is off") >= 1);
        writeSwitch = true;
    }

    // Part B — ZERO gate wiring: host rules pass, ungated channels pass,
    // play_pack_audio reaches its MOUNT check (not the owner gate).
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
        core::RuleEngineService service(&eventApi, &instanceApi, &tuning,
                                        &db);
        service.setBubbleSink([&bubbles](const QString& text, int durationMs) {
            bubbles.append(qMakePair(text, durationMs));
        });
        // NOTE: no setOwnerCapabilityQuery / setOwnerWriteEnabled — the
        // fail-closed defaults must only ever bite PLUGIN rules on the
        // two gated channels.

        // Host (Panel) rule play_motion — never gated, no wiring needed.
        QVERIFY(service
                    .upsertRuleJson(
                        ruleJsonFor(
                            QStringLiteral("host-motion"),
                            QStringLiteral("pet.instance.hit"),
                            {actionPlayMotionFor(QStringLiteral("flick"))}),
                        core::RuleScope::Panel, QString())
                    .has_value());
        pet::Event hit = makeEvent(
            QString(pet::kEventInstanceHit), QStringLiteral("u1"),
            core::makeHitPayload(QStringLiteral("head"), 0.0, 0.0, 0));
        QCOMPARE(eventApi.publish(hit), pet::PluginError::Ok);
        QCOMPARE(tuning.motions.size(), 1);

        // Plugin notify_bubble — ungated by design.
        QVERIFY(service
                    .upsertRuleJson(
                        ruleJsonFor(
                            QStringLiteral("plugin-bubble"),
                            QStringLiteral("pet.model.loaded"),
                            {actionNotifyBubbleFor(QStringLiteral("hi"))}),
                        core::RuleScope::Plugin,
                        QStringLiteral("org.test.plug"))
                    .has_value());
        pet::Event loaded = makeEvent(
            QString(pet::kEventModelLoaded), QStringLiteral("u1"),
            core::makeModelLoadedPayload(QStringLiteral("Hiyori"), QStringList(), QStringList()));
        QCOMPARE(eventApi.publish(loaded), pet::PluginError::Ok);
        QCOMPARE(bubbles.size(), 1);
        QCOMPARE(bubbles.at(0).first, QStringLiteral("hi"));

        // Plugin play_pack_audio — ungated by design: the denial (if
        // any) comes from the MOUNT check, never the owner gate.
        QVERIFY(service
                    .upsertRuleJson(
                        ruleJsonFor(
                            QStringLiteral("plugin-audio"),
                            QStringLiteral("pet.model.load_failed"),
                            {actionPlayPackAudioFor(
                                QStringLiteral("other_pack"),
                                QStringLiteral("tap"))}),
                        core::RuleScope::Plugin,
                        QStringLiteral("org.test.plug"))
                    .has_value());
        pet::Event failed = makeEvent(
            QString(pet::kEventModelLoadFailed), QStringLiteral("u1"),
            core::makeModelLoadFailedPayload(5003, QStringLiteral("boom")));
        const int deniedBefore = countMessages(*m_sink, "action denied");
        QCOMPARE(eventApi.publish(failed), pet::PluginError::Ok);
        QCOMPARE(countMessages(*m_sink, "action denied"), deniedBefore);
        QVERIFY(countMessages(*m_sink, "is not mounted") >= 1);
    }
}

// ── 5b. owner gate × nested plugin ids (audit P1 regression) ─────────────────

void RuleApiTest::testOwnerGateNestedPluginIds()
{
    // Audit P1: the pre-fix oracle keyed on the RULE id with a
    // "plugin.<id>." startsWith scan over orderedEntries(). With the
    // nested pair below, orderedEntries() (same order=100, id asc)
    // lists org.a FIRST, and the org.a.b-owned rule's stored id
    // ("plugin.org.a.b.nested") STARTS WITH "plugin.org.a." → the scan
    // answered org.a's grant → the motion FIRED (false authorization).
    // The fixed seam passes the OWNER PLUGIN ID (rule.ownerUuid) and
    // the oracle does an EXACT entry() lookup, so org.a.b has no grant
    // of its own → DENIED.
    QTemporaryDir base;
    QVERIFY(base.isValid());
    DatabaseManager db;
    QVERIFY(db.open(base.path() + QStringLiteral("/app.db")));
    core::EventBus bus;
    core::EventApiImpl eventApi(&bus);
    FakeInstanceApi instanceApi;
    instanceApi.setRows({makeInfo(QStringLiteral("u1"), true)});
    FakeTuningApi tuning;
    core::RuleEngineService service(&eventApi, &instanceApi, &tuning,
                                    &db);

    // ONE registry, nested ids, asymmetric grants: org.a holds
    // instance_tuning; org.a.b holds NOTHING (empty capabilities list).
    core::PluginRegistry registry;
    registry.addStaticPlugin(
        manifestJson("org.a", "\"instance_tuning\""), nullptr);
    registry.addStaticPlugin(manifestJson("org.a.b", ""), nullptr);
    service.setOwnerCapabilityQuery(
        [&registry](const QString& ownerPluginId,
                    QLatin1StringView capability) {
            const core::PluginEntry* entry =
                registry.entry(ownerPluginId);
            return entry != nullptr
                && entry->manifest.capabilities.contains(capability);
        });
    service.setOwnerWriteEnabled([]() { return true; });

    // (a) org.a.b's rule fires play_motion → DENIED. Pre-fix this exact
    //     wiring authorized the action (org.a's grant leaked through
    //     the prefix match); the exact-owner oracle must not. Also red
    //     pre-fix in the OPPOSITE direction: had the service still
    //     passed rule.id, entry("plugin.org.a.b.nested") would miss →
    //     (b) below would fail (allowed rule wrongly denied).
    QVERIFY(service
                .upsertRuleJson(
                    ruleJsonFor(
                        QStringLiteral("nested"),
                        QStringLiteral("pet.instance.hit"),
                        {actionPlayMotionFor(QStringLiteral("flick"))}),
                    core::RuleScope::Plugin,
                    QStringLiteral("org.a.b"))
                .has_value());
    pet::Event hit = makeEvent(
        QString(pet::kEventInstanceHit), QStringLiteral("u1"),
        core::makeHitPayload(QStringLiteral("head"), 0.0, 0.0, 0));
    QCOMPARE(eventApi.publish(hit), pet::PluginError::Ok);
    QCOMPARE(tuning.motions.size(), 0); // DENIED — org.a.b has no grant
    QVERIFY(countMessages(*m_sink, "action denied") >= 1);
    QVERIFY(countMessages(*m_sink, "rule fired") >= 1); // fire recorded

    // (b) Reverse: org.a's OWN rule on the SAME event — granted owner,
    //     exact-id lookup finds it → ALLOWED (proves the tightened key
    //     did not over-restrict genuine grants).
    QVERIFY(service
                .upsertRuleJson(
                    ruleJsonFor(
                        QStringLiteral("outer"),
                        QStringLiteral("pet.instance.hit"),
                        {actionPlayMotionFor(QStringLiteral("nod"))}),
                    core::RuleScope::Plugin,
                    QStringLiteral("org.a"))
                .has_value());
    pet::Event hit2 = hit; // fresh copy — hit is cancellable
    QCOMPARE(eventApi.publish(hit2), pet::PluginError::Ok);
    QCOMPARE(tuning.motions.size(), 1);
    QCOMPARE(tuning.motions.at(0).group, QStringLiteral("nod"));
}

// ── 6. PluginHost shutdown sweep ─────────────────────────────────────────────

void RuleApiTest::testShutdownSweep()
{
    RulesHarness h;
    core::PluginRegistry registry;
    core::PluginHost host(registry);
    host.setInstanceManager(&h.manager);
    host.setInstanceApi(&h.ctxInstanceApi);
    host.setEventApi(&h.eventApi);
    host.setRuleApi(&h.ruleApi); // shared impl over the harness service

    registry.addStaticPlugin(
        manifestJson("org.test.sloppy", "\"instance_tuning\""),
        &spawnSloppyPlugin);
    registry.addStaticPlugin(
        manifestJson("org.test.tidy", "\"instance_tuning\""),
        &spawnTidyPlugin);

    host.initializeAll();
    QVERIFY(g_sloppy != nullptr && g_tidy != nullptr);
    QCOMPARE(g_sloppy->registerResult, pet::PluginError::Ok);
    QCOMPARE(g_tidy->registerResult, pet::PluginError::Ok);
    QCOMPARE(h.service.ruleCount(), 2);
    QVERIFY(h.service
                .ruleById(QStringLiteral("plugin.org.test.sloppy.sloppy-rule"))
                .has_value());
    QVERIFY(h.service
                .ruleById(QStringLiteral("plugin.org.test.tidy.tidy-rule"))
                .has_value());

    const int sweptBefore = countMessages(*m_sink, "at unload — swept");
    const int ruleSweepsBefore =
        countMessages(*m_sink, "registered rule(s) at unload — swept");
    host.shutdownAll(); // reverse order: tidy (unregisters itself) → sloppy

    // Both rule sets are gone; only the SLOPPY plugin needed the sweep
    // (tidy unregistered in shutdown — no WARN for it).
    QCOMPARE(h.service.ruleCount(), 0);
    QVERIFY(!h.service
                 .ruleById(
                     QStringLiteral("plugin.org.test.sloppy.sloppy-rule"))
                 .has_value());
    QVERIFY(!h.service
                 .ruleById(QStringLiteral("plugin.org.test.tidy.tidy-rule"))
                 .has_value());
    QCOMPARE(countMessages(*m_sink, "registered rule(s) at unload — swept"),
             ruleSweepsBefore + 1); // exactly one sweep WARN (sloppy)
    QVERIFY(countMessages(*m_sink, "at unload — swept") >= sweptBefore);
}

// ── 7. north-star plugin rule (full e2e) ─────────────────────────────────────

void RuleApiTest::testNorthStarPluginRule()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    DatabaseManager db;
    QVERIFY(db.open(base.path() + QStringLiteral("/app.db")));
    const QString rendererRoot = base.path() + QStringLiteral("/renderer");
    QVERIFY(pack_fixture::writePack(rendererRoot, QStringLiteral("ns_pack"),
                                    pack_fixture::buildMountableBundle()));

    // The bus/eventApi are declared BEFORE the InstanceManager on
    // purpose — the PanelApplication registration-order discipline:
    // sessions hold the publisher pointer and unsubscribe in their
    // destructor, so the bus must OUTLIVE every session (stack-reverse
    // here mirrors registration-reverse there).
    core::EventBus bus;
    core::EventApiImpl eventApi(&bus);

    WsServer server;
    PendingRequests pending;
    InstanceManager manager(
        base.path(), server, pending,
        [&base](const PanelConfig& cfg) {
            PanelStateManager(base.path()).save(cfg);
        });
    core::InstanceApiImpl realApi(&manager, nullptr);
    core::TuningApiImpl realTuning(&manager, nullptr);
    realTuning.setPackScanDirs(rendererRoot, QString());
    ForceConnectedInstanceApi instanceApi(&realApi);
    RecordingTuningApi tuning(&realTuning);

    core::RuleEngineService service(&eventApi, &instanceApi, &tuning, &db);
    service.setPackScanDirs(rendererRoot, QString());
    core::RuleApiImpl ruleApi(&service);

    // The production-shape gate wiring: capability oracle over the REAL
    // registry (EXACT owner-id entry() lookup — the audit-P1-fixed
    // PanelApplication shape; unknown owner → fail closed) + the write
    // switch permanently on (this test's focus is the grant path).
    core::PluginRegistry registry;
    service.setOwnerCapabilityQuery(
        [&registry](const QString& ownerPluginId,
                    QLatin1StringView capability) {
            const core::PluginEntry* entry =
                registry.entry(ownerPluginId);
            return entry != nullptr
                && entry->manifest.capabilities.contains(capability);
        });
    service.setOwnerWriteEnabled([]() { return true; });

    core::PluginHost host(registry);
    host.setInstanceManager(&manager);
    host.setInstanceApi(&realApi);
    host.setEventApi(&eventApi);
    host.setRuleApi(&ruleApi);
    host.setWriteEnabledProvider([]() { return true; });

    // The fake plugin (granted instance_tuning) registers the flagship
    // rule inside initialize() — through the SAME queryApi proxy surface
    // a real plugin uses.
    registry.addStaticPlugin(
        manifestJson("org.test.northstar", "\"instance_tuning\""),
        &spawnNorthStarPlugin);
    host.initializeAll();

    QVERIFY(g_north != nullptr);
    QCOMPARE(g_north->registerResult, pet::PluginError::Ok);
    QCOMPARE(host.startedIds(),
             QStringList{QStringLiteral("org.test.northstar")});
    QCOMPARE(service.ruleCount(), 1);
    const auto stored = service.ruleById(
        QStringLiteral("plugin.org.test.northstar.pack-greet"));
    QVERIFY(stored.has_value());
    QCOMPARE(stored->scope, core::RuleScope::Plugin);
    QCOMPARE(stored->ownerUuid, QStringLiteral("org.test.northstar"));

    // Full chain proof listener: pet.pack.mounted must reach the bus
    // with host identity before the rule engine (Low) evaluates it.
    RecordingListener packListener;
    QCOMPARE(eventApi.subscribe(QString(pet::kEventPackMounted),
                                &packListener, pet::EventPriority::Highest),
             pet::PluginError::Ok);

    const QString uuid = manager.createInstance(QStringLiteral("NS3"));
    QVERIFY(!uuid.isEmpty());
    // PanelApplication's S8a wiring, mirrored: hand the session the
    // shared publisher so its mount publishes on the REAL bus.
    for (int row = 0; row < manager.rowCount(); ++row) {
        InstanceSession* s = manager.instanceAt(row);
        if (s != nullptr && s->uuid() == uuid)
            s->setEventPublisher(&eventApi);
    }

    // Host mounts the pack through the REAL shared TuningApiImpl → the
    // session publishes pet.pack.mounted → rule matches (where
    // pack_id == ns_pack) → the GATED set_expression passes the live
    // owner check → the recording seam over the real ITuningApi.
    QCOMPARE(realTuning.mountVoicePack(uuid, QStringLiteral("ns_pack")),
             pet::PluginError::Ok);

    QCOMPARE(tuning.expressions.size(), 1);
    QCOMPARE(tuning.expressions.at(0).uuid, uuid);
    QCOMPARE(tuning.expressions.at(0).id, QStringLiteral("smile"));

    // The event chain was complete: the bus listener saw the host's
    // publish, and the rule engine recorded the fire.
    QCOMPARE(packListener.events.size(), 1);
    QCOMPARE(packListener.events.at(0).publisherId,
             QString(pet::kEventPublisherHost));
    QCOMPARE(packListener.events.at(0).instanceUuid, uuid);
    QCOMPARE(packListener.events.at(0).payload.value(QStringLiteral("pack_id"))
                  .toString(),
             QStringLiteral("ns_pack"));
    QVERIFY(countMessages(*m_sink, "rule fired") >= 1);

    // Revocation stops the rule WITHOUT re-registration: flip the grant
    // off, unmount + re-mount (a fresh pet.pack.mounted publish), and
    // the second set_expression is denied at emission time.
    registry.entry(QStringLiteral("org.test.northstar"))
        ->manifest.capabilities.removeAll(
            QString(pet::kCapabilityInstanceTuning));
    realTuning.unmountVoicePack(uuid);
    QCOMPARE(realTuning.mountVoicePack(uuid, QStringLiteral("ns_pack")),
             pet::PluginError::Ok);
    QCOMPARE(tuning.expressions.size(), 1); // still exactly one
    QVERIFY(countMessages(*m_sink, "action denied") >= 1);
}

QTEST_MAIN(RuleApiTest)
#include "RuleApiTest.moc"
