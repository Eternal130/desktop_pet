// RuleEngineTest (S8d) — the declarative rule engine core: strict schema
// parse/validate (reject matrix, table-driven), compile-to-CompiledRule
// (defaults + full-feature deep check), and the pure evaluation kernel
// (cooldown / one-shot / enable-cycle / where type-sensitivity / all five
// condition kinds incl. wrap-around time windows / fail-closed unknowns /
// connected gate), plus rule-set management (atomic upsert replace, MAX_RULES
// ceiling, owner-prefix sweep, composite ordering, differential subscription
// set) and the load-time cancel_default WARN (CapturingSink, same pattern as
// MessageDispatcherTest).
//
// Everything is pure logic — QTEST_APPLESS_MAIN, no event loop, no renderer,
// no bus (S8d deliberately unwired; see RuleEngine.hpp TODO(S8e)/TODO(S8f)).
//
// Links pet_panel_core (RuleEngine lives there) + spdlog (LOG_* + sink).

#include "core/RuleEngine.hpp"
#include "core/RuleSchema.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QTest>
#include <QVariantList>

#include <functional>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>

#include "logging/Logging.hpp"

// Capturing spdlog sink — collects every formatted record so the
// cancel_default WARN test can assert the load-time warning. Installed as the
// default logger's only sink in initTestCase() (same pattern as
// MessageDispatcherTest).
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

// ── Fixtures ────────────────────────────────────────────────────────────────

// The valid baseline every reject-matrix row mutates: minimal legal rule.
QJsonObject validRuleJson()
{
    QJsonObject where;
    QJsonObject areaEq;
    areaEq.insert(QStringLiteral("op"), QStringLiteral("eq"));
    areaEq.insert(QStringLiteral("value"), QStringLiteral("head"));
    where.insert(QStringLiteral("area_id"), areaEq);

    QJsonObject trigger;
    trigger.insert(QStringLiteral("event"), QStringLiteral("pet.instance.hit"));
    trigger.insert(QStringLiteral("where"), where);

    QJsonObject action;
    action.insert(QStringLiteral("kind"), QStringLiteral("play_motion"));
    action.insert(QStringLiteral("group"), QStringLiteral("flick"));

    QJsonObject rule;
    rule.insert(QStringLiteral("schema_version"), 1);
    rule.insert(QStringLiteral("id"), QStringLiteral("no-head-pat"));
    rule.insert(QStringLiteral("trigger"), trigger);
    rule.insert(QStringLiteral("actions"), QJsonArray{action});
    return rule;
}

// Baseline WITHOUT where/condition — the canvas for where/condition tests.
QJsonObject bareRule(const QString& id = QStringLiteral("cond-rule"))
{
    QJsonObject trigger;
    trigger.insert(QStringLiteral("event"), QStringLiteral("pet.instance.hit"));

    QJsonObject action;
    action.insert(QStringLiteral("kind"), QStringLiteral("play_motion"));
    action.insert(QStringLiteral("group"), QStringLiteral("flick"));

    QJsonObject rule;
    rule.insert(QStringLiteral("schema_version"), 1);
    rule.insert(QStringLiteral("id"), id);
    rule.insert(QStringLiteral("trigger"), trigger);
    rule.insert(QStringLiteral("actions"), QJsonArray{action});
    return rule;
}

QJsonObject ruleWithCondition(const QJsonObject& condition)
{
    QJsonObject rule = bareRule();
    rule.insert(QStringLiteral("condition"), QJsonArray{condition});
    return rule;
}

QJsonObject ruleWithWhere(const QJsonObject& where)
{
    QJsonObject rule = bareRule();
    QJsonObject trigger = rule.value(QStringLiteral("trigger")).toObject();
    trigger.insert(QStringLiteral("where"), where);
    rule.insert(QStringLiteral("trigger"), trigger);
    return rule;
}

// The inner predicate object {op, value?} (no field key).
QJsonObject predObj(const QString& op,
                    const QJsonValue& value = QJsonValue(QJsonValue::Undefined))
{
    QJsonObject pred;
    pred.insert(QStringLiteral("op"), op);
    if (value.type() != QJsonValue::Undefined)
        pred.insert(QStringLiteral("value"), value);
    return pred;
}

QJsonObject wherePred(const QString& field, const QString& op,
                      const QJsonValue& value = QJsonValue(QJsonValue::Undefined))
{
    QJsonObject where;
    where.insert(field, predObj(op, value));
    return where;
}

QJsonObject condThreshold(const QString& kind, const QString& op, double valueS)
{
    QJsonObject cond;
    cond.insert(QStringLiteral("kind"), kind);
    cond.insert(QStringLiteral("op"), op);
    cond.insert(QStringLiteral("value_s"), valueS);
    return cond;
}

QJsonObject condTimeOfDay(int fromHour, int toHour)
{
    QJsonObject cond;
    cond.insert(QStringLiteral("kind"), QStringLiteral("time_of_day"));
    cond.insert(QStringLiteral("from_hour"), fromHour);
    cond.insert(QStringLiteral("to_hour"), toHour);
    return cond;
}

QJsonObject condValue(const QString& kind, const QString& value)
{
    QJsonObject cond;
    cond.insert(QStringLiteral("kind"), kind);
    cond.insert(QStringLiteral("value"), value);
    return cond;
}

// Context that satisfies the bare rule (connected, no where to match).
core::EvalContext connectedContext()
{
    core::EvalContext ctx;
    ctx.connected = true;
    return ctx;
}

int countMessages(const CapturingSink& sink, const char* needle)
{
    int hits = 0;
    for (const std::string& m : sink.messages) {
        if (m.find(needle) != std::string::npos)
            ++hits;
    }
    return hits;
}

} // namespace

class RuleEngineTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // Layer 1: schema validation (strict-in).
    void testRejectMatrix_data();
    void testRejectMatrix();
    void testCompileDefaults();
    void testCompileFullFeature();

    // Rule-set management.
    void testMaxRulesExceeded();
    void testUpsertAtomicReplace();
    void testRemoveByOwnerPrefix();
    void testCancelDefaultWarn();

    // Layer 3: evaluation kernel.
    void testCooldownWindow();
    void testOneShotLatch();
    void testEnableCycleResetsOneShot();
    void testWhereEqTypeSensitive();
    void testWhereNe();
    void testWhereIn();
    void testWhereExists();
    void testConditionIdleFor();
    void testConditionSinceLastMotion();
    void testConditionTimeOfDay();
    void testConditionModelIs();
    void testConditionPackMounted();
    void testConnectedGate();

    // Ordering + subscriptions.
    void testCompositeOrder();
    void testSubscribedEventIds();

    // Bookkeeping safety.
    void testRecordFireUnknownIdSafe();

private:
    // Compile helper usable from slots (QVERIFY works in any void member).
    void compileOk(const core::RuleEngine& engine, const QJsonObject& json,
                   core::CompiledRule* out);
    void loadOk(core::RuleEngine& engine, const QJsonObject& json,
                core::CompiledRule* out);

    std::shared_ptr<CapturingSink> m_sink;
};

void RuleEngineTest::initTestCase()
{
    m_sink = std::make_shared<CapturingSink>();
    auto logger = std::make_shared<spdlog::logger>("test_rule_engine", m_sink);
    logger->set_level(spdlog::level::trace);
    logger->flush_on(spdlog::level::trace);
    spdlog::set_default_logger(logger);
}

void RuleEngineTest::cleanupTestCase()
{
    spdlog::shutdown();
}

void RuleEngineTest::compileOk(const core::RuleEngine& engine,
                               const QJsonObject& json,
                               core::CompiledRule* out)
{
    core::RuleError err;
    const auto rule = engine.compileRule(json, &err);
    if (!rule.has_value()) {
        QVERIFY2(false, qPrintable(QStringLiteral("compile failed: %1 — %2")
                                       .arg(err.code, err.message)));
        return;
    }
    *out = *rule;
}

void RuleEngineTest::loadOk(core::RuleEngine& engine, const QJsonObject& json,
                            core::CompiledRule* out)
{
    core::RuleError err;
    const auto rule = engine.loadRule(json, &err);
    if (!rule.has_value()) {
        QVERIFY2(false, qPrintable(QStringLiteral("load failed: %1 — %2")
                                       .arg(err.code, err.message)));
        return;
    }
    *out = *rule;
}

// ── Layer 1: reject matrix (table-driven) ──────────────────────────────────

void RuleEngineTest::testRejectMatrix_data()
{
    QTest::addColumn<QJsonObject>("json");
    QTest::addColumn<QString>("expectedCode");

    auto row = [](const char* name,
                  const std::function<void(QJsonObject&)>& mutate,
                  const char* code) {
        QJsonObject json = validRuleJson();
        mutate(json);
        QTest::newRow(name) << json << QString::fromLatin1(code);
    };
    auto setTriggerEvent = [](QJsonObject& json, const QString& event) {
        QJsonObject trigger =
            json.value(QStringLiteral("trigger")).toObject();
        trigger.insert(QStringLiteral("event"), event);
        json.insert(QStringLiteral("trigger"), trigger);
    };
    auto setWherePred = [](QJsonObject& json, const QString& key,
                           const QJsonObject& pred) {
        QJsonObject trigger =
            json.value(QStringLiteral("trigger")).toObject();
        QJsonObject where = trigger.value(QStringLiteral("where")).toObject();
        where.insert(key, pred);
        trigger.insert(QStringLiteral("where"), where);
        json.insert(QStringLiteral("trigger"), trigger);
    };

    row("schema_version missing",
        [](QJsonObject& j) { j.remove(QStringLiteral("schema_version")); },
        "schema_version_missing");
    row("schema_version unknown",
        [](QJsonObject& j) {
            j.insert(QStringLiteral("schema_version"), 2);
        },
        "schema_version_unsupported");
    row("id missing",
        [](QJsonObject& j) { j.remove(QStringLiteral("id")); },
        "id_missing");
    row("id invalid chars",
        [](QJsonObject& j) {
            j.insert(QStringLiteral("id"), QStringLiteral("No Head!"));
        },
        "id_invalid");
    row("unknown top-level field",
        [](QJsonObject& j) { j.insert(QStringLiteral("scope"), 1); },
        "unknown_top_level_field");
    row("trigger.event two segments",
        [&](QJsonObject& j) {
            setTriggerEvent(j, QStringLiteral("pet.hit"));
        },
        "trigger_event_syntax");
    row("trigger.event empty segment",
        [&](QJsonObject& j) {
            setTriggerEvent(j, QStringLiteral("pet..hit"));
        },
        "trigger_event_syntax");
    row("trigger.event uppercase segment",
        [&](QJsonObject& j) {
            setTriggerEvent(j, QStringLiteral("pet.instance.HIT"));
        },
        "trigger_event_syntax");
    row("trigger.event wildcard segment",
        [&](QJsonObject& j) {
            setTriggerEvent(j, QStringLiteral("pet.*.hit"));
        },
        "trigger_event_syntax");
    row("where op unknown",
        [&](QJsonObject& j) {
            setWherePred(j, QStringLiteral("area_id"),
                         predObj(QStringLiteral("regex"),
                                 QStringLiteral("head")));
        },
        "where_op_unknown");
    row("where eq value null (illegal type)",
        [&](QJsonObject& j) {
            setWherePred(j, QStringLiteral("area_id"),
                         predObj(QStringLiteral("eq"),
                                 QJsonValue(QJsonValue::Null)));
        },
        "where_value_type");
    row("condition kind unknown",
        [](QJsonObject& j) {
            j.insert(QStringLiteral("condition"),
                     QJsonArray{condValue(QStringLiteral("moon_phase"),
                                          QStringLiteral("full"))});
        },
        "condition_kind_unknown");
    row("actions missing",
        [](QJsonObject& j) { j.remove(QStringLiteral("actions")); },
        "actions_missing");
    row("actions empty",
        [](QJsonObject& j) {
            j.insert(QStringLiteral("actions"), QJsonArray{});
        },
        "actions_empty");
    row("actions over cap (9 > 8)",
        [](QJsonObject& j) {
            QJsonObject action;
            action.insert(QStringLiteral("kind"), QStringLiteral("play_motion"));
            action.insert(QStringLiteral("group"), QStringLiteral("flick"));
            QJsonArray nine;
            for (int i = 0; i < 9; ++i)
                nine.append(action);
            j.insert(QStringLiteral("actions"), nine);
        },
        "actions_too_many");
    row("action kind unknown",
        [](QJsonObject& j) {
            QJsonObject action;
            action.insert(QStringLiteral("kind"), QStringLiteral("teleport"));
            j.insert(QStringLiteral("actions"), QJsonArray{action});
        },
        "action_kind_unknown");
    row("publish_event prefix violation",
        [](QJsonObject& j) {
            QJsonObject action;
            action.insert(QStringLiteral("kind"), QStringLiteral("publish_event"));
            action.insert(QStringLiteral("event"),
                          QStringLiteral("pet.custom.fired"));
            j.insert(QStringLiteral("actions"), QJsonArray{action});
        },
        "publish_event_prefix");
}

void RuleEngineTest::testRejectMatrix()
{
    QFETCH(QJsonObject, json);
    QFETCH(QString, expectedCode);

    core::RuleEngine engine;
    core::RuleError err;
    QVERIFY2(!engine.compileRule(json, &err).has_value(),
             "rule must be rejected");
    QCOMPARE(err.code, expectedCode);
    QVERIFY2(!err.message.isEmpty(), "error message must be populated");
    // Rejection must never throw — reaching here is the proof.
}

void RuleEngineTest::testCompileDefaults()
{
    core::RuleEngine engine;
    core::CompiledRule rule;
    compileOk(engine, validRuleJson(), &rule);

    QCOMPARE(rule.id, QStringLiteral("no-head-pat"));
    QCOMPARE(rule.triggerEvent, QStringLiteral("pet.instance.hit"));
    QVERIFY(rule.enabled);                 // default true
    QCOMPARE(rule.priority, 100);          // default 100
    QCOMPARE(rule.cooldownMs, qint64(0));  // default 0 = none
    QVERIFY(!rule.oneShot);
    QVERIFY(!rule.dryRun);
    QVERIFY(!rule.cancelDefault);
    QCOMPARE(rule.scope, core::RuleScope::Panel); // host-assigned default
    QCOMPARE(rule.where.size(), 1);               // from the fixture
    QCOMPARE(rule.conditions.size(), 0);
    QCOMPARE(rule.actions.size(), 1);
    QCOMPARE(rule.actions.at(0).kind, core::RuleActionKind::PlayMotion);
    QCOMPARE(rule.actions.at(0).group, QStringLiteral("flick"));
    QCOMPARE(rule.actions.at(0).index, 0); // default index
}

void RuleEngineTest::testCompileFullFeature()
{
    QJsonObject whereArea;
    whereArea.insert(QStringLiteral("op"), QStringLiteral("eq"));
    whereArea.insert(QStringLiteral("value"), QStringLiteral("head"));
    QJsonObject whereCount;
    whereCount.insert(QStringLiteral("op"), QStringLiteral("in"));
    whereCount.insert(QStringLiteral("value"),
                      QJsonArray{1, 2, 3});
    QJsonObject whereTag;
    whereTag.insert(QStringLiteral("op"), QStringLiteral("exists"));
    QJsonObject where;
    where.insert(QStringLiteral("area_id"), whereArea);
    where.insert(QStringLiteral("count"), whereCount);
    where.insert(QStringLiteral("tag"), whereTag);

    QJsonObject trigger;
    trigger.insert(QStringLiteral("event"), QStringLiteral("pet.instance.hit"));
    trigger.insert(QStringLiteral("where"), where);

    QJsonArray actions;
    QJsonObject a1;
    a1.insert(QStringLiteral("kind"), QStringLiteral("play_motion"));
    a1.insert(QStringLiteral("group"), QStringLiteral("flick"));
    a1.insert(QStringLiteral("index"), 2);
    actions.append(a1);
    QJsonObject a2;
    a2.insert(QStringLiteral("kind"), QStringLiteral("play_pack_audio"));
    a2.insert(QStringLiteral("pack"), QStringLiteral("packA"));
    a2.insert(QStringLiteral("group"), QStringLiteral("ooo"));
    actions.append(a2);
    QJsonObject a3;
    a3.insert(QStringLiteral("kind"), QStringLiteral("set_expression"));
    a3.insert(QStringLiteral("id"), QStringLiteral("smile"));
    actions.append(a3);
    QJsonObject a4;
    a4.insert(QStringLiteral("kind"), QStringLiteral("notify_bubble"));
    a4.insert(QStringLiteral("text"), QStringLiteral("别摸头"));
    a4.insert(QStringLiteral("duration_ms"), 1500);
    actions.append(a4);
    QJsonObject a5Payload;
    a5Payload.insert(QStringLiteral("k"), 1);
    QJsonObject a5;
    a5.insert(QStringLiteral("kind"), QStringLiteral("publish_event"));
    a5.insert(QStringLiteral("event"), QStringLiteral("pet.rules.night.fired"));
    a5.insert(QStringLiteral("payload"), a5Payload);
    actions.append(a5);

    QJsonObject ruleJson;
    ruleJson.insert(QStringLiteral("schema_version"), 1);
    ruleJson.insert(QStringLiteral("id"), QStringLiteral("night-notify"));
    ruleJson.insert(QStringLiteral("trigger"), trigger);
    ruleJson.insert(QStringLiteral("condition"),
                    QJsonArray{
                        condThreshold(QStringLiteral("idle_for"),
                                      QStringLiteral("ge"), 300),
                        condThreshold(QStringLiteral("since_last_motion"),
                                      QStringLiteral("gt"), 60.5),
                        condTimeOfDay(22, 6),
                        condValue(QStringLiteral("model_is"),
                                  QStringLiteral("Hiyori")),
                        condValue(QStringLiteral("pack_mounted"),
                                  QStringLiteral("")),
                    });
    ruleJson.insert(QStringLiteral("actions"), actions);
    ruleJson.insert(QStringLiteral("cancel_default"), true);
    ruleJson.insert(QStringLiteral("priority"), 5);
    ruleJson.insert(QStringLiteral("cooldown_ms"), 3000);
    ruleJson.insert(QStringLiteral("one_shot"), true);
    ruleJson.insert(QStringLiteral("dry_run"), true);

    core::RuleEngine engine;
    core::CompiledRule rule;
    compileOk(engine, ruleJson, &rule);

    // where — compiled to {field, op, QVariant value}
    QCOMPARE(rule.where.size(), 3);
    QCOMPARE(rule.where.at(0).field, QStringLiteral("area_id"));
    QCOMPARE(rule.where.at(0).op, core::WhereOp::Eq);
    QCOMPARE(rule.where.at(0).value.metaType().id(), qMetaTypeId<QString>());
    QCOMPARE(rule.where.at(0).value.toString(), QStringLiteral("head"));
    QCOMPARE(rule.where.at(1).op, core::WhereOp::In);
    const QVariantList inList = rule.where.at(1).value.toList();
    QCOMPARE(inList.size(), 3);
    QCOMPARE(inList.at(0).metaType().id(), qMetaTypeId<double>());
    QCOMPARE(inList.at(0).toDouble(), 1.0);
    QCOMPARE(inList.at(2).toDouble(), 3.0);
    QCOMPARE(rule.where.at(2).field, QStringLiteral("tag"));
    QCOMPARE(rule.where.at(2).op, core::WhereOp::Exists);

    // conditions — typed structs, value_s → ms
    QCOMPARE(rule.conditions.size(), 5);
    const auto* idle = std::get_if<core::IdleForCondition>(&rule.conditions.at(0));
    QVERIFY2(idle != nullptr, "condition 0 must be idle_for");
    QCOMPARE(idle->op, core::ThresholdOp::Ge);
    QCOMPARE(idle->valueMs, qint64(300000));
    const auto* since = std::get_if<core::SinceLastMotionCondition>(
        &rule.conditions.at(1));
    QVERIFY2(since != nullptr, "condition 1 must be since_last_motion");
    QCOMPARE(since->op, core::ThresholdOp::Gt);
    QCOMPARE(since->valueMs, qint64(60500)); // 60.5s fractional survives
    const auto* tod = std::get_if<core::TimeOfDayCondition>(
        &rule.conditions.at(2));
    QVERIFY2(tod != nullptr, "condition 2 must be time_of_day");
    QCOMPARE(tod->fromHour, 22);
    QCOMPARE(tod->toHour, 6);
    const auto* model = std::get_if<core::ModelIsCondition>(
        &rule.conditions.at(3));
    QVERIFY2(model != nullptr, "condition 3 must be model_is");
    QCOMPARE(model->value, QStringLiteral("Hiyori"));
    const auto* pack = std::get_if<core::PackMountedCondition>(
        &rule.conditions.at(4));
    QVERIFY2(pack != nullptr, "condition 4 must be pack_mounted");
    QVERIFY(pack->value.isEmpty()); // "" = requires nothing mounted

    // actions — kind enum + parameters
    QCOMPARE(rule.actions.size(), 5);
    QCOMPARE(rule.actions.at(0).kind, core::RuleActionKind::PlayMotion);
    QCOMPARE(rule.actions.at(0).index, 2);
    QCOMPARE(rule.actions.at(1).kind, core::RuleActionKind::PlayPackAudio);
    QCOMPARE(rule.actions.at(1).pack, QStringLiteral("packA"));
    QCOMPARE(rule.actions.at(1).group, QStringLiteral("ooo"));
    QCOMPARE(rule.actions.at(2).kind, core::RuleActionKind::SetExpression);
    QCOMPARE(rule.actions.at(2).id, QStringLiteral("smile"));
    QCOMPARE(rule.actions.at(3).kind, core::RuleActionKind::NotifyBubble);
    QCOMPARE(rule.actions.at(3).text, QStringLiteral("别摸头"));
    QCOMPARE(rule.actions.at(3).durationMs, qint64(1500));
    QCOMPARE(rule.actions.at(4).kind, core::RuleActionKind::PublishEvent);
    QCOMPARE(rule.actions.at(4).event, QStringLiteral("pet.rules.night.fired"));
    QCOMPARE(rule.actions.at(4).payload.value(QStringLiteral("k")).toInt(), 1);

    // metadata
    QVERIFY(rule.cancelDefault);
    QCOMPARE(rule.priority, 5);
    QCOMPARE(rule.cooldownMs, qint64(3000));
    QVERIFY(rule.oneShot);
    QVERIFY(rule.dryRun);
}

// ── Rule-set management ─────────────────────────────────────────────────────

void RuleEngineTest::testMaxRulesExceeded()
{
    core::RuleEngine engine;
    core::CompiledRule base;
    compileOk(engine, validRuleJson(), &base);

    for (int i = 0; i < core::kRuleMaxRules; ++i) {
        core::CompiledRule rule = base;
        rule.id = QStringLiteral("rule-%1").arg(i);
        core::RuleError err;
        QVERIFY2(engine.upsertRule(rule, &err),
                 qPrintable(QStringLiteral("upsert %1 failed: %2")
                                .arg(rule.id, err.message)));
    }
    QCOMPARE(engine.ruleCount(), core::kRuleMaxRules);

    // A NEW id past the ceiling is rejected…
    core::CompiledRule extra = base;
    extra.id = QStringLiteral("rule-extra");
    core::RuleError err;
    QVERIFY(!engine.upsertRule(extra, &err));
    QCOMPARE(err.code, QStringLiteral("max_rules_exceeded"));
    QCOMPARE(engine.ruleCount(), core::kRuleMaxRules);

    // …but replacing an EXISTING id at capacity stays legal.
    core::CompiledRule replacement = base;
    replacement.id = QStringLiteral("rule-0");
    replacement.priority = 7;
    QVERIFY(engine.upsertRule(replacement, &err));
    QCOMPARE(engine.ruleCount(), core::kRuleMaxRules);
    QCOMPARE(engine.ruleById(QStringLiteral("rule-0"))->priority, 7);
}

void RuleEngineTest::testUpsertAtomicReplace()
{
    core::RuleEngine engine;

    QJsonObject v1 = validRuleJson();
    v1.insert(QStringLiteral("cooldown_ms"), 5000);
    core::CompiledRule first;
    loadOk(engine, v1, &first);
    QCOMPARE(engine.ruleCount(), 1);

    // Fire inside v1's cooldown window.
    core::EvalContext ctx = connectedContext();
    ctx.payload.insert(QStringLiteral("area_id"), QStringLiteral("head"));
    QVERIFY(engine.shouldFire(*engine.ruleById(QStringLiteral("no-head-pat")),
                              ctx, 1000)
                .fire);
    engine.recordFire(QStringLiteral("no-head-pat"), 1000);

    // Atomic replace: same id, different content, fresh declaration order.
    QJsonObject v2 = validRuleJson();
    v2.insert(QStringLiteral("cooldown_ms"), 5000);
    v2.insert(QStringLiteral("priority"), 20);
    core::CompiledRule second;
    loadOk(engine, v2, &second);
    QCOMPARE(engine.ruleCount(), 1); // replaced, not appended
    QCOMPARE(engine.ruleById(QStringLiteral("no-head-pat"))->priority, 20);
    QVERIFY(second.declarationOrder > first.declarationOrder);

    // Same enabled cycle → fire bookkeeping SURVIVES the replace.
    QVERIFY(!engine.shouldFire(*engine.ruleById(
                                   QStringLiteral("no-head-pat")),
                               ctx, 2000)
                 .fire); // cooldown still armed from t=1000

    // Remove + reload → fresh bookkeeping.
    QVERIFY(engine.removeRule(QStringLiteral("no-head-pat")));
    QCOMPARE(engine.ruleCount(), 0);
    loadOk(engine, v1, &first);
    QVERIFY(engine.shouldFire(*engine.ruleById(QStringLiteral("no-head-pat")),
                              ctx, 2000)
                .fire);
}

void RuleEngineTest::testRemoveByOwnerPrefix()
{
    core::RuleEngine engine;
    core::CompiledRule base;
    compileOk(engine, validRuleJson(), &base);

    const QStringList ids = {
        QStringLiteral("plugin.foo.r1"),
        QStringLiteral("plugin.foo.r2"),
        QStringLiteral("panel.main"),
        QStringLiteral("plugin.bar.r3"),
    };
    for (const QString& id : ids) {
        core::CompiledRule rule = base;
        rule.id = id;
        QVERIFY(engine.upsertRule(rule));
    }

    // Arm fire state on a plugin-owned rule, then sweep the plugin's rules.
    engine.recordFire(QStringLiteral("plugin.foo.r1"), 1000);
    QCOMPARE(engine.removeRulesByOwnerPrefix(QStringLiteral("plugin.foo.")), 2);
    QCOMPARE(engine.ruleCount(), 2);
    QVERIFY(engine.ruleById(QStringLiteral("panel.main")).has_value());
    QVERIFY(engine.ruleById(QStringLiteral("plugin.bar.r3")).has_value());
    QVERIFY(!engine.ruleById(QStringLiteral("plugin.foo.r1")).has_value());

    // Fire state was swept too — re-adding the rule starts a fresh cycle.
    // (The baseline rule carries where area_id eq "head", so the context's
    // payload must satisfy it.)
    core::EvalContext headCtx = connectedContext();
    headCtx.payload.insert(QStringLiteral("area_id"), QStringLiteral("head"));
    core::CompiledRule r1 = base;
    r1.id = QStringLiteral("plugin.foo.r1");
    r1.cooldownMs = 60000;
    QVERIFY(engine.upsertRule(r1));
    QVERIFY(engine.shouldFire(*engine.ruleById(QStringLiteral("plugin.foo.r1")),
                              headCtx, 1500)
                .fire);

    // A broader prefix takes the remaining plugin rules but not panel ones.
    QCOMPARE(engine.removeRulesByOwnerPrefix(QStringLiteral("plugin.")), 2);
    QCOMPARE(engine.ruleCount(), 1);
    QCOMPARE(engine.removeRulesByOwnerPrefix(QStringLiteral("plugin.")), 0);
}

void RuleEngineTest::testCancelDefaultWarn()
{
    core::RuleEngine::KnownEventTable table;
    table.insert(QStringLiteral("pet.instance.hit"), true);   // cancellable
    table.insert(QStringLiteral("pet.renderer.shutdown"), false); // NOT cancellable
    core::RuleEngine engine(table);
    core::CompiledRule rule;
    compileOk(engine, validRuleJson(), &rule); // pet.instance.hit, no cancel_default
    QVERIFY(engine.upsertRule(rule));
    QCOMPARE(countMessages(*m_sink, "NON-cancellable"), 0);

    // Known NON-cancellable + cancel_default=true → WARN, load proceeds.
    QJsonObject nonCancellable = validRuleJson();
    QJsonObject trigger =
        nonCancellable.value(QStringLiteral("trigger")).toObject();
    trigger.insert(QStringLiteral("event"),
                   QStringLiteral("pet.renderer.shutdown"));
    nonCancellable.insert(QStringLiteral("trigger"), trigger);
    nonCancellable.insert(QStringLiteral("cancel_default"), true);
    core::CompiledRule warned;
    compileOk(engine, nonCancellable, &warned); // compiles fine
    QVERIFY(engine.upsertRule(warned));         // and loads fine
    QVERIFY(countMessages(*m_sink, "NON-cancellable") >= 1);

    // Known CANCELLABLE + cancel_default=true → no WARN.
    const int warnCount = countMessages(*m_sink, "NON-cancellable");
    QJsonObject cancellable = validRuleJson();
    cancellable.insert(QStringLiteral("cancel_default"), true);
    core::CompiledRule fine;
    compileOk(engine, cancellable, &fine);
    QVERIFY(engine.upsertRule(fine));
    QCOMPARE(countMessages(*m_sink, "NON-cancellable"), warnCount);

    // UNKNOWN event + cancel_default=true → no opinion, no WARN.
    QJsonObject unknown = validRuleJson();
    QJsonObject unknownTrigger =
        unknown.value(QStringLiteral("trigger")).toObject();
    unknownTrigger.insert(QStringLiteral("event"),
                          QStringLiteral("pet.custom.foo"));
    unknown.insert(QStringLiteral("trigger"), unknownTrigger);
    unknown.insert(QStringLiteral("cancel_default"), true);
    core::CompiledRule unknownRule;
    compileOk(engine, unknown, &unknownRule);
    QVERIFY(engine.upsertRule(unknownRule));
    QCOMPARE(countMessages(*m_sink, "NON-cancellable"), warnCount);
}

// ── Layer 3: evaluation kernel ──────────────────────────────────────────────

void RuleEngineTest::testCooldownWindow()
{
    core::RuleEngine engine;
    QJsonObject json = bareRule(QStringLiteral("cooldown-rule"));
    json.insert(QStringLiteral("cooldown_ms"), 3000);
    core::CompiledRule rule;
    loadOk(engine, json, &rule);

    const core::EvalContext ctx = connectedContext();
    const auto stored = [&engine]() {
        return *engine.ruleById(QStringLiteral("cooldown-rule"));
    };

    // Window armed by the recorded fire at t=1000.
    QVERIFY(engine.shouldFire(stored(), ctx, 1000).fire);
    engine.recordFire(QStringLiteral("cooldown-rule"), 1000);

    QVERIFY(!engine.shouldFire(stored(), ctx, 2000).fire);
    QCOMPARE(engine.shouldFire(stored(), ctx, 2000).reason,
             QStringLiteral("cooldown_active"));
    QVERIFY(!engine.shouldFire(stored(), ctx, 3999).fire);
    // Boundary: exactly cooldownMs elapsed → allowed (skip iff strictly less).
    QVERIFY(engine.shouldFire(stored(), ctx, 4000).fire);

    // A skipped probe must NOT reset the window: probe at t=2000 skips, yet
    // t=4500 still fires against the ORIGINAL t=1000 window (4500-1000=3500
    // ≥ 3000). Had the probe reset it, 4500-2000=2500 < 3000 → skip.
    core::RuleEngine engine2;
    core::CompiledRule rule2;
    loadOk(engine2, json, &rule2);
    QVERIFY(engine2.shouldFire(*engine2.ruleById(QStringLiteral("cooldown-rule")),
                               ctx, 1000)
                .fire);
    engine2.recordFire(QStringLiteral("cooldown-rule"), 1000);
    QVERIFY(!engine2.shouldFire(*engine2.ruleById(QStringLiteral("cooldown-rule")),
                                ctx, 2000)
                 .fire); // probe — skips
    QVERIFY(engine2.shouldFire(*engine2.ruleById(QStringLiteral("cooldown-rule")),
                               ctx, 4500)
                .fire); // window NOT reset by the probe
}

void RuleEngineTest::testOneShotLatch()
{
    core::RuleEngine engine;
    QJsonObject json = bareRule(QStringLiteral("one-shot-rule"));
    json.insert(QStringLiteral("one_shot"), true);
    core::CompiledRule rule;
    loadOk(engine, json, &rule);
    const core::EvalContext ctx = connectedContext();
    const auto stored = [&engine]() {
        return *engine.ruleById(QStringLiteral("one-shot-rule"));
    };

    QVERIFY(engine.shouldFire(stored(), ctx, 100).fire);
    engine.recordFire(QStringLiteral("one-shot-rule"), 100);
    QVERIFY(!engine.shouldFire(stored(), ctx, 200).fire);
    QCOMPARE(engine.shouldFire(stored(), ctx, 200).reason,
             QStringLiteral("one_shot_fired"));

    // Single-rule cycle reset re-arms the latch.
    engine.resetOneShotCycle(QStringLiteral("one-shot-rule"));
    QVERIFY(engine.shouldFire(stored(), ctx, 300).fire);
    engine.recordFire(QStringLiteral("one-shot-rule"), 300);
    QVERIFY(!engine.shouldFire(stored(), ctx, 400).fire);

    // Global cycle reset re-arms every latch.
    engine.resetOneShotCycles();
    QVERIFY(engine.shouldFire(stored(), ctx, 500).fire);
}

void RuleEngineTest::testEnableCycleResetsOneShot()
{
    core::RuleEngine engine;
    QJsonObject json = bareRule(QStringLiteral("cycle-rule"));
    json.insert(QStringLiteral("one_shot"), true);
    core::CompiledRule rule;
    loadOk(engine, json, &rule);
    const core::EvalContext ctx = connectedContext();

    QVERIFY(engine.shouldFire(*engine.ruleById(QStringLiteral("cycle-rule")),
                              ctx, 100)
                .fire);
    engine.recordFire(QStringLiteral("cycle-rule"), 100);
    QVERIFY(!engine.shouldFire(*engine.ruleById(QStringLiteral("cycle-rule")),
                               ctx, 200)
                 .fire); // latched

    // Disable: skips for a different reason now.
    core::CompiledRule disabled = *engine.ruleById(QStringLiteral("cycle-rule"));
    disabled.enabled = false;
    QVERIFY(engine.upsertRule(disabled));
    QCOMPARE(engine.shouldFire(*engine.ruleById(QStringLiteral("cycle-rule")),
                               ctx, 300)
                 .reason,
             QStringLiteral("disabled"));

    // Re-enable: NEW enabled cycle → latch cleared.
    core::CompiledRule reEnabled = disabled;
    reEnabled.enabled = true;
    QVERIFY(engine.upsertRule(reEnabled));
    QVERIFY(engine.shouldFire(*engine.ruleById(QStringLiteral("cycle-rule")),
                              ctx, 400)
                .fire);
}

void RuleEngineTest::testWhereEqTypeSensitive()
{
    core::RuleEngine engine;

    // String eq: exact match only.
    core::CompiledRule areaRule;
    compileOk(engine, ruleWithWhere(wherePred(QStringLiteral("area_id"),
                                              QStringLiteral("eq"),
                                              QStringLiteral("head"))),
              &areaRule);
    core::EvalContext ctx = connectedContext();
    ctx.payload.insert(QStringLiteral("area_id"), QStringLiteral("head"));
    QVERIFY(engine.shouldFire(areaRule, ctx, 1000).fire);
    ctx.payload.insert(QStringLiteral("area_id"), QStringLiteral("body"));
    QVERIFY(!engine.shouldFire(areaRule, ctx, 1000).fire);

    // Number eq with a type-sensitive counterexample: payload "5" (string)
    // never matches the compiled number 5 — and vice versa.
    core::CompiledRule countRule;
    compileOk(engine, ruleWithWhere(wherePred(QStringLiteral("count"),
                                              QStringLiteral("eq"), 5)),
              &countRule);
    ctx.payload.remove(QStringLiteral("area_id"));
    ctx.payload.insert(QStringLiteral("count"), 5);
    QVERIFY(engine.shouldFire(countRule, ctx, 1000).fire);
    ctx.payload.insert(QStringLiteral("count"), QStringLiteral("5"));
    QVERIFY(!engine.shouldFire(countRule, ctx, 1000).fire);
    QCOMPARE(engine.shouldFire(countRule, ctx, 1000).reason,
             QStringLiteral("where_predicate_failed"));

    // Bool eq is bool-typed, not truthy-typed.
    core::CompiledRule boolRule;
    compileOk(engine, ruleWithWhere(wherePred(QStringLiteral("flag"),
                                              QStringLiteral("eq"), true)),
              &boolRule);
    ctx.payload.insert(QStringLiteral("flag"), true);
    QVERIFY(engine.shouldFire(boolRule, ctx, 1000).fire);
    ctx.payload.insert(QStringLiteral("flag"), 1); // number 1 ≠ bool true
    QVERIFY(!engine.shouldFire(boolRule, ctx, 1000).fire);
}

void RuleEngineTest::testWhereNe()
{
    core::RuleEngine engine;
    core::CompiledRule rule;
    compileOk(engine, ruleWithWhere(wherePred(QStringLiteral("area_id"),
                                              QStringLiteral("ne"),
                                              QStringLiteral("head"))),
              &rule);
    core::EvalContext ctx = connectedContext();

    ctx.payload.insert(QStringLiteral("area_id"), QStringLiteral("body"));
    QVERIFY(engine.shouldFire(rule, ctx, 1000).fire); // different value → ne true
    ctx.payload.insert(QStringLiteral("area_id"), QStringLiteral("head"));
    QVERIFY(!engine.shouldFire(rule, ctx, 1000).fire); // equal → ne false

    // Documented semantics: ne ≡ !eq, so a TYPE MISMATCH satisfies ne
    // (payload number 7 vs compiled string "head").
    ctx.payload.insert(QStringLiteral("area_id"), 7);
    QVERIFY(engine.shouldFire(rule, ctx, 1000).fire);
}

void RuleEngineTest::testWhereIn()
{
    core::RuleEngine engine;
    core::CompiledRule rule;
    compileOk(engine,
              ruleWithWhere(wherePred(QStringLiteral("area_id"),
                                      QStringLiteral("in"),
                                      QJsonArray{QStringLiteral("head"),
                                                 QStringLiteral("hair")})),
              &rule);
    core::EvalContext ctx = connectedContext();

    ctx.payload.insert(QStringLiteral("area_id"), QStringLiteral("hair"));
    QVERIFY(engine.shouldFire(rule, ctx, 1000).fire);
    ctx.payload.insert(QStringLiteral("area_id"), QStringLiteral("body"));
    QVERIFY(!engine.shouldFire(rule, ctx, 1000).fire);

    // Type-sensitive: string "1" is not in the number list [1, 2].
    core::CompiledRule numRule;
    compileOk(engine,
              ruleWithWhere(wherePred(QStringLiteral("count"),
                                      QStringLiteral("in"),
                                      QJsonArray{1, 2})),
              &numRule);
    ctx.payload.remove(QStringLiteral("area_id"));
    ctx.payload.insert(QStringLiteral("count"), 2);
    QVERIFY(engine.shouldFire(numRule, ctx, 1000).fire);
    ctx.payload.insert(QStringLiteral("count"), QStringLiteral("2"));
    QVERIFY(!engine.shouldFire(numRule, ctx, 1000).fire);
}

void RuleEngineTest::testWhereExists()
{
    core::RuleEngine engine;
    core::CompiledRule rule;
    compileOk(engine,
              ruleWithWhere(wherePred(QStringLiteral("tag"),
                                      QStringLiteral("exists"))),
              &rule);
    core::EvalContext ctx = connectedContext();

    ctx.payload.insert(QStringLiteral("tag"), QStringLiteral("x"));
    QVERIFY(engine.shouldFire(rule, ctx, 1000).fire);
    ctx.payload.remove(QStringLiteral("tag"));
    QVERIFY(!engine.shouldFire(rule, ctx, 1000).fire);
    // Explicit JSON null counts as absent (documented convention).
    ctx.payload.insert(QStringLiteral("tag"), QJsonValue(QJsonValue::Null));
    QVERIFY(!engine.shouldFire(rule, ctx, 1000).fire);
}

void RuleEngineTest::testConditionIdleFor()
{
    core::RuleEngine engine;
    core::CompiledRule rule;
    compileOk(engine,
              ruleWithCondition(condThreshold(QStringLiteral("idle_for"),
                                              QStringLiteral("gt"), 300)),
              &rule);
    core::EvalContext ctx = connectedContext();
    ctx.lastActivityMs = 1000;

    QVERIFY(!engine.shouldFire(rule, ctx, 301000).fire); // exactly 300s: gt fails
    QVERIFY(engine.shouldFire(rule, ctx, 301001).fire);  // 300s + 1ms: gt passes

    core::CompiledRule geRule;
    compileOk(engine,
              ruleWithCondition(condThreshold(QStringLiteral("idle_for"),
                                              QStringLiteral("ge"), 300)),
              &geRule);
    QVERIFY(engine.shouldFire(geRule, ctx, 301000).fire); // boundary inclusive

    // Fail-closed: unknown lastActivityMs (0).
    core::EvalContext unknown = connectedContext();
    QVERIFY(!engine.shouldFire(geRule, unknown, 999999).fire);
    QCOMPARE(engine.shouldFire(geRule, unknown, 999999).reason,
             QStringLiteral("condition_failed"));
}

void RuleEngineTest::testConditionSinceLastMotion()
{
    core::RuleEngine engine;
    core::CompiledRule rule;
    compileOk(engine,
              ruleWithCondition(condThreshold(QStringLiteral("since_last_motion"),
                                              QStringLiteral("gt"), 60)),
              &rule);
    core::EvalContext ctx = connectedContext();
    ctx.lastMotionMs = 1000;

    QVERIFY(!engine.shouldFire(rule, ctx, 61000).fire);
    QVERIFY(engine.shouldFire(rule, ctx, 61001).fire);

    // Fail-closed: unknown lastMotionMs (0).
    core::EvalContext unknown = connectedContext();
    QVERIFY(!engine.shouldFire(rule, unknown, 999999).fire);
}

void RuleEngineTest::testConditionTimeOfDay()
{
    core::RuleEngine engine;
    core::CompiledRule night;
    compileOk(engine, ruleWithCondition(condTimeOfDay(22, 6)), &night);
    core::EvalContext ctx = connectedContext();

    // Wrap-around 22→6, inclusive on both ends.
    for (int hour : {22, 23, 0, 2, 6}) {
        ctx.currentHour = hour;
        QVERIFY2(engine.shouldFire(night, ctx, 1000).fire,
                 qPrintable(QStringLiteral("hour %1 must be inside 22→6")
                                .arg(hour)));
    }
    for (int hour : {7, 12, 21}) {
        ctx.currentHour = hour;
        QVERIFY2(!engine.shouldFire(night, ctx, 1000).fire,
                 qPrintable(QStringLiteral("hour %1 must be outside 22→6")
                                .arg(hour)));
    }

    // Non-wrapped 9→17.
    core::CompiledRule day;
    compileOk(engine, ruleWithCondition(condTimeOfDay(9, 17)), &day);
    for (int hour : {9, 12, 17}) {
        ctx.currentHour = hour;
        QVERIFY2(engine.shouldFire(day, ctx, 1000).fire,
                 qPrintable(QStringLiteral("hour %1 must be inside 9→17")
                                .arg(hour)));
    }
    for (int hour : {8, 18}) {
        ctx.currentHour = hour;
        QVERIFY2(!engine.shouldFire(day, ctx, 1000).fire,
                 qPrintable(QStringLiteral("hour %1 must be outside 9→17")
                                .arg(hour)));
    }

    // from == to degenerates to the single hour [h, h].
    core::CompiledRule single;
    compileOk(engine, ruleWithCondition(condTimeOfDay(5, 5)), &single);
    ctx.currentHour = 5;
    QVERIFY(engine.shouldFire(single, ctx, 1000).fire);
    ctx.currentHour = 6;
    QVERIFY(!engine.shouldFire(single, ctx, 1000).fire);

    // Fail-closed: currentHour unknown (-1) or out of range (24).
    ctx.currentHour = -1;
    QVERIFY(!engine.shouldFire(night, ctx, 1000).fire);
    ctx.currentHour = 24;
    QVERIFY(!engine.shouldFire(night, ctx, 1000).fire);
}

void RuleEngineTest::testConditionModelIs()
{
    core::RuleEngine engine;
    core::CompiledRule rule;
    compileOk(engine,
              ruleWithCondition(condValue(QStringLiteral("model_is"),
                                          QStringLiteral("Hiyori"))),
              &rule);
    core::EvalContext ctx = connectedContext();

    ctx.modelName = QStringLiteral("Hiyori");
    QVERIFY(engine.shouldFire(rule, ctx, 1000).fire);
    ctx.modelName = QStringLiteral("hiyori"); // case-sensitive
    QVERIFY(!engine.shouldFire(rule, ctx, 1000).fire);

    // Fail-closed: snapshot missing the model name.
    ctx.modelName = QString();
    QVERIFY(!engine.shouldFire(rule, ctx, 1000).fire);
}

void RuleEngineTest::testConditionPackMounted()
{
    core::RuleEngine engine;
    core::CompiledRule rule;
    compileOk(engine,
              ruleWithCondition(condValue(QStringLiteral("pack_mounted"),
                                          QStringLiteral("packA"))),
              &rule);
    core::EvalContext ctx = connectedContext();

    ctx.mountedPackId = QStringLiteral("packA");
    QVERIFY(engine.shouldFire(rule, ctx, 1000).fire);
    ctx.mountedPackId = QStringLiteral("packB");
    QVERIFY(!engine.shouldFire(rule, ctx, 1000).fire);
    ctx.mountedPackId = QString(); // nothing mounted
    QVERIFY(!engine.shouldFire(rule, ctx, 1000).fire);

    // "" value = requires NOTHING mounted (and an empty pack id is a legal
    // "unmounted" state, not an unknown — no fail-closed here).
    core::CompiledRule unmounted;
    compileOk(engine,
              ruleWithCondition(condValue(QStringLiteral("pack_mounted"),
                                          QStringLiteral(""))),
              &unmounted);
    QVERIFY(engine.shouldFire(unmounted, ctx, 1000).fire);
    ctx.mountedPackId = QStringLiteral("packA");
    QVERIFY(!engine.shouldFire(unmounted, ctx, 1000).fire);
}

void RuleEngineTest::testConnectedGate()
{
    core::RuleEngine engine;
    core::CompiledRule rule;
    compileOk(engine, bareRule(), &rule);

    core::EvalContext offline = connectedContext();
    offline.connected = false;
    QCOMPARE(engine.shouldFire(rule, offline, 1000).reason,
             QStringLiteral("not_connected"));
    QVERIFY(!engine.shouldFire(rule, offline, 1000).fire);

    core::EvalContext online = connectedContext();
    QVERIFY(engine.shouldFire(rule, online, 1000).fire);
}

// ── Ordering + subscriptions ────────────────────────────────────────────────

void RuleEngineTest::testCompositeOrder()
{
    core::RuleEngine engine;
    auto declare = [&engine, this](const QString& id, int priority,
                                   core::RuleScope scope) {
        QJsonObject json = bareRule(id);
        json.insert(QStringLiteral("priority"), priority);
        core::CompiledRule rule;
        compileOk(engine, json, &rule);
        rule.scope = scope; // host-assigned, never from JSON
        QVERIFY(engine.upsertRule(rule));
    };

    // Deliberately declare in a scrambled order.
    declare(QStringLiteral("plugin-first"), 100, core::RuleScope::Plugin);
    declare(QStringLiteral("inst"), 100, core::RuleScope::Instance);
    declare(QStringLiteral("panel-early"), 100, core::RuleScope::Panel);
    declare(QStringLiteral("urgent-plugin"), 50, core::RuleScope::Plugin);
    declare(QStringLiteral("panel-late"), 100, core::RuleScope::Panel);

    const QList<core::CompiledRule> ordered = engine.orderedRules();
    QCOMPARE(ordered.size(), 5);
    QStringList ids;
    for (const core::CompiledRule& rule : ordered)
        ids.append(rule.id);
    // priority asc → scope weight asc (Instance < Panel < Plugin) →
    // declaration order asc within the same (priority, scope) bucket.
    QCOMPARE(ids.join(QLatin1Char(',')),
             QStringLiteral("urgent-plugin,inst,panel-early,panel-late,"
                            "plugin-first"));
}

void RuleEngineTest::testSubscribedEventIds()
{
    core::RuleEngine engine;
    auto load = [&engine, this](const QString& id, const QString& event,
                                bool enabled) {
        QJsonObject json = bareRule(id);
        QJsonObject trigger =
            json.value(QStringLiteral("trigger")).toObject();
        trigger.insert(QStringLiteral("event"), event);
        json.insert(QStringLiteral("trigger"), trigger);
        json.insert(QStringLiteral("enabled"), enabled);
        core::CompiledRule rule;
        loadOk(engine, json, &rule);
    };

    load(QStringLiteral("r1"), QStringLiteral("pet.instance.hit"), true);
    load(QStringLiteral("r2"), QStringLiteral("pet.renderer.shutdown"),
         false); // disabled → NOT subscribed
    load(QStringLiteral("r3"), QStringLiteral("pet.model.loaded"), true);

    QSet<QString> expected{QStringLiteral("pet.instance.hit"),
                           QStringLiteral("pet.model.loaded")};
    QCOMPARE(engine.subscribedEventIds(), expected);

    // Upsert adds a new event id…
    load(QStringLiteral("r4"), QStringLiteral("pet.audio.ended"), true);
    expected.insert(QStringLiteral("pet.audio.ended"));
    QCOMPARE(engine.subscribedEventIds(), expected);

    // …disable drops an id…
    load(QStringLiteral("r1"), QStringLiteral("pet.instance.hit"), false);
    expected.remove(QStringLiteral("pet.instance.hit"));
    QCOMPARE(engine.subscribedEventIds(), expected);

    // …and remove drops it too.
    QVERIFY(engine.removeRule(QStringLiteral("r3")));
    expected.remove(QStringLiteral("pet.model.loaded"));
    QCOMPARE(engine.subscribedEventIds(), expected);
}

// ── Bookkeeping safety ──────────────────────────────────────────────────────

void RuleEngineTest::testRecordFireUnknownIdSafe()
{
    core::RuleEngine engine;
    core::CompiledRule rule;
    loadOk(engine, validRuleJson(), &rule);
    QCOMPARE(engine.ruleCount(), 1);

    // Unknown id: DEBUG no-op, never a crash, never a phantom rule.
    engine.recordFire(QStringLiteral("ghost"), 12345);
    engine.recordFire(QString(), 0);
    QCOMPARE(engine.ruleCount(), 1);
    QVERIFY(!engine.ruleById(QStringLiteral("ghost")).has_value());

    // The real rule's state is untouched by the ghost writes. (The baseline
    // rule has where area_id eq "head" — satisfy it.)
    core::EvalContext ctx = connectedContext();
    ctx.payload.insert(QStringLiteral("area_id"), QStringLiteral("head"));
    QVERIFY(engine.shouldFire(*engine.ruleById(QStringLiteral("no-head-pat")),
                              ctx, 1000)
                .fire);
}

QTEST_APPLESS_MAIN(RuleEngineTest)
#include "RuleEngineTest.moc"
