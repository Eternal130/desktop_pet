#pragma once

// RuleEngineService (S8e) — the host-side wiring around the pure RuleEngine
// core (S8d). One QObject service in the PanelApplication tree that owns:
//
//   1. DIFFERENTIAL BUS SUBSCRIPTIONS — after every rule-set mutation the
//      desired set is recomputed as (enabled rules' triggerEvent union) ∪
//      (the activity-tracking triple {pet.instance.hit, pet.instance.
//      drag_ended, pet.instance.motion_finished} when ANY enabled rule
//      exists); new ids subscribe at EventPriority::Low (above the MBE /
//      InteractionHandler Lowest tier — S8b), orphans unsubscribe, zero
//      enabled rules → zero subscriptions. All through the shared
//      EventApiImpl HOST channel.
//   2. STATE TRACKING — per-uuid lastActivityMs (hit / drag_ended /
//      non-idle motion_finished) + lastMotionMs (motion_finished), taken
//      from the bus-stamped timestampMs. Conditions evaluate against the
//      PRE-event values: the north-star "hit after long idle" pattern must
//      see the idle time BEFORE the triggering hit, so the tracker updates
//      AFTER the context snapshot (deliberate deviation from the literal
//      spec order — see RuleEngineService::onEvent).
//   3. EVAL CONTEXT ASSEMBLY — payload from the event, timestamps from the
//      tracking tables, modelName/connected from pet::IInstanceApi::
//      instances() (roster miss → sentinels → fail-closed), mountedPackId
//      from the InstanceRuntime cache fed by subscribeInstances (the only
//      SDK surface that carries it), currentHour from the wall clock.
//   4. ACTION DISPATCH — play_motion/set_expression via the shared
//      pet::ITuningApi, play_pack_audio via MountedBehaviorEngine::
//      buildAudioOnlyCommand semantics + an injected renderer channel,
//      notify_bubble via an injected sink, publish_event back onto the bus
//      (host identity). Fail-fast: one failed action WARNs and aborts the
//      rule's remaining actions (the fire is still recorded — cooldown
//      applies).
//   5. LOADING — panel rules from the panel_config kv row "rules"
//      ({"rules":[...]}, same SQLite table PanelStateManager uses),
//      instance rules from the instance_configs blob's "rules" array (read
//      RAW via DatabaseManager::loadInstanceJson — the InstanceConfig
//      serde drops unknown fields). Startup loads everything into a FRESH
//      engine and swaps atomically; later roster changes diff per-uuid
//      (new uuid → load its rules; gone uuid → removeRulesByOwnerPrefix
//      sweep + tracking purge). Per-rule failures WARN (id + reason) and
//      skip; a wholly unreadable source loads as an empty set (ERROR, no
//      crash). Instance rule ids are stored namespaced ("inst.<uuid>.<id>")
//      so one engine's scope-unique keying never collides across instances.
//
// Known-event table: built from api/EventIds.hpp (kEventInstanceHit and
// kEventInstanceIdleTick cancellable, the rest of the catalog not) — powers
// RuleEngine's load-time cancel_default WARN.
//
// TODO(S8f): plugin-declared rules (RuleScope::Plugin + "plugin.<id>."
// owner-prefix sweep at unload), owner gating for play_pack_audio, the
// rules-master toggle → resetOneShotCycles() hook (no UI switch exists in
// S8e, so nothing calls it yet) and the plugin registration channel (the
// upsertRuleJson/removeRuleById surface below is its landing pad).
//
// Threading: GUI thread only, never blocking (every dependency is
// synchronous and in-process). Never throws.

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>

#include <functional>
#include <optional>

#include "api/IEventApi.hpp"
#include "api/IInstanceApi.hpp"
#include "api/ITuningApi.hpp"
#include "core/RuleEngine.hpp"

class DatabaseManager;

namespace core {

class EventApiImpl;

class RuleEngineService : public QObject,
                          public pet::IEventListener,
                          public pet::IInstanceObserver
{
    Q_OBJECT
public:
    // notify_bubble sink: (text, durationMs). Late-injected by PanelUiBoot
    // (NotificationStreamController is a UiBoot child, constructed after
    // this service); a null sink WARNs per notify_bubble action and fails
    // the action (fail-fast).
    using BubbleSink = std::function<void(const QString& text, int durationMs)>;
    // play_pack_audio renderer channel: serialize the built play_audio
    // envelope, hand it over keyed by instance uuid. Return 0 = sent,
    // non-zero = delivery failed (action fails, fail-fast). Wired in
    // PanelApplication over the roster + WsServer.
    using PackAudioChannel =
        std::function<int(const QString& uuid, const QJsonObject& command)>;

    // All pointers are borrowed (service-tree lifetime). eventApi /
    // instanceApi / db may be null — degraded wiring: the engine still
    // loads (db null → zero rules) but never subscribes (eventApi null) or
    // never observes the roster (instanceApi null). tuningApi may be null —
    // play_motion/set_expression actions fail with WARN. The constructor
    // subscribes the roster observer and performs the startup load.
    RuleEngineService(EventApiImpl* eventApi,
                      pet::IInstanceApi* instanceApi,
                      pet::ITuningApi* tuningApi,
                      DatabaseManager* db,
                      QObject* parent = nullptr);
    ~RuleEngineService() override;

    // ── Late injection seams ─────────────────────────────────────────────
    void setBubbleSink(BubbleSink sink) { m_bubbleSink = std::move(sink); }
    void setPackAudioChannel(PackAudioChannel channel)
    {
        m_packAudioChannel = std::move(channel);
    }
    // Pack-resolution scan sources (empty = production defaults, the same
    // pair TuningApiImpl resolves: defaultRendererDir() +
    // ConfigDir::userVoicePacksDir()). Test seam over VoicePackScanner.
    void setPackScanDirs(const QString& rendererDir, const QString& userPacksDir);

    // ── Runtime rule surface (tests in S8e; the S8f plugin registration
    // channel lands here). Compile + scope-stamp + upsert + recompute the
    // subscriptions. Returns the stored rule (namespaced id for Instance
    // scope) or nullopt (WARN logged).
    std::optional<CompiledRule> upsertRuleJson(const QJsonObject& ruleJson,
                                               RuleScope scope,
                                               const QString& ownerUuid);
    // Remove by STORED id (namespaced for instance rules). Recomputes the
    // subscriptions. False when absent.
    bool removeRuleById(const QString& storedRuleId);

    // Introspection (tests / diagnostics).
    int ruleCount() const { return m_engine.ruleCount(); }

    // Test seam: preset the activity tracking (the north-star e2e needs a
    // lastActivityMs several minutes old, which the bus-stamped wall clock
    // cannot produce). Same spirit as RestartController::setBackoffForTest.
    void seedActivityTrackingForTest(const QString& uuid,
                                     qint64 lastActivityMs,
                                     qint64 lastMotionMs);

    // ── pet::IEventListener ──────────────────────────────────────────────
    void onEvent(pet::Event& event) override;

    // ── pet::IInstanceObserver ───────────────────────────────────────────
    void rosterChanged() override;
    void instanceStateChanged(const QString& uuid,
                              const pet::InstanceRuntime& info) override;
    void modelLoadFailed(const QString& uuid, const QString& error) override;

private:
    // One rule >20 fires for a single event smells like a rule-set loop.
    static constexpr int kMaxFiresPerEvent = 20;

    static RuleEngine::KnownEventTable makeKnownEventTable();

    // Full startup load: panel + every roster instance into a FRESH engine,
    // then atomic swap + subscription recompute.
    void loadAll();
    // Compile-and-upsert every entry of a rules array into `engine`
    // (per-rule WARN + skip on rejection). Returns the loaded count.
    int loadRulesArrayInto(RuleEngine& engine, const QJsonArray& rules,
                           RuleScope scope,
                           const QString& ownerUuid) const;
    // Panel kv row "rules" → engine (absent = empty; corrupt = ERROR +
    // empty). Returns the loaded count.
    int loadPanelRulesInto(RuleEngine& engine) const;
    // Instance blob "rules" array → engine (absent = empty; unreadable
    // blob = ERROR + empty, never a crash). Returns the loaded count.
    int loadInstanceRulesInto(RuleEngine& engine, const QString& uuid) const;
    // Compile one rule JSON, stamp scope/owner (namespace Instance ids as
    // "inst.<uuid>.<id>"), upsert into `engine`. Nullopt + WARN on any
    // rejection.
    std::optional<CompiledRule> compileScopedInto(RuleEngine& engine,
                                                  const QJsonObject& ruleJson,
                                                  RuleScope scope,
                                                  const QString& ownerUuid) const;
    // Diff the desired set against m_subscribedIds; subscribe new ids at
    // Low priority, unsubscribe orphans. No-op without the event API.
    void recomputeSubscriptions();
    // Roster uuid list via the injected instance API (empty when unwired).
    QStringList rosterUuids() const;
    // EvalContext snapshot from the PRE-event tracking tables + roster
    // lookup (miss → sentinels, fail-closed downstream).
    EvalContext buildContext(const pet::Event& event) const;
    // Post-evaluation tracker update for the activity triple.
    void updateTracking(const pet::Event& event, qint64 nowMs);
    // Actions in order, fail-fast (one failure WARNs + aborts the rest).
    void executeActions(const CompiledRule& rule, const pet::Event& event,
                        const EvalContext& ctx);
    bool executeOneAction(const RuleAction& action, const pet::Event& event,
                          const EvalContext& ctx);
    bool executePackAudioAction(const RuleAction& action,
                                const pet::Event& event,
                                const EvalContext& ctx);
    // packId (directory name) → absolute pack dir via the dual-source scan
    // (mirrors TuningApiImpl::resolvePackDir). Empty when unknown.
    QString resolvePackDir(const QString& packId) const;

    EventApiImpl* m_eventApi = nullptr;      // not owned (service tree)
    pet::IInstanceApi* m_instanceApi = nullptr; // not owned (service tree)
    pet::ITuningApi* m_tuningApi = nullptr;  // not owned (service tree)
    DatabaseManager* m_db = nullptr;         // not owned (service tree)
    BubbleSink m_bubbleSink;                 // late-injected, may be null
    PackAudioChannel m_packAudioChannel;     // may be null until wired
    QString m_packRendererDir;               // scan seams (empty = defaults)
    QString m_packUserDir;

    RuleEngine::KnownEventTable m_knownEvents;
    RuleEngine m_engine;                     // composed core (atomic swaps)
    QSet<QString> m_subscribedIds;           // current bus subscriptions
    QSet<QString> m_knownUuids;              // last-seen roster membership
    QHash<QString, qint64> m_lastActivityMs; // activity triple tracking
    QHash<QString, qint64> m_lastMotionMs;
    QHash<QString, pet::InstanceRuntime> m_runtimeCache; // mountedPackId source
};

} // namespace core
