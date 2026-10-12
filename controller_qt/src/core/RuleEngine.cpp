#include "core/RuleEngine.hpp"

#include <QJsonArray>
#include <QJsonValue>

#include <algorithm>
#include <cmath>

// spdlog MUST be included before logging/Logging.hpp (same rationale as
// ModelScanner.cpp / PathResolve.cpp).
#include <spdlog/spdlog.h>
#include "logging/Logging.hpp"

// RuleEngine (S8d) — implementation. Three layers, all pure logic:
//
//   1. compileRule  — strict schema validation → structured RuleError
//   2. (product)    — CompiledRule, zero JSON key lookups at eval time
//   3. shouldFire   — cooldown / one-shot / where / condition kernel
//
// ── Rejection matrix (stable error codes) ──────────────────────────────────
// Every row = load-time reject (strict-in). Codes are stable identifiers the
// S8e loader surfaces verbatim; messages carry the specifics.
//
//   code                        | trigger
//   ----------------------------+------------------------------------------
//   schema_version_missing      | "schema_version" absent or not a number
//   schema_version_unsupported  | value != 1 (incl. non-integral numbers)
//   id_missing                  | "id" absent or not a string
//   id_invalid                  | id fails the dot-segmented [a-z0-9_-] form
//   unknown_top_level_field     | top-level key outside the closed set
//   field_type                  | enabled/cancel_default/one_shot/dry_run not
//                               | bool; priority/cooldown_ms not an integral
//                               | number; cooldown_ms negative
//   trigger_missing             | "trigger" absent or not an object
//   trigger_unknown_field       | key inside "trigger" outside {event, where}
//   trigger_event_missing       | "trigger.event" absent or not a string
//   trigger_event_syntax        | event id fails the syntax check (≥3
//                               | segments, non-empty, [a-z0-9_], no
//                               | wildcards)
//   where_type                  | "trigger.where" not an object
//   where_predicate_type        | a where value not an object
//   where_unknown_field         | key inside a predicate outside {op, value}
//   where_op_missing            | predicate "op" absent or not a string
//   where_op_unknown            | op outside {eq, ne, in, exists}
//   where_value_type            | eq/ne value not string|number|bool; in
//                               | value not an array of those; null anywhere
//   where_value_unexpected      | "value" present on an exists predicate
//   condition_type              | "condition" not an array
//   condition_item_type         | array element not an object
//   condition_kind_unknown      | kind outside the closed 5-kind enum
//   condition_unknown_field     | key outside the kind's closed field set
//   condition_field_invalid     | op outside {gt, ge}; value_s missing /
//                               | negative; from_hour/to_hour missing /
//                               | non-integral / outside 0-23; model value
//                               | missing / empty / not a string
//   actions_missing             | "actions" absent or not an array
//   actions_empty               | empty array (or a CompiledRule with none)
//   actions_too_many            | more than kRuleMaxActionsPerRule (8)
//   action_item_type            | array element not an object
//   action_kind_unknown         | kind outside the closed 5-kind enum
//   action_unknown_field        | key outside the kind's closed field set
//   action_field_invalid        | required field missing / mistyped / empty
//                               | (group, pack, id, text, event); index /
//                               | duration_ms non-integral or negative
//   publish_event_prefix       | "event" lacks the "pet.rules." prefix
//   publish_event_syntax        | "event" fails the event-id syntax check
//   max_rules_exceeded          | upsert of a NEW id at the 200-rule ceiling
//
// Evaluation-time unknowns never reject — they fail CLOSED (condition false,
// predicate false) with a DEBUG log (lenient-out).

namespace core {

namespace {

// ── Small JSON helpers (never throw) ────────────────────────────────────────

bool isNumber(const QJsonValue& v) { return v.isDouble(); }

// JSON has no integer type — require an exactly-representable integral double.
bool isIntegralNumber(const QJsonValue& v)
{
    return v.isDouble() && v.toDouble() == std::floor(v.toDouble());
}

bool isNonEmptyString(const QJsonValue& v)
{
    return v.isString() && !v.toString().isEmpty();
}

// The three scalar types legal in where values / in-lists.
bool isWhereScalar(const QJsonValue& v)
{
    return v.isString() || v.isBool() || v.isDouble();
}

// ── Where matching (type-sensitive) ─────────────────────────────────────────

// Type-sensitive equality between a payload JSON value and a compiled scalar
// QVariant. A JSON string NEVER equals a JSON number/bool and vice versa —
// "5" != 5. Anything else (arrays/objects/null/undefined on the payload side,
// unexpected variant types) is false.
bool jsonEqualsVariant(const QJsonValue& json, const QVariant& scalar)
{
    switch (scalar.typeId()) {
    case QMetaType::QString:
        return json.isString() && json.toString() == scalar.toString();
    case QMetaType::Bool:
        return json.isBool() && json.toBool() == scalar.toBool();
    case QMetaType::Double:
        return json.isDouble() && json.toDouble() == scalar.toDouble();
    default:
        return false;
    }
}

bool matchPredicate(const WherePredicate& pred, const QJsonObject& payload)
{
    const QJsonValue actual = payload.value(pred.field);
    switch (pred.op) {
    case WhereOp::Exists:
        // Present AND non-null — explicit JSON null is treated as absent
        // (same convention as the stats_state gpu/vram null sentinels).
        return !actual.isUndefined() && !actual.isNull();
    case WhereOp::Eq:
        return jsonEqualsVariant(actual, pred.value);
    case WhereOp::Ne:
        return !jsonEqualsVariant(actual, pred.value);
    case WhereOp::In: {
        const QVariantList list = pred.value.toList();
        for (const QVariant& item : list) {
            if (jsonEqualsVariant(actual, item))
                return true;
        }
        return false;
    }
    }
    return false; // unreachable — op is a closed enum
}

// ── Condition matching (fail-closed on unknown context) ─────────────────────

struct ConditionVisitor {
    const QString& ruleId;
    const EvalContext& ctx;
    qint64 nowMs;

    bool failClosed(const char* kind, const char* why) const
    {
        LOG_DEBUG("RuleEngine: condition '{}' unevaluable for rule \"{}\" "
                  "({}), failing closed",
                  kind, ruleId.toStdString(), why);
        return false;
    }

    bool operator()(const IdleForCondition& c) const
    {
        if (ctx.lastActivityMs <= 0)
            return failClosed("idle_for", "lastActivityMs unknown");
        const qint64 idleMs = nowMs - ctx.lastActivityMs;
        return c.op == ThresholdOp::Gt ? idleMs > c.valueMs
                                       : idleMs >= c.valueMs;
    }

    bool operator()(const SinceLastMotionCondition& c) const
    {
        if (ctx.lastMotionMs <= 0)
            return failClosed("since_last_motion", "lastMotionMs unknown");
        const qint64 sinceMs = nowMs - ctx.lastMotionMs;
        return c.op == ThresholdOp::Gt ? sinceMs > c.valueMs
                                       : sinceMs >= c.valueMs;
    }

    bool operator()(const TimeOfDayCondition& c) const
    {
        if (ctx.currentHour < 0 || ctx.currentHour > 23)
            return failClosed("time_of_day", "currentHour unknown");
        const int h = ctx.currentHour;
        // Inclusive on both ends; from > to is the legal wrap (22→6).
        return c.fromHour <= c.toHour
                   ? (h >= c.fromHour && h <= c.toHour)
                   : (h >= c.fromHour || h <= c.toHour);
    }

    bool operator()(const ModelIsCondition& c) const
    {
        if (ctx.modelName.isEmpty())
            return failClosed("model_is", "modelName unknown");
        return ctx.modelName == c.value;
    }

    bool operator()(const PackMountedCondition& c) const
    {
        // Empty mountedPackId is a legal "nothing mounted" state, NOT an
        // unknown — no fail-closed path here. "" value = requires
        // unmounted.
        return c.value.isEmpty() ? ctx.mountedPackId.isEmpty()
                                 : ctx.mountedPackId == c.value;
    }
};

bool matchCondition(const QString& ruleId, const RuleCondition& cond,
                    const EvalContext& ctx, qint64 nowMs)
{
    return std::visit(ConditionVisitor{ruleId, ctx, nowMs}, cond);
}

// ── Compile-time field-set helpers ─────────────────────────────────────────

// True iff every key of `obj` is in the closed `allowed` set.
bool keysWithin(const QJsonObject& obj, const QStringList& allowed,
                QString* offending = nullptr)
{
    const QStringList keys = obj.keys();
    for (const QString& key : keys) {
        if (!allowed.contains(key)) {
            if (offending)
                *offending = key;
            return false;
        }
    }
    return true;
}

} // namespace

// ── Syntax validators ───────────────────────────────────────────────────────

bool isValidEventIdSyntax(const QString& eventId)
{
    if (eventId.isEmpty())
        return false;
    const QStringList segments = eventId.split(QLatin1Char('.'));
    if (segments.size() < 3)
        return false;
    for (const QString& seg : segments) {
        if (seg.isEmpty())
            return false;
        for (const QChar ch : seg) {
            const ushort u = ch.unicode();
            const bool legal = (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9')
                               || u == '_';
            if (!legal)
                return false; // rejects '*', '?', uppercase, '-', non-ASCII…
        }
    }
    return true;
}

bool isValidRuleIdSyntax(const QString& id)
{
    if (id.isEmpty())
        return false;
    // Dot-segmented: every segment must be a non-empty [a-z0-9_-] run (the
    // schema's [a-z0-9_-]+ applies PER SEGMENT — S8f owner-prefixed ids like
    // "plugin.foo.r1" are legal, "No Head!", "a..b" and ".a" are not).
    const QStringList segments = id.split(QLatin1Char('.'));
    for (const QString& seg : segments) {
        if (seg.isEmpty())
            return false;
        for (const QChar ch : seg) {
            const ushort u = ch.unicode();
            const bool legal = (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9')
                               || u == '_' || u == '-';
            if (!legal)
                return false;
        }
    }
    return true;
}

// ── Composite ordering ──────────────────────────────────────────────────────

bool ruleCompositeLess(const CompiledRule& a, const CompiledRule& b)
{
    if (a.priority != b.priority)
        return a.priority < b.priority;
    const int wa = ruleScopeWeight(a.scope);
    const int wb = ruleScopeWeight(b.scope);
    if (wa != wb)
        return wa < wb;
    return a.declarationOrder < b.declarationOrder;
}

// ── RuleEngine ──────────────────────────────────────────────────────────────

RuleEngine::RuleEngine(const KnownEventTable& knownEvents)
    : m_knownEvents(knownEvents)
{
}

void RuleEngine::setKnownEventTable(const KnownEventTable& table)
{
    m_knownEvents = table;
}

std::optional<CompiledRule> RuleEngine::compileRule(const QJsonObject& json,
                                                    RuleError* error) const
{
    RuleError sink;
    RuleError* err = error ? error : &sink;

    // Best-effort id for error attribution even when the id itself is broken.
    const QJsonValue idValue = json.value(QStringLiteral("id"));
    const QString bestEffortId =
        idValue.isString() ? idValue.toString() : QString();

    auto fail = [&](const QString& code, const QString& message)
        -> std::optional<CompiledRule> {
        err->ruleId = bestEffortId;
        err->code = code;
        err->message = message;
        LOG_DEBUG("RuleEngine: rejected rule \"{}\": {} ({})",
                  bestEffortId.toStdString(), message.toStdString(),
                  code.toStdString());
        return std::nullopt;
    };

    // 1. schema_version — required, only 1 is legal.
    const QJsonValue version = json.value(QStringLiteral("schema_version"));
    if (version.isUndefined())
        return fail(QStringLiteral("schema_version_missing"),
                    QStringLiteral("'schema_version' is required"));
    if (!isIntegralNumber(version)
        || static_cast<int>(version.toDouble()) != kRuleSchemaVersion)
        return fail(QStringLiteral("schema_version_unsupported"),
                    QStringLiteral("unsupported schema_version (only %1 is "
                                   "legal)")
                        .arg(kRuleSchemaVersion));

    // 2. id — required, [a-z0-9_-]+.
    if (idValue.isUndefined())
        return fail(QStringLiteral("id_missing"),
                    QStringLiteral("'id' is required"));
    if (!idValue.isString())
        return fail(QStringLiteral("id_missing"),
                    QStringLiteral("'id' must be a string"));
    const QString id = idValue.toString();
    if (!isValidRuleIdSyntax(id))
        return fail(QStringLiteral("id_invalid"),
                    QStringLiteral("id '%1' fails the dot-segmented "
                                   "[a-z0-9_-] form")
                        .arg(id));

    // 3. Unknown top-level fields — strict-in.
    QString offending;
    if (!keysWithin(json,
                    {QStringLiteral("schema_version"), QStringLiteral("id"),
                     QStringLiteral("enabled"), QStringLiteral("trigger"),
                     QStringLiteral("condition"), QStringLiteral("actions"),
                     QStringLiteral("cancel_default"),
                     QStringLiteral("priority"),
                     QStringLiteral("cooldown_ms"),
                     QStringLiteral("one_shot"), QStringLiteral("dry_run")},
                    &offending))
        return fail(QStringLiteral("unknown_top_level_field"),
                    QStringLiteral("unknown top-level field '%1'")
                        .arg(offending));

    CompiledRule rule;
    rule.id = id;

    // 4. Scalar meta fields (defaults: enabled=true, priority=100,
    //    cooldown_ms=0, one_shot=false, dry_run=false, cancel_default=false).
    const QJsonValue enabled = json.value(QStringLiteral("enabled"));
    if (!enabled.isUndefined() && !enabled.isBool())
        return fail(QStringLiteral("field_type"),
                    QStringLiteral("'enabled' must be a boolean"));
    rule.enabled = enabled.toBool(true);

    const QJsonValue priority = json.value(QStringLiteral("priority"));
    if (!priority.isUndefined()) {
        if (!isIntegralNumber(priority))
            return fail(QStringLiteral("field_type"),
                        QStringLiteral("'priority' must be an integer"));
        rule.priority = static_cast<int>(priority.toDouble());
    }

    const QJsonValue cooldown = json.value(
        QStringLiteral("cooldown_ms"));
    if (!cooldown.isUndefined()) {
        if (!isIntegralNumber(cooldown) || cooldown.toDouble() < 0.0)
            return fail(QStringLiteral("field_type"),
                        QStringLiteral("'cooldown_ms' must be a non-negative "
                                       "integer"));
        rule.cooldownMs = static_cast<qint64>(cooldown.toDouble());
    }

    for (const char* flagKey : {"cancel_default", "one_shot", "dry_run"}) {
        const QJsonValue flag = json.value(QLatin1String(flagKey));
        if (!flag.isUndefined() && !flag.isBool())
            return fail(QStringLiteral("field_type"),
                        QStringLiteral("'%1' must be a boolean")
                            .arg(QLatin1String(flagKey)));
    }
    rule.cancelDefault = json.value(
        QStringLiteral("cancel_default")).toBool(false);
    rule.oneShot = json.value(QStringLiteral("one_shot")).toBool(false);
    rule.dryRun = json.value(QStringLiteral("dry_run")).toBool(false);

    // 5. trigger — required object {event, where?}.
    const QJsonValue trigger = json.value(QStringLiteral("trigger"));
    if (trigger.isUndefined())
        return fail(QStringLiteral("trigger_missing"),
                    QStringLiteral("'trigger' is required"));
    if (!trigger.isObject())
        return fail(QStringLiteral("trigger_missing"),
                    QStringLiteral("'trigger' must be an object"));
    const QJsonObject triggerObj = trigger.toObject();
    if (!keysWithin(triggerObj,
                    {QStringLiteral("event"), QStringLiteral("where")},
                    &offending))
        return fail(QStringLiteral("trigger_unknown_field"),
                    QStringLiteral("unknown field '%1' inside 'trigger'")
                        .arg(offending));

    const QJsonValue event = triggerObj.value(QStringLiteral("event"));
    if (event.isUndefined())
        return fail(QStringLiteral("trigger_event_missing"),
                    QStringLiteral("'trigger.event' is required"));
    if (!event.isString())
        return fail(QStringLiteral("trigger_event_missing"),
                    QStringLiteral("'trigger.event' must be a string"));
    rule.triggerEvent = event.toString();
    if (!isValidEventIdSyntax(rule.triggerEvent))
        return fail(QStringLiteral("trigger_event_syntax"),
                    QStringLiteral("trigger.event '%1' fails event-id syntax "
                                   "(≥3 dot segments, each non-empty "
                                   "[a-z0-9_], no wildcards)")
                        .arg(rule.triggerEvent));

    // 5b. trigger.where — optional object {field: {op, value?}}.
    const QJsonValue where = triggerObj.value(QStringLiteral("where"));
    if (!where.isUndefined()) {
        if (!where.isObject())
            return fail(QStringLiteral("where_type"),
                        QStringLiteral("'trigger.where' must be an object"));
        const QJsonObject whereObj = where.toObject();
        for (const QString& field : whereObj.keys()) {
            const QJsonValue predValue = whereObj.value(field);
            if (!predValue.isObject())
                return fail(
                    QStringLiteral("where_predicate_type"),
                    QStringLiteral("where predicate for '%1' must be an "
                                   "object")
                        .arg(field));
            const QJsonObject predObj = predValue.toObject();
            if (!keysWithin(predObj,
                            {QStringLiteral("op"),
                             QStringLiteral("value")},
                            &offending))
                return fail(
                    QStringLiteral("where_unknown_field"),
                    QStringLiteral("unknown field '%1' in where predicate "
                                   "'%2'")
                        .arg(offending, field));

            WherePredicate pred;
            pred.field = field;

            const QJsonValue op = predObj.value(QStringLiteral("op"));
            if (op.isUndefined() || !op.isString())
                return fail(QStringLiteral("where_op_missing"),
                            QStringLiteral("where predicate '%1' needs a "
                                           "string 'op'")
                                .arg(field));
            const QString opStr = op.toString();
            if (opStr == QStringLiteral("eq"))
                pred.op = WhereOp::Eq;
            else if (opStr == QStringLiteral("ne"))
                pred.op = WhereOp::Ne;
            else if (opStr == QStringLiteral("in"))
                pred.op = WhereOp::In;
            else if (opStr == QStringLiteral("exists"))
                pred.op = WhereOp::Exists;
            else
                return fail(QStringLiteral("where_op_unknown"),
                            QStringLiteral("where op '%1' is not in "
                                           "{eq, ne, in, exists}")
                                .arg(opStr));

            const QJsonValue value = predObj.value(QStringLiteral("value"));
            if (pred.op == WhereOp::Exists) {
                if (!value.isUndefined())
                    return fail(
                        QStringLiteral("where_value_unexpected"),
                        QStringLiteral("exists predicate '%1' takes no "
                                       "'value'")
                            .arg(field));
            } else if (pred.op == WhereOp::In) {
                if (value.isUndefined() || !value.isArray())
                    return fail(QStringLiteral("where_value_type"),
                                QStringLiteral("in predicate '%1' needs an "
                                               "array 'value'")
                                    .arg(field));
                QVariantList list;
                for (const QJsonValue& item : value.toArray()) {
                    if (!isWhereScalar(item))
                        return fail(
                            QStringLiteral("where_value_type"),
                            QStringLiteral("in predicate '%1' has a "
                                           "non-scalar element")
                                .arg(field));
                    if (item.isString())
                        list.append(item.toString());
                    else if (item.isBool())
                        list.append(item.toBool());
                    else
                        list.append(item.toDouble());
                }
                pred.value = QVariant(list);
            } else { // Eq / Ne
                if (!value.isUndefined() && !isWhereScalar(value))
                    return fail(
                        QStringLiteral("where_value_type"),
                        QStringLiteral("eq/ne predicate '%1' needs a "
                                       "string/number/bool 'value'")
                            .arg(field));
                if (value.isUndefined())
                    return fail(QStringLiteral("where_value_type"),
                                QStringLiteral("eq/ne predicate '%1' needs a "
                                               "'value'")
                                    .arg(field));
                if (value.isString())
                    pred.value = value.toString();
                else if (value.isBool())
                    pred.value = value.toBool();
                else
                    pred.value = value.toDouble();
            }
            rule.where.append(pred);
        }
    }

    // 6. condition — optional flat AND list, closed kind enum.
    const QJsonValue conditions = json.value(QStringLiteral("condition"));
    if (!conditions.isUndefined()) {
        if (!conditions.isArray())
            return fail(QStringLiteral("condition_type"),
                        QStringLiteral("'condition' must be an array"));
        for (const QJsonValue& item : conditions.toArray()) {
            if (!item.isObject())
                return fail(QStringLiteral("condition_item_type"),
                            QStringLiteral("condition entries must be "
                                           "objects"));
            const QJsonObject condObj = item.toObject();
            const QJsonValue kind = condObj.value(QStringLiteral("kind"));
            if (!isNonEmptyString(kind))
                return fail(QStringLiteral("condition_kind_unknown"),
                            QStringLiteral("condition entry needs a string "
                                           "'kind'"));

            const QString kindStr = kind.toString();
            if (kindStr == QStringLiteral("idle_for")
                || kindStr == QStringLiteral("since_last_motion")) {
                if (!keysWithin(condObj,
                                {QStringLiteral("kind"),
                                 QStringLiteral("op"),
                                 QStringLiteral("value_s")},
                                &offending))
                    return fail(
                        QStringLiteral("condition_unknown_field"),
                        QStringLiteral("unknown field '%1' in %2 condition")
                            .arg(offending, kindStr));

                const QJsonValue op = condObj.value(QStringLiteral("op"));
                if (!op.isString()
                    || (op.toString() != QStringLiteral("gt")
                        && op.toString() != QStringLiteral("ge")))
                    return fail(
                        QStringLiteral("condition_field_invalid"),
                        QStringLiteral("%1 condition needs op ∈ {gt, ge}")
                            .arg(kindStr));
                const ThresholdOp thresholdOp =
                    op.toString() == QStringLiteral("gt")
                        ? ThresholdOp::Gt
                        : ThresholdOp::Ge;

                const QJsonValue valueS = condObj.value(
                    QStringLiteral("value_s"));
                if (!isNumber(valueS) || valueS.toDouble() < 0.0)
                    return fail(
                        QStringLiteral("condition_field_invalid"),
                        QStringLiteral("%1 condition needs a non-negative "
                                       "number 'value_s'")
                            .arg(kindStr));
                const qint64 valueMs = static_cast<qint64>(
                    std::round(valueS.toDouble() * 1000.0));

                if (kindStr == QStringLiteral("idle_for"))
                    rule.conditions.append(
                        IdleForCondition{thresholdOp, valueMs});
                else
                    rule.conditions.append(
                        SinceLastMotionCondition{thresholdOp, valueMs});
            } else if (kindStr == QStringLiteral("time_of_day")) {
                if (!keysWithin(condObj,
                                {QStringLiteral("kind"),
                                 QStringLiteral("from_hour"),
                                 QStringLiteral("to_hour")},
                                &offending))
                    return fail(
                        QStringLiteral("condition_unknown_field"),
                        QStringLiteral("unknown field '%1' in time_of_day "
                                       "condition")
                            .arg(offending));
                const QJsonValue from = condObj.value(
                    QStringLiteral("from_hour"));
                const QJsonValue to = condObj.value(
                    QStringLiteral("to_hour"));
                const bool rangesOk =
                    isIntegralNumber(from) && isIntegralNumber(to)
                    && from.toDouble() >= 0.0 && from.toDouble() <= 23.0
                    && to.toDouble() >= 0.0 && to.toDouble() <= 23.0;
                if (!rangesOk)
                    return fail(
                        QStringLiteral("condition_field_invalid"),
                        QStringLiteral("time_of_day needs integer "
                                       "from_hour/to_hour in [0, 23]"));
                rule.conditions.append(TimeOfDayCondition{
                    static_cast<int>(from.toDouble()),
                    static_cast<int>(to.toDouble())});
            } else if (kindStr == QStringLiteral("model_is")) {
                if (!keysWithin(condObj,
                                {QStringLiteral("kind"),
                                 QStringLiteral("value")},
                                &offending))
                    return fail(
                        QStringLiteral("condition_unknown_field"),
                        QStringLiteral("unknown field '%1' in model_is "
                                       "condition")
                            .arg(offending));
                const QJsonValue value = condObj.value(
                    QStringLiteral("value"));
                if (!isNonEmptyString(value))
                    return fail(QStringLiteral("condition_field_invalid"),
                                QStringLiteral("model_is needs a non-empty "
                                               "string 'value'"));
                rule.conditions.append(
                    ModelIsCondition{value.toString()});
            } else if (kindStr == QStringLiteral("pack_mounted")) {
                if (!keysWithin(condObj,
                                {QStringLiteral("kind"),
                                 QStringLiteral("value")},
                                &offending))
                    return fail(
                        QStringLiteral("condition_unknown_field"),
                        QStringLiteral("unknown field '%1' in pack_mounted "
                                       "condition")
                            .arg(offending));
                const QJsonValue value = condObj.value(
                    QStringLiteral("value"));
                if (!value.isString()) // "" (unmounted) IS legal here
                    return fail(QStringLiteral("condition_field_invalid"),
                                QStringLiteral("pack_mounted needs a string "
                                               "'value' (\"\" = requires "
                                               "nothing mounted)"));
                rule.conditions.append(
                    PackMountedCondition{value.toString()});
            } else {
                return fail(QStringLiteral("condition_kind_unknown"),
                            QStringLiteral("condition kind '%1' is not in "
                                           "{idle_for, since_last_motion, "
                                           "time_of_day, model_is, "
                                           "pack_mounted}")
                                .arg(kindStr));
            }
        }
    }

    // 7. actions — required, 1..8, closed kind enum, per-kind fields.
    const QJsonValue actions = json.value(QStringLiteral("actions"));
    if (actions.isUndefined())
        return fail(QStringLiteral("actions_missing"),
                    QStringLiteral("'actions' is required"));
    if (!actions.isArray())
        return fail(QStringLiteral("actions_missing"),
                    QStringLiteral("'actions' must be an array"));
    const QJsonArray actionArray = actions.toArray();
    if (actionArray.isEmpty())
        return fail(QStringLiteral("actions_empty"),
                    QStringLiteral("'actions' must contain at least one "
                                   "action"));
    if (actionArray.size() > kRuleMaxActionsPerRule)
        return fail(QStringLiteral("actions_too_many"),
                    QStringLiteral("'actions' holds %1, over the cap of %2")
                        .arg(actionArray.size())
                        .arg(kRuleMaxActionsPerRule));

    for (const QJsonValue& item : actionArray) {
        if (!item.isObject())
            return fail(QStringLiteral("action_item_type"),
                        QStringLiteral("action entries must be objects"));
        const QJsonObject actionObj = item.toObject();
        const QJsonValue kind = actionObj.value(QStringLiteral("kind"));
        if (!isNonEmptyString(kind))
            return fail(QStringLiteral("action_kind_unknown"),
                        QStringLiteral("action entry needs a string 'kind'"));
        const QString kindStr = kind.toString();

        RuleAction action;
        if (kindStr == QStringLiteral("play_motion")) {
            action.kind = RuleActionKind::PlayMotion;
            if (!keysWithin(actionObj,
                            {QStringLiteral("kind"),
                             QStringLiteral("group"),
                             QStringLiteral("index")},
                            &offending))
                return fail(
                    QStringLiteral("action_unknown_field"),
                    QStringLiteral("unknown field '%1' in play_motion "
                                   "action")
                        .arg(offending));
            const QJsonValue group = actionObj.value(
                QStringLiteral("group"));
            if (!isNonEmptyString(group))
                return fail(QStringLiteral("action_field_invalid"),
                            QStringLiteral("play_motion needs a non-empty "
                                           "string 'group'"));
            action.group = group.toString();
            const QJsonValue index = actionObj.value(
                QStringLiteral("index"));
            if (!index.isUndefined()) {
                if (!isIntegralNumber(index) || index.toDouble() < 0.0)
                    return fail(
                        QStringLiteral("action_field_invalid"),
                        QStringLiteral("play_motion 'index' must be a "
                                       "non-negative integer"));
                action.index = static_cast<int>(index.toDouble());
            } // else: default index = 0
        } else if (kindStr == QStringLiteral("play_pack_audio")) {
            action.kind = RuleActionKind::PlayPackAudio;
            if (!keysWithin(actionObj,
                            {QStringLiteral("kind"),
                             QStringLiteral("pack"),
                             QStringLiteral("group")},
                            &offending))
                return fail(
                    QStringLiteral("action_unknown_field"),
                    QStringLiteral("unknown field '%1' in play_pack_audio "
                                   "action")
                        .arg(offending));
            const QJsonValue pack = actionObj.value(
                QStringLiteral("pack"));
            const QJsonValue group = actionObj.value(
                QStringLiteral("group"));
            if (!isNonEmptyString(pack) || !isNonEmptyString(group))
                return fail(
                    QStringLiteral("action_field_invalid"),
                    QStringLiteral("play_pack_audio needs non-empty string "
                                   "'pack' and 'group'"));
            action.pack = pack.toString();
            action.group = group.toString();
        } else if (kindStr == QStringLiteral("set_expression")) {
            action.kind = RuleActionKind::SetExpression;
            if (!keysWithin(actionObj,
                            {QStringLiteral("kind"),
                             QStringLiteral("id")},
                            &offending))
                return fail(
                    QStringLiteral("action_unknown_field"),
                    QStringLiteral("unknown field '%1' in set_expression "
                                   "action")
                        .arg(offending));
            const QJsonValue id = actionObj.value(QStringLiteral("id"));
            if (!isNonEmptyString(id))
                return fail(QStringLiteral("action_field_invalid"),
                            QStringLiteral("set_expression needs a non-empty "
                                           "string 'id'"));
            action.id = id.toString();
        } else if (kindStr == QStringLiteral("notify_bubble")) {
            action.kind = RuleActionKind::NotifyBubble;
            if (!keysWithin(actionObj,
                            {QStringLiteral("kind"),
                             QStringLiteral("text"),
                             QStringLiteral("duration_ms")},
                            &offending))
                return fail(
                    QStringLiteral("action_unknown_field"),
                    QStringLiteral("unknown field '%1' in notify_bubble "
                                   "action")
                        .arg(offending));
            const QJsonValue text = actionObj.value(
                QStringLiteral("text"));
            if (!isNonEmptyString(text))
                return fail(QStringLiteral("action_field_invalid"),
                            QStringLiteral("notify_bubble needs a non-empty "
                                           "string 'text'"));
            action.text = text.toString();
            const QJsonValue duration = actionObj.value(
                QStringLiteral("duration_ms"));
            if (!duration.isUndefined()) {
                if (!isIntegralNumber(duration) || duration.toDouble() < 0.0)
                    return fail(
                        QStringLiteral("action_field_invalid"),
                        QStringLiteral("notify_bubble 'duration_ms' must be "
                                       "a non-negative integer"));
                action.durationMs = static_cast<qint64>(
                    duration.toDouble());
            } // else: default duration = 0
        } else if (kindStr == QStringLiteral("publish_event")) {
            action.kind = RuleActionKind::PublishEvent;
            if (!keysWithin(actionObj,
                            {QStringLiteral("kind"),
                             QStringLiteral("event"),
                             QStringLiteral("payload")},
                            &offending))
                return fail(
                    QStringLiteral("action_unknown_field"),
                    QStringLiteral("unknown field '%1' in publish_event "
                                   "action")
                        .arg(offending));
            const QJsonValue eventVal = actionObj.value(
                QStringLiteral("event"));
            if (!isNonEmptyString(eventVal))
                return fail(QStringLiteral("action_field_invalid"),
                            QStringLiteral("publish_event needs a non-empty "
                                           "string 'event'"));
            action.event = eventVal.toString();
            if (!action.event.startsWith(QStringLiteral("pet.rules.")))
                return fail(
                    QStringLiteral("publish_event_prefix"),
                    QStringLiteral("publish_event event '%1' lacks the "
                                   "'pet.rules.' prefix")
                        .arg(action.event));
            if (!isValidEventIdSyntax(action.event))
                return fail(
                    QStringLiteral("publish_event_syntax"),
                    QStringLiteral("publish_event event '%1' fails event-id "
                                   "syntax")
                        .arg(action.event));
            const QJsonValue payload = actionObj.value(
                QStringLiteral("payload"));
            if (!payload.isUndefined()) {
                if (!payload.isObject())
                    return fail(
                        QStringLiteral("action_field_invalid"),
                        QStringLiteral("publish_event 'payload' must be an "
                                       "object"));
                action.payload = payload.toObject();
            } // else: default payload = {}
        } else {
            return fail(
                QStringLiteral("action_kind_unknown"),
                QStringLiteral("action kind '%1' is not in {play_motion, "
                               "play_pack_audio, set_expression, "
                               "notify_bubble, publish_event}")
                    .arg(kindStr));
        }
        rule.actions.append(action);
    }

    return rule;
}

bool RuleEngine::upsertRule(const CompiledRule& rule, RuleError* error)
{
    RuleError sink;
    RuleError* err = error ? error : &sink;

    auto fail = [&](const QString& code, const QString& message) {
        err->ruleId = rule.id;
        err->code = code;
        err->message = message;
        LOG_DEBUG("RuleEngine: upsert rejected rule \"{}\": {} ({})",
                  rule.id.toStdString(), message.toStdString(),
                  code.toStdString());
        return false;
    };

    if (!isValidRuleIdSyntax(rule.id))
        return fail(QStringLiteral("id_invalid"),
                    QStringLiteral("id '%1' fails the dot-segmented "
                                   "[a-z0-9_-] form")
                        .arg(rule.id));
    if (rule.actions.isEmpty())
        return fail(QStringLiteral("actions_empty"),
                    QStringLiteral("a rule needs at least one action"));
    if (rule.actions.size() > kRuleMaxActionsPerRule)
        return fail(QStringLiteral("actions_too_many"),
                    QStringLiteral("rule holds %1 actions, over the cap of "
                                   "%2")
                        .arg(rule.actions.size())
                        .arg(kRuleMaxActionsPerRule));
    if (!m_rules.contains(rule.id) && m_rules.size() >= kRuleMaxRules)
        return fail(QStringLiteral("max_rules_exceeded"),
                    QStringLiteral("rule ceiling of %1 reached")
                        .arg(kRuleMaxRules));

    CompiledRule stored = rule;
    stored.declarationOrder = m_nextDeclarationOrder++;

    const auto previous = m_rules.constFind(rule.id);
    const bool freshInsert = previous == m_rules.constEnd();
    // disabled→enabled transition starts a NEW enabled cycle: drop the old
    // one-shot latch + cooldown window. (Same-cycle replaces keep them.)
    const bool reEnabled = !freshInsert && !previous->enabled && stored.enabled;
    m_rules.insert(rule.id, stored);
    if (reEnabled || freshInsert)
        m_fireState.remove(rule.id);

    warnIfCancelDefaultInvalid(stored);
    return true;
}

std::optional<CompiledRule> RuleEngine::loadRule(const QJsonObject& ruleJson,
                                                 RuleError* error)
{
    std::optional<CompiledRule> compiled = compileRule(ruleJson, error);
    if (!compiled)
        return std::nullopt;
    if (!upsertRule(*compiled, error))
        return std::nullopt;
    return m_rules.value(compiled->id);
}

bool RuleEngine::removeRule(const QString& ruleId)
{
    const bool removed = m_rules.remove(ruleId) != 0;
    m_fireState.remove(ruleId);
    return removed;
}

int RuleEngine::removeRulesByOwnerPrefix(const QString& ownerPrefix)
{
    QStringList doomed;
    for (auto it = m_rules.constBegin(); it != m_rules.constEnd(); ++it) {
        if (it.key().startsWith(ownerPrefix))
            doomed.append(it.key());
    }
    for (const QString& id : doomed)
        m_rules.remove(id);
    for (const QString& id : doomed)
        m_fireState.remove(id);
    return doomed.size();
}

int RuleEngine::ruleCount() const
{
    return m_rules.size();
}

std::optional<CompiledRule> RuleEngine::ruleById(const QString& ruleId) const
{
    const auto it = m_rules.constFind(ruleId);
    if (it == m_rules.constEnd())
        return std::nullopt;
    return it.value();
}

QList<CompiledRule> RuleEngine::orderedRules() const
{
    QList<CompiledRule> list = m_rules.values();
    std::sort(list.begin(), list.end(), ruleCompositeLess);
    return list;
}

void RuleEngine::resetOneShotCycles()
{
    for (auto it = m_fireState.begin(); it != m_fireState.end(); ++it)
        it->firedThisCycle = false;
}

void RuleEngine::resetOneShotCycle(const QString& ruleId)
{
    const auto it = m_fireState.find(ruleId);
    if (it != m_fireState.end())
        it->firedThisCycle = false;
}

FireResult RuleEngine::shouldFire(const CompiledRule& rule,
                                  const EvalContext& ctx, qint64 nowMs) const
{
    if (!rule.enabled)
        return FireResult::skipping(QStringLiteral("disabled"));
    if (!ctx.connected)
        return FireResult::skipping(QStringLiteral("not_connected"));

    const auto state = m_fireState.constFind(rule.id);
    if (state != m_fireState.constEnd()) {
        if (rule.oneShot && state->firedThisCycle)
            return FireResult::skipping(
                QStringLiteral("one_shot_fired"));
        if (rule.cooldownMs > 0 && state->lastFireMs > 0
            && nowMs - state->lastFireMs < rule.cooldownMs)
            return FireResult::skipping(
                QStringLiteral("cooldown_active"));
    }

    for (const WherePredicate& pred : rule.where) {
        if (!matchPredicate(pred, ctx.payload))
            return FireResult::skipping(
                QStringLiteral("where_predicate_failed"));
    }

    for (const RuleCondition& cond : rule.conditions) {
        if (!matchCondition(rule.id, cond, ctx, nowMs))
            return FireResult::skipping(
                QStringLiteral("condition_failed"));
    }

    return FireResult::firing();
}

void RuleEngine::recordFire(const QString& ruleId, qint64 nowMs)
{
    if (!m_rules.contains(ruleId)) {
        LOG_DEBUG("RuleEngine: recordFire on unknown rule \"{}\" ignored",
                  ruleId.toStdString());
        return;
    }
    FireState& state = m_fireState[ruleId];
    state.lastFireMs = nowMs;
    state.firedThisCycle = true;
}

QSet<QString> RuleEngine::subscribedEventIds() const
{
    QSet<QString> ids;
    for (auto it = m_rules.constBegin(); it != m_rules.constEnd(); ++it) {
        if (it->enabled)
            ids.insert(it->triggerEvent);
    }
    return ids;
}

void RuleEngine::warnIfCancelDefaultInvalid(const CompiledRule& rule) const
{
    if (!rule.cancelDefault)
        return;
    const auto known = m_knownEvents.constFind(rule.triggerEvent);
    if (known == m_knownEvents.constEnd() || known.value())
        return; // unknown (no opinion) or cancellable — nothing to say
    LOG_WARN("RuleEngine: rule \"{}\" declares cancel_default=true on the "
             "known NON-cancellable event \"{}\" — rule loaded, but the "
             "default behavior will still run",
             rule.id.toStdString(), rule.triggerEvent.toStdString());
}

} // namespace core
