#pragma once

#include "core/RuleSchema.hpp"

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QString>

#include <optional>

// RuleEngine (S8d) — the declarative rule engine CORE. Pure host-internal
// logic: schema parse/validate/compile (layer 1+2) and the cooldown /
// one-shot / where / condition evaluation kernel (layer 3). NOT a QObject —
// no signals, no bus, no DB, no QML. Deliberately unwired in S8d:
//
//   TODO(S8e): subscribe via subscribedEventIds() to the EventBus, build an
//   EvalContext per arriving event (payload = event payload; timestamps /
//   modelName / mountedPackId / connected from the owning session snapshot;
//   currentHour from QTime), iterate orderedRules() filtered by trigger
//   event, call shouldFire() then recordFire(), then dispatch the compiled
//   actions (play_motion/play_pack_audio/set_expression/notify_bubble to the
//   owning instance, publish_event back onto the bus).
//   TODO(S8f): plugin-declared rules load with RuleScope::Plugin and the
//   "plugin.<id>." owner prefix; plugin unload sweeps them via
//   removeRulesByOwnerPrefix().
//
// Deployment shape: ONE engine per scope context is the expected S8e layout
// (an instance engine per pet, a panel engine for panel rules) — "id 作用域
// 内唯一" from the schema spec is enforced by the upsert keying (same id
// atomically replaces; ids never coexist within one engine). The scope enum
// exists for cross-scope MERGED ordering (panel-level dispatch iterating
// several engines' orderedRules()).
//
// Error discipline: NOTHING in this class throws. Compile/validation
// failures return std::nullopt + a structured RuleError (strict-in); runtime
// unknowns fail closed (lenient-out) with a DEBUG log.
namespace core {

class RuleEngine {
public:
    // event id → cancellable flag. Only ids PRESENT in the table are "known";
    // an absent id is unknown (no cancel_default opinion → no WARN). S8e
    // injects the EventCatalog's cancellable matrix here.
    using KnownEventTable = QHash<QString, bool>;

    explicit RuleEngine(const KnownEventTable& knownEvents = KnownEventTable());

    // Replace the known-event table (load-time WARN input). Affects only
    // subsequent upserts' cancel_default static check.
    void setKnownEventTable(const KnownEventTable& table);

    // ── Layer 1+2: parse / validate / compile ──────────────────────────────

    // JSON rule object → CompiledRule. Stateless w.r.t. the rule set (the
    // rule is NOT stored). Returns std::nullopt + *error on any schema
    // violation; *error is untouched on success. Never throws.
    std::optional<CompiledRule> compileRule(const QJsonObject& ruleJson,
                                            RuleError* error = nullptr) const;

    // ── Rule set management ────────────────────────────────────────────────

    // Insert or atomically replace (same id). On replace the rule gets a
    // FRESH declaration order and — when it transitioned disabled→enabled —
    // a fresh enabled cycle (one-shot latch + cooldown window cleared).
    // Validates the basics (id syntax, action count) for hand-built rules
    // and rejects beyond kRuleMaxRules. Emits the cancel_default WARN for
    // known non-cancellable events (load continues — WARN, not reject).
    bool upsertRule(const CompiledRule& rule, RuleError* error = nullptr);

    // compileRule + upsertRule in one call: compile, then store on success.
    std::optional<CompiledRule> loadRule(const QJsonObject& ruleJson,
                                         RuleError* error = nullptr);

    // Remove by exact id (also drops its fire bookkeeping). False if absent.
    bool removeRule(const QString& ruleId);

    // Bulk-remove every rule whose id starts with ownerPrefix (the S8f
    // plugin-unload sweep, prefix "plugin.<id>."). Returns the removed count.
    int removeRulesByOwnerPrefix(const QString& ownerPrefix);

    int ruleCount() const;

    // Copy of the stored rule, or nullopt for an unknown id.
    std::optional<CompiledRule> ruleById(const QString& ruleId) const;

    // All rules sorted by the composite order (priority asc, scope weight
    // asc, declaration order asc).
    QList<CompiledRule> orderedRules() const;

    // ── Enabled-cycle & fire bookkeeping ───────────────────────────────────

    // Clear the one-shot latch for EVERY rule (S8e calls this on a wholesale
    // re-enable, e.g. the rules toggle flipping back on). Cooldown windows
    // are KEPT — they are wall-clock based, not cycle based.
    void resetOneShotCycles();

    // Same, for a single rule. No-op for unknown ids.
    void resetOneShotCycle(const QString& ruleId);

    // ── Layer 3: evaluation core ───────────────────────────────────────────

    // Pure decision — mutates NOTHING (cooldown windows therefore cannot be
    // reset by a probe that skips). Check order: enabled → connected →
    // one-shot latch → cooldown window → where predicates → conditions.
    // The one-shot/cooldown state is the ENGINE's bookkeeping keyed by
    // rule.id, so the rule should originate from this engine for those two
    // gates to engage (where/condition checks are fully self-contained).
    FireResult shouldFire(const CompiledRule& rule, const EvalContext& ctx,
                          qint64 nowMs) const;

    // Record a fire: stamps the cooldown window and sets the one-shot latch.
    // Unknown id → DEBUG log + no-op (never a crash).
    void recordFire(const QString& ruleId, qint64 nowMs);

    // ── Differential subscription set (S8e) ────────────────────────────────

    // Event ids referenced by ENABLED rules only — the S8e bus layer diffs
    // this against its current subscriptions on every rule-set mutation.
    QSet<QString> subscribedEventIds() const;

private:
    struct FireState {
        qint64 lastFireMs = 0;
        bool firedThisCycle = false;
    };

    // Load-time static WARN for cancel_default on a KNOWN non-cancellable
    // event. Load continues (spec: WARN, not reject).
    void warnIfCancelDefaultInvalid(const CompiledRule& rule) const;

    KnownEventTable m_knownEvents;
    QHash<QString, CompiledRule> m_rules;     // id → rule (scope-unique key)
    QHash<QString, FireState> m_fireState;    // id → cooldown/one-shot state
    int m_nextDeclarationOrder = 0;
};

} // namespace core // namespace core
