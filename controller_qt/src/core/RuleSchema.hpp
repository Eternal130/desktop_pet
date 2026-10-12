#pragma once

#include <QJsonObject>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVector>

#include <variant>

// RuleSchema (S8d) — pure data model for the declarative rule engine: limits,
// enums, the structured error type, the typed condition/action structures,
// CompiledRule, EvalContext and the shouldFire result. Data ONLY — all
// behavior lives in RuleEngine.hpp/.cpp. Host-internal (core namespace), NOT
// part of the plugin SDK surface.
//
// Three layers per the S8d spec:
//   1. Schema parse/validate ("strict in")  → RuleEngine::compileRule
//   2. Load-time compilation                → CompiledRule (zero JSON key
//      lookups at evaluation time)
//   3. Pure evaluation core                 → RuleEngine::shouldFire
//      ("lenient out": unknown context data fails CLOSED = false + DEBUG)
//
// NO bus wiring / DB loading / QML bridging here — see the TODO(S8e)/
// TODO(S8f) markers in RuleEngine.hpp.

namespace core {

// ── Schema constants (S8d spec) ─────────────────────────────────────────────

// The only legal "schema_version" value. Unknown versions are rejected.
inline constexpr int kRuleSchemaVersion = 1;

// Hard ceiling on rules held by one engine instance. A further upsert of a
// NEW id is rejected with max_rules_exceeded (replacing an existing id at
// capacity stays legal).
inline constexpr int kRuleMaxRules = 200;

// Hard ceiling on actions inside one rule (spec: "必填非空，≤8").
inline constexpr int kRuleMaxActionsPerRule = 8;

// Default priority when the JSON omits "priority".
inline constexpr int kRuleDefaultPriority = 100;

// ── Scope & ordering ────────────────────────────────────────────────────────

// Which load surface declared the rule. Participates in the composite order
// (priority asc → scope weight asc → declaration order asc). Host-assigned:
// the rule JSON never carries a scope field (an explicit "scope" key is an
// unknown top-level field → rejected, strict-in).
enum class RuleScope {
    Instance = 0,  // per-pet instance rule set
    Panel = 1,     // panel-global rule set
    Plugin = 2,    // plugin-declared rule set
};

inline constexpr int ruleScopeWeight(RuleScope scope)
{
    return static_cast<int>(scope);
}

// ── Structured error ────────────────────────────────────────────────────────

// Load/compile failure. NEVER thrown — compileRule/upsertRule report through
// out-params. `code` is a stable machine-readable identifier (full matrix in
// RuleEngine.cpp's file comment); `message` carries the human detail.
struct RuleError {
    QString ruleId;  // best-effort — empty when the id itself is unusable
    QString code;
    QString message;

    bool isValid() const { return !code.isEmpty(); }
};

// ── Where predicates (compiled trigger payload filters) ─────────────────────

enum class WhereOp { Eq, Ne, In, Exists };

// One compiled payload predicate: `field` is the payload key, `op` the
// comparison, `value` the compiled operand.
//   Eq/Ne — value is a scalar QVariant (QString / double / bool); comparison
//           is TYPE-SENSITIVE (a JSON string never equals a JSON number).
//           Ne is defined as !Eq, so a type mismatch satisfies Ne.
//   In    — value holds a QVariantList of scalars; true iff the payload value
//           equals ANY element (same type-sensitive rule).
//   Exists— value unused; true iff the payload key is present AND non-null
//           (explicit JSON null is treated as absent — same "null = no data"
//           convention as the stats_state gpu/vram sentinels).
struct WherePredicate {
    QString field;
    WhereOp op = WhereOp::Eq;
    QVariant value;
};

// ── Conditions (typed, kind-tagged via std::variant) ───────────────────────

enum class ThresholdOp { Gt, Ge };

// value_s is compiled to whole milliseconds at load time.
struct IdleForCondition {
    ThresholdOp op = ThresholdOp::Gt;
    qint64 valueMs = 0;
};

struct SinceLastMotionCondition {
    ThresholdOp op = ThresholdOp::Gt;
    qint64 valueMs = 0;
};

// Window [fromHour, toHour] inclusive on BOTH ends. fromHour > toHour is the
// legal wrap-around form (22 → 6 covers 22:00–06:59). fromHour == toHour
// degenerates to the single hour [h, h].
struct TimeOfDayCondition {
    int fromHour = 0;
    int toHour = 0;
};

// Exact (case-sensitive) model-name match. Empty context modelName is
// "snapshot missing" → fail-closed false.
struct ModelIsCondition {
    QString value;
};

// value "" means "requires NOTHING mounted" (spec: ""=未挂载). Empty
// context mountedPackId is a legal "not mounted" state, NOT an unknown.
struct PackMountedCondition {
    QString value;
};

using RuleCondition = std::variant<IdleForCondition, SinceLastMotionCondition,
                                   TimeOfDayCondition, ModelIsCondition,
                                   PackMountedCondition>;

// ── Actions (kind enum + parameters) ────────────────────────────────────────

enum class RuleActionKind {
    PlayMotion,
    PlayPackAudio,
    SetExpression,
    NotifyBubble,
    PublishEvent,
};

// Flat parameter struct: fields are meaningful per kind (compile-time
// validation guarantees the required subset is populated; unused fields keep
// their defaults).
//   PlayMotion    — group (required non-empty), index (default 0)
//   PlayPackAudio — pack + group (both required non-empty)
//   SetExpression — id (required non-empty)
//   NotifyBubble  — text (required non-empty), durationMs (default 0)
//   PublishEvent  — event (required, "pet.rules." prefix + id syntax),
//                   payload (default empty object)
struct RuleAction {
    RuleActionKind kind = RuleActionKind::PlayMotion;
    QString group;
    int index = 0;
    QString pack;
    QString id;
    QString text;
    qint64 durationMs = 0;
    QString event;
    QJsonObject payload;
};

// ── Compiled rule ───────────────────────────────────────────────────────────

// The load-time compilation product: evaluation touches ZERO JSON keys.
// `scope` is host-assigned (compileRule defaults it to Panel; the loader
// overwrites before upsert). `declarationOrder` is assigned by
// RuleEngine::upsertRule from a monotonic counter — every upsert (including
// an atomic replace of an existing id) restarts the tiebreak position.
struct CompiledRule {
    QString id;
    RuleScope scope = RuleScope::Panel;
    int declarationOrder = 0;
    bool enabled = true;
    QString triggerEvent;
    QVector<WherePredicate> where;
    QVector<RuleCondition> conditions;
    QVector<RuleAction> actions;
    bool cancelDefault = false;
    int priority = kRuleDefaultPriority;
    qint64 cooldownMs = 0;
    bool oneShot = false;
    bool dryRun = false;
};

// Composite ordering comparator: (priority asc, scope weight asc with
// Instance < Panel < Plugin, declaration order asc). Defined in RuleEngine.cpp.
bool ruleCompositeLess(const CompiledRule& a, const CompiledRule& b);

// ── Evaluation context ──────────────────────────────────────────────────────

// The snapshot shouldFire evaluates against. S8d tests construct it by hand;
// TODO(S8e): fill from the event-bus event stream + session snapshot
// (payload from the triggering event, timestamps from the session's
// activity/motion trackers, currentHour from the wall clock).
struct EvalContext {
    QJsonObject payload;
    qint64 lastActivityMs = 0;  // 0 = unknown → idle_for fails closed
    qint64 lastMotionMs = 0;    // 0 = unknown → since_last_motion fails closed
    QString modelName;          // empty = unknown → model_is fails closed
    QString mountedPackId;      // empty = nothing mounted (legal state)
    bool connected = false;     // renderer WS link gate — false skips ALL rules
    int currentHour = -1;       // 0-23; -1 = unknown → time_of_day fails closed
};

// ── shouldFire result ───────────────────────────────────────────────────────

// `fire` is the decision; `reason` is a stable machine-readable skip cause
// (empty when firing) so the S8e caller can DEBUG-log skips without parsing
// free text. Stable reasons: "disabled", "not_connected", "one_shot_fired",
// "cooldown_active", "where_predicate_failed", "condition_failed".
struct FireResult {
    bool fire = false;
    QString reason;

    static FireResult firing() { return FireResult{true, QString()}; }
    static FireResult skipping(const QString& why)
    {
        return FireResult{false, why};
    }
};

// ── Syntax validators (defined in RuleEngine.cpp) ──────────────────────────

// Event-id syntax: ≥3 dot-separated segments, every segment non-empty and
// ASCII [a-z0-9_] only (rejects wildcards, uppercase, '-', spaces, non-ASCII).
// Deliberately SYNTACTIC ONLY — ids unknown to the event catalog load fine
// (the catalog check is the bus's job, S8e).
bool isValidEventIdSyntax(const QString& eventId);

// Rule-id syntax: dot-separated owner-namespaced identifier. Every segment
// must be non-empty ASCII [a-z0-9_-] (a dotless id = one segment, matching
// the schema's [a-z0-9_-]+ form); dots exist so S8f plugin-owned ids like
// "plugin.<id>.<rule>" can be swept by the owner-prefix removal. No leading/
// trailing dot, no empty segments, no uppercase.
bool isValidRuleIdSyntax(const QString& ruleId);

} // namespace core
