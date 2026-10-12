#pragma once

// Plugin SDK — declarative rule-engine registration family (S8f, v1.6).
//
// Post-freeze interface family: derives pet::IExtApi and is obtained
// EXCLUSIVELY through IPluginContext::queryApi(kRulesApiId, minVersion).
//
// DESIGN — lazy registration, emission-time owner gate: registering a
// rule is an INERT act. NO capability word and NO write-switch consult
// happens at queryApi/registerRule time; the gate fires LATER, at
// ACTION-EMISSION time inside the host's rule engine: when a
// plugin-owned rule (stored id prefix "plugin.<ownId>.") dispatches a
// play_motion or set_expression action, the host LIVE-checks that the
// owning plugin still holds the "instance_tuning" capability
// (kCapabilityInstanceTuning) AND that the host-global
// plugin_write_enabled kill-switch is on — otherwise that one action
// fails with a WARN (the rule's fire is still recorded). A rule
// registered while granted therefore stops acting the moment either
// flips: a lingered rule surface can never outlive its grant (stronger
// than the queryApi-time gating of the v1.2/v1.3 write families, which
// already-held pointers bypass). The notify_bubble / play_pack_audio /
// publish_event channels are deliberately NOT owner-gated (S8f design
// ruling: bubbles are a notification surface, pack audio is already
// mount-gated, publish_event is host-identity and prefix-checked at
// compile time).
//
// Rule JSON schema: the full field tables, condition/action
// vocabularies and the rejection-code matrix live in docs/rules.md.
// The rule document's "schema_version" (currently 1) is the RULE
// FORMAT version and is DECOUPLED from kRulesApiVersion — a schema
// revision does not bump the family version and vice versa.
//
// Identity rules (enforced by the per-plugin proxy, not honor-system):
//   - registerRule stores the rule with the plugin's OWN
//     "plugin.<ownId>." id prefix and RuleScope::Plugin — a plugin
//     cannot claim another owner's id, cannot collide with panel- or
//     instance-scoped ids, and re-registering the same bare id
//     atomically REPLACES its own earlier registration;
//   - unregisterRule takes the BARE id (the proxy re-applies the
//     prefix), so it can only ever reach the plugin's own rules;
//     unregistering an unknown id is a legal no-op;
//   - registerRule validates the full schema: a violation returns
//     PluginError::InvalidArgument (reason surfaced through the
//     plugin's context log) with the engine state UNCHANGED; a NEW id
//     past the host's rule ceiling (200, kRuleMaxRules) also returns
//     InvalidArgument (replacing an existing id at capacity stays
//     legal).
//
// Lifetime: a plugin SHOULD unregister every rule before shutdown()
// returns; the host sweeps leftovers at unload (every rule under
// "plugin.<ownId>." removed, WARN logged) — the sweep is the safety
// net, not the contract. Plugin rules are NOT persisted: they live
// exactly as long as the registration (a panel restart re-registers
// them from the plugin's initialize()).
//
// Threading: GUI thread only, non-blocking, never throws.
//
// ABI discipline: same rules as every api/ header — pure virtual
// declarations + Qt value types only, no std:: across the boundary, no
// inline function bodies (the defaulted destructor exemption applies).

#include <QJsonObject>
#include <QString>

#include "api/IPluginContext.hpp"
#include "api/PluginTypes.hpp"

namespace pet {

// apiId + per-family version for queryApi(). Version 1 = the surface
// below. minVersion > this → queryApi returns nullptr ("feature
// absent", never an error).
inline constexpr char kRulesApiId[] = "pet.rules";
inline constexpr int kRulesApiVersion = 1;

class IRuleApi : public IExtApi
{
public:
    // Register (or atomically replace) ONE rule owned by this plugin.
    // `rule` follows the docs/rules.md JSON schema; its "id" is stored
    // prefixed "plugin.<ownId>." with RuleScope::Plugin. Ok on success;
    // InvalidArgument for a schema violation (engine state unchanged,
    // reason via the plugin's context log) or a new id past the rule
    // ceiling; Generic when the host has no rule engine wired
    // (degraded wiring, never a crash).
    virtual PluginError registerRule(const QJsonObject& rule) = 0;

    // Unregister ONE rule by its BARE id (the proxy re-applies the
    // owner prefix — only this plugin's own rules are reachable).
    // Unregistering an unknown id is a legal no-op.
    virtual void unregisterRule(const QString& ruleId) = 0;
};

} // namespace pet
