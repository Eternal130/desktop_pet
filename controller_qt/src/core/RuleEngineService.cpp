#include "core/RuleEngineService.hpp"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QTime>

#include <spdlog/spdlog.h>

#include "api/EventIds.hpp"
#include "core/ConfigDir.hpp"
#include "core/DatabaseManager.hpp"
#include "core/MetaMkoParser.hpp"
#include "core/MountedBehaviorEngine.hpp"
#include "core/PathResolve.hpp"
#include "core/PluginContextImpl.hpp" // complete core::EventApiImpl
#include "core/VoicePackScanner.hpp"
#include "logging/Logging.hpp"
#include "network/Envelope.hpp"

namespace core {

namespace {

// The activity-tracking triple (spec S8e): source events for
// lastActivityMs / lastMotionMs. Subscribed whenever any enabled rule
// exists so idle_for / since_last_motion conditions always have data.
constexpr auto kTrackHit = pet::kEventInstanceHit;
constexpr auto kTrackDragEnded = pet::kEventDragEnded;
constexpr auto kTrackMotionFinished = pet::kEventInstanceMotionFinished;

// An idle-group motion_finished is the scheduler's own doing, not user
// activity — it must not reset the idle clock (same "idle" group identity
// MountedBehaviorEngine::isIdleMotionPath uses, case-insensitive).
bool isIdleMotionGroup(const QString& group)
{
    return group.compare(QStringLiteral("idle"), Qt::CaseInsensitive) == 0;
}

} // namespace

// ── Construction / teardown ──────────────────────────────────────────────────

RuleEngineService::RuleEngineService(EventApiImpl* eventApi,
                                     pet::IInstanceApi* instanceApi,
                                     pet::ITuningApi* tuningApi,
                                     DatabaseManager* db,
                                     QObject* parent)
    : QObject(parent),
      m_eventApi(eventApi),
      m_instanceApi(instanceApi),
      m_tuningApi(tuningApi),
      m_db(db),
      m_knownEvents(makeKnownEventTable()),
      m_engine(m_knownEvents)
{
    if (m_instanceApi != nullptr) {
        m_instanceApi->subscribeInstances(this);
    } else {
        LOG_WARN("RuleEngineService: no instance API wired — roster "
                 "observation degraded (no instance rules will load)");
    }
    if (m_eventApi == nullptr)
        LOG_WARN("RuleEngineService: no event API wired — rules load but "
                 "the engine stays inert (no subscriptions)");
    if (m_db == nullptr)
        LOG_WARN("RuleEngineService: no database wired — loading an empty "
                 "rule set");

    loadAll();
}

RuleEngineService::~RuleEngineService()
{
    // The service dies BEFORE the bus / instance API in the service tree,
    // but unsubscribe regardless — a dangling listener in the bus table is
    // never acceptable.
    if (m_instanceApi != nullptr)
        m_instanceApi->unsubscribeInstances(this);
    if (m_eventApi != nullptr) {
        for (const QString& id : std::as_const(m_subscribedIds))
            m_eventApi->unsubscribe(id, this);
    }
}

void RuleEngineService::setPackScanDirs(const QString& rendererDir,
                                        const QString& userPacksDir)
{
    m_packRendererDir = rendererDir;
    m_packUserDir = userPacksDir;
}

RuleEngine::KnownEventTable RuleEngineService::makeKnownEventTable()
{
    // Cancellable matrix straight from api/EventIds.hpp (S8a froze the
    // flags; hit + idle_tick are the designated pre-action gates).
    RuleEngine::KnownEventTable table;
    table.insert(QString(pet::kEventInstanceHit), true);
    table.insert(QString(pet::kEventInstanceIdleTick), true);
    table.insert(QString(pet::kEventInstanceReady), false);
    table.insert(QString(pet::kEventInstanceMotionStarted), false);
    table.insert(QString(pet::kEventInstanceMotionFinished), false);
    table.insert(QString(pet::kEventDragStarted), false);
    table.insert(QString(pet::kEventDragEnded), false);
    table.insert(QString(pet::kEventModelLoaded), false);
    table.insert(QString(pet::kEventModelLoadFailed), false);
    table.insert(QString(pet::kEventPackMounted), false);
    table.insert(QString(pet::kEventPackUnmounted), false);
    return table;
}

// ── Loading pipeline ─────────────────────────────────────────────────────────

void RuleEngineService::loadAll()
{
    // Compile EVERYTHING into a fresh engine first, then swap — a half-
    // rejected source can never leave a partially-mutated live set.
    RuleEngine next(m_knownEvents);
    const int panelRules = loadPanelRulesInto(next);
    int instanceRules = 0;
    const QStringList uuids = rosterUuids();
    for (const QString& uuid : uuids)
        instanceRules += loadInstanceRulesInto(next, uuid);

    m_engine = next; // atomic换入 (copy-assign; RuleEngine is a value type)
    m_knownUuids = QSet<QString>(uuids.cbegin(), uuids.cend());
    recomputeSubscriptions();
    LOG_INFO("RuleEngineService: startup load — {} panel rule(s) + {} "
             "instance rule(s) across {} instance(ies), {} bus subscription(s)",
             panelRules, instanceRules, uuids.size(), m_subscribedIds.size());
}

int RuleEngineService::loadPanelRulesInto(RuleEngine& engine) const
{
    if (m_db == nullptr)
        return 0;
    const QString raw = m_db->getValue(QStringLiteral("rules"));
    if (raw.isEmpty())
        return 0; // absent kv row = empty rule set (spec: 不存在=空)
    QJsonParseError parseError;
    const QJsonDocument doc =
        QJsonDocument::fromJson(raw.toUtf8(), &parseError);
    if (!doc.isObject()) {
        LOG_ERROR("RuleEngineService: panel 'rules' kv is corrupt ({}), "
                  "loading an empty rule set",
                  parseError.errorString().toStdString());
        return 0;
    }
    const QJsonArray rules =
        doc.object().value(QStringLiteral("rules")).toArray();
    return loadRulesArrayInto(engine, rules, RuleScope::Panel, QString());
}

int RuleEngineService::loadInstanceRulesInto(RuleEngine& engine,
                                             const QString& uuid) const
{
    if (m_db == nullptr)
        return 0;
    const std::optional<QJsonObject> blob = m_db->loadInstanceJson(uuid);
    if (!blob.has_value()) {
        LOG_ERROR("RuleEngineService: instance '{}' rules blob unreadable "
                  "— loading an empty set for this instance",
                  uuid.toStdString());
        return 0;
    }
    const QJsonArray rules = blob->value(QStringLiteral("rules")).toArray();
    return loadRulesArrayInto(engine, rules, RuleScope::Instance, uuid);
}

int RuleEngineService::loadRulesArrayInto(RuleEngine& engine,
                                          const QJsonArray& rules,
                                          RuleScope scope,
                                          const QString& ownerUuid) const
{
    int loaded = 0;
    for (const QJsonValue& item : rules) {
        if (!item.isObject()) {
            LOG_WARN("RuleEngineService: non-object rule entry skipped "
                     "(scope {}, owner \"{}\")",
                     static_cast<int>(scope), ownerUuid.toStdString());
            continue;
        }
        if (compileScopedInto(engine, item.toObject(), scope, ownerUuid)
                .has_value())
            ++loaded;
    }
    return loaded;
}

std::optional<CompiledRule>
RuleEngineService::compileScopedInto(RuleEngine& engine,
                                     const QJsonObject& ruleJson,
                                     RuleScope scope,
                                     const QString& ownerUuid,
                                     RuleError* error) const
{
    RuleError localError;
    RuleError* err = error != nullptr ? error : &localError;
    std::optional<CompiledRule> compiled =
        engine.compileRule(ruleJson, err);
    if (!compiled.has_value()) {
        LOG_WARN("RuleEngineService: rejected rule \"{}\": {} ({})",
                 err->ruleId.toStdString(), err->message.toStdString(),
                 err->code.toStdString());
        return std::nullopt;
    }
    // Host-assigned scope metadata (the rule JSON never carries a scope —
    // strict-in). Instance ids are namespaced "inst.<uuid>.<id>" and
    // Plugin ids "plugin.<pluginId>.<id>" so the engine's scope-unique id
    // keying never collides across owners AND the deletion sweeps can
    // remove a whole owner's rules by prefix.
    compiled->scope = scope;
    compiled->ownerUuid = ownerUuid;
    if (scope == RuleScope::Instance)
        compiled->id = QStringLiteral("inst.%1.%2").arg(ownerUuid,
                                                        compiled->id);
    else if (scope == RuleScope::Plugin)
        compiled->id = QStringLiteral("plugin.%1.%2").arg(ownerUuid,
                                                          compiled->id);
    RuleError upsertError;
    if (!engine.upsertRule(*compiled, &upsertError)) {
        LOG_WARN("RuleEngineService: upsert rejected rule \"{}\": {} ({})",
                 upsertError.ruleId.toStdString(),
                 upsertError.message.toStdString(),
                 upsertError.code.toStdString());
        if (error != nullptr)
            *error = upsertError;
        return std::nullopt;
    }
    return engine.ruleById(compiled->id);
}

std::optional<CompiledRule>
RuleEngineService::upsertRuleJson(const QJsonObject& ruleJson,
                                  RuleScope scope, const QString& ownerUuid,
                                  RuleError* error)
{
    std::optional<CompiledRule> stored =
        compileScopedInto(m_engine, ruleJson, scope, ownerUuid, error);
    if (stored.has_value())
        recomputeSubscriptions();
    return stored;
}

bool RuleEngineService::removeRuleById(const QString& storedRuleId)
{
    const bool removed = m_engine.removeRule(storedRuleId);
    if (removed)
        recomputeSubscriptions();
    return removed;
}

int RuleEngineService::removeRulesByOwnerPrefix(const QString& ownerPrefix)
{
    const int swept = m_engine.removeRulesByOwnerPrefix(ownerPrefix);
    if (swept > 0)
        recomputeSubscriptions();
    return swept;
}

// ── Differential subscriptions ───────────────────────────────────────────────

void RuleEngineService::recomputeSubscriptions()
{
    if (m_eventApi == nullptr)
        return;
    QSet<QString> desired = m_engine.subscribedEventIds();
    // Any enabled rule keeps the activity triple alive (idle_for /
    // since_last_motion conditions must keep receiving data even when no
    // rule triggers on the triple's events). Zero enabled rules → zero
    // subscriptions (spec: 零规则=零订阅).
    if (!desired.isEmpty()) {
        desired.insert(QString(kTrackHit));
        desired.insert(QString(kTrackDragEnded));
        desired.insert(QString(kTrackMotionFinished));
    }
    for (const QString& id : desired - m_subscribedIds) {
        const pet::PluginError err =
            m_eventApi->subscribe(id, this, pet::EventPriority::Low);
        if (err != pet::PluginError::Ok)
            LOG_WARN("RuleEngineService: subscribe('{}') failed ({}), "
                     "rules on this event will not fire",
                     id.toStdString(), static_cast<unsigned>(err));
    }
    for (const QString& id : m_subscribedIds - desired)
        m_eventApi->unsubscribe(id, this);
    m_subscribedIds = desired;
}

// ── Roster observation ───────────────────────────────────────────────────────

QStringList RuleEngineService::rosterUuids() const
{
    QStringList out;
    if (m_instanceApi != nullptr) {
        const QVector<pet::InstanceInfo> rows = m_instanceApi->instances();
        for (const pet::InstanceInfo& info : rows)
            out.append(info.uuid);
    }
    return out;
}

void RuleEngineService::rosterChanged()
{
    if (m_instanceApi == nullptr)
        return;
    const QStringList currentList = rosterUuids();
    const QSet<QString> current(currentList.cbegin(), currentList.cend());
    bool mutated = false;

    // Gone uuids: sweep their rules + tracking + runtime cache entry.
    for (const QString& uuid : m_knownUuids - current) {
        const int swept = m_engine.removeRulesByOwnerPrefix(
            QStringLiteral("inst.%1.").arg(uuid));
        m_lastActivityMs.remove(uuid);
        m_lastMotionMs.remove(uuid);
        m_runtimeCache.remove(uuid);
        if (swept > 0) {
            LOG_INFO("RuleEngineService: instance '{}' removed — swept {} "
                     "rule(s)", uuid.toStdString(), swept);
        }
        mutated = true;
    }
    // New uuids: load their instance rules into the live engine.
    for (const QString& uuid : current - m_knownUuids) {
        const int loaded = loadInstanceRulesInto(m_engine, uuid);
        if (loaded > 0) {
            LOG_INFO("RuleEngineService: instance '{}' added — loaded {} "
                     "rule(s)", uuid.toStdString(), loaded);
        }
        mutated = true;
    }
    m_knownUuids = current;
    if (mutated)
        recomputeSubscriptions();
}

void RuleEngineService::instanceStateChanged(const QString& uuid,
                                             const pet::InstanceRuntime& info)
{
    // mountedPackId source: the runtime snapshot is the only SDK surface
    // carrying it (InstanceInfo does not). Cached per uuid; pruned with
    // the roster diff above.
    m_runtimeCache.insert(uuid, info);
}

void RuleEngineService::modelLoadFailed(const QString& uuid,
                                        const QString& error)
{
    Q_UNUSED(uuid);
    Q_UNUSED(error);
    // No rule-engine consumer in S8e (rules trigger on the bus channel,
    // not the observer callback); required pure-virtual no-op.
    LOG_DEBUG("RuleEngineService: modelLoadFailed observed (no consumer)");
}

// ── Event evaluation ─────────────────────────────────────────────────────────

void RuleEngineService::onEvent(pet::Event& event)
{
    const qint64 nowMs = event.timestampMs > 0
        ? event.timestampMs
        : QDateTime::currentMSecsSinceEpoch();

    // Context FIRST — conditions measure the PRE-event tracking state.
    // (Deliberate deviation from the spec's literal listing order: the
    // north-star "hit after long idle" rule needs the idle time BEFORE the
    // triggering hit; updating the tracker first would zero every
    // hit-triggered idle_for condition.)
    const EvalContext ctx = buildContext(event);

    // An earlier (higher-priority) listener already claimed this event:
    // skip every rule. The event still HAPPENED, so the tracker below runs
    // regardless — cancellation suppresses the default reaction, not the
    // fact of the interaction.
    const bool alreadyCancelled = event.cancelled;
    if (alreadyCancelled) {
        LOG_DEBUG("RuleEngineService: event '{}' arrived cancelled — all "
                  "rules skipped", event.eventId.toStdString());
    }

    updateTracking(event, nowMs);

    if (alreadyCancelled)
        return;

    int firedCount = 0;
    const QList<CompiledRule> rules = m_engine.orderedRules();
    for (const CompiledRule& rule : rules) {
        if (rule.triggerEvent != event.eventId)
            continue;
        // Instance-scope rules belong to ONE pet: they only see events
        // carrying their own uuid. Panel rules see every event.
        if (rule.scope == RuleScope::Instance
            && rule.ownerUuid != event.instanceUuid) {
            continue;
        }

        const FireResult result = m_engine.shouldFire(rule, ctx, nowMs);
        if (!result.fire) {
            LOG_DEBUG("RuleEngineService: rule \"{}\" skipped on '{}' ({})",
                      rule.id.toStdString(), event.eventId.toStdString(),
                      result.reason.toStdString());
            continue;
        }

        // Fire bookkeeping FIRST — the cooldown/one-shot window applies
        // even when every action later fails (fail-fast still "counted").
        m_engine.recordFire(rule.id, nowMs);
        ++firedCount;

        if (rule.dryRun) {
            // dry_run = pure preview: NO actions, NO cancel_default (a
            // preview that changed behavior would not be a preview).
            LOG_INFO("RuleEngineService: rule \"{}\" would fire on '{}' "
                     "instance '{}' (dry_run, {} action(s))",
                     rule.id.toStdString(), event.eventId.toStdString(),
                     event.instanceUuid.toStdString(),
                     rule.actions.size());
            continue;
        }

        if (rule.cancelDefault)
            event.cancelled = true;

        LOG_INFO("RuleEngineService: rule fired: id=\"{}\" event=\"{}\" "
                 "instance=\"{}\" actions={}",
                 rule.id.toStdString(), event.eventId.toStdString(),
                 event.instanceUuid.toStdString(), rule.actions.size());
        executeActions(rule, event, ctx);
    }

    if (firedCount > kMaxFiresPerEvent)
        LOG_WARN("RuleEngineService: {} rules fired for a single '{}' "
                 "event (> {}) — rule set looks loopy",
                 firedCount, event.eventId.toStdString(),
                 kMaxFiresPerEvent);
}

EvalContext RuleEngineService::buildContext(const pet::Event& event) const
{
    EvalContext ctx;
    ctx.payload = event.payload;
    ctx.currentHour = QTime::currentTime().hour();
    ctx.lastActivityMs = m_lastActivityMs.value(event.instanceUuid, 0);
    ctx.lastMotionMs = m_lastMotionMs.value(event.instanceUuid, 0);

    // Roster lookup for modelName/connected; a miss keeps the sentinels
    // ("" / false) → fail-closed downstream (not_connected skips all).
    if (m_instanceApi != nullptr) {
        const QVector<pet::InstanceInfo> rows = m_instanceApi->instances();
        for (const pet::InstanceInfo& info : rows) {
            if (info.uuid == event.instanceUuid) {
                ctx.modelName = info.modelName;
                ctx.connected = info.connected;
                break;
            }
        }
    }
    // mountedPackId from the runtime cache ("" when never observed —
    // EvalContext treats that as the legal "nothing mounted" state, the
    // frozen S8d representation).
    const auto cached = m_runtimeCache.constFind(event.instanceUuid);
    if (cached != m_runtimeCache.constEnd())
        ctx.mountedPackId = cached->mountedPackId;
    return ctx;
}

void RuleEngineService::updateTracking(const pet::Event& event, qint64 nowMs)
{
    if (event.instanceUuid.isEmpty())
        return; // panel-level events carry no per-pet activity
    if (event.eventId == kTrackHit || event.eventId == kTrackDragEnded) {
        m_lastActivityMs[event.instanceUuid] =
            qMax(m_lastActivityMs.value(event.instanceUuid, 0), nowMs);
    } else if (event.eventId == kTrackMotionFinished) {
        m_lastMotionMs[event.instanceUuid] =
            qMax(m_lastMotionMs.value(event.instanceUuid, 0), nowMs);
        if (!isIdleMotionGroup(
                event.payload.value(QStringLiteral("group")).toString())) {
            m_lastActivityMs[event.instanceUuid] =
                qMax(m_lastActivityMs.value(event.instanceUuid, 0), nowMs);
        }
    }
}

void RuleEngineService::seedActivityTrackingForTest(const QString& uuid,
                                                    qint64 lastActivityMs,
                                                    qint64 lastMotionMs)
{
    m_lastActivityMs.insert(uuid, lastActivityMs);
    m_lastMotionMs.insert(uuid, lastMotionMs);
}

// ── Action dispatch ──────────────────────────────────────────────────────────

void RuleEngineService::executeActions(const CompiledRule& rule,
                                       const pet::Event& event,
                                       const EvalContext& ctx)
{
    for (const RuleAction& action : rule.actions) {
        if (!executeOneAction(rule, action, event, ctx)) {
            LOG_WARN("RuleEngineService: rule \"{}\" action sequence "
                     "aborted (fail-fast) after action kind {}",
                     rule.id.toStdString(),
                     static_cast<int>(action.kind));
            return;
        }
    }
}

bool RuleEngineService::pluginActionDenied(const CompiledRule& rule,
                                           const RuleAction& action) const
{
    // Host rules (panel kv / instance blobs / the raw host channel) are
    // never gated — the host IS the host.
    if (!rule.id.startsWith(QStringLiteral("plugin.")))
        return false;
    // S8f design ruling: ONLY the two tuning-write channels are gated.
    // play_pack_audio is already mount-gated, notify_bubble is a
    // notification surface, publish_event is host-identity (prefix
    // compile-checked in S8d) — none carry an owner write.
    if (action.kind != RuleActionKind::PlayMotion
        && action.kind != RuleActionKind::SetExpression)
        return false;
    // Emission-time LIVE checks — a rule registered while granted must
    // stop acting the moment either flips (a lingered rule surface
    // cannot outlive its grant).
    if (m_ownerWriteEnabled && !m_ownerWriteEnabled()) {
        LOG_WARN("RuleEngineService: rule \"{}\" action denied — the "
                 "plugin write switch is off",
                 rule.id.toStdString());
        return true;
    }
    if (!m_ownerCapabilityQuery
        || !m_ownerCapabilityQuery(rule.id, pet::kCapabilityInstanceTuning)) {
        LOG_WARN("RuleEngineService: rule \"{}\" action denied — the "
                 "owner plugin lacks the instance_tuning capability "
                 "(unknown owner or capability query unwired)",
                 rule.id.toStdString());
        return true;
    }
    return false;
}

bool RuleEngineService::executeOneAction(const CompiledRule& rule,
                                         const RuleAction& action,
                                         const pet::Event& event,
                                         const EvalContext& ctx)
{
    switch (action.kind) {
    case RuleActionKind::PlayMotion: {
        if (pluginActionDenied(rule, action))
            return false;
        if (m_tuningApi == nullptr) {
            LOG_WARN("RuleEngineService: play_motion — no tuning API wired");
            return false;
        }
        const pet::PluginError err = m_tuningApi->playMotion(
            event.instanceUuid, action.group, action.index);
        if (err != pet::PluginError::Ok) {
            LOG_WARN("RuleEngineService: playMotion('{}', \"{}\", {}) "
                     "rejected ({})",
                     event.instanceUuid.toStdString(),
                     action.group.toStdString(), action.index,
                     static_cast<unsigned>(err));
            return false;
        }
        return true;
    }
    case RuleActionKind::SetExpression: {
        if (pluginActionDenied(rule, action))
            return false;
        if (m_tuningApi == nullptr) {
            LOG_WARN("RuleEngineService: set_expression — no tuning API "
                     "wired");
            return false;
        }
        const pet::PluginError err = m_tuningApi->setExpression(
            event.instanceUuid, action.id);
        if (err != pet::PluginError::Ok) {
            LOG_WARN("RuleEngineService: setExpression('{}', \"{}\") "
                     "rejected ({})",
                     event.instanceUuid.toStdString(),
                     action.id.toStdString(), static_cast<unsigned>(err));
            return false;
        }
        return true;
    }
    case RuleActionKind::PlayPackAudio:
        return executePackAudioAction(action, event, ctx);
    case RuleActionKind::NotifyBubble: {
        if (!m_bubbleSink) {
            LOG_WARN("RuleEngineService: notify_bubble — no sink wired "
                     "(PanelUiBoot late injection missing)");
            return false;
        }
        const int duration = action.durationMs > INT_MAX
            ? INT_MAX
            : static_cast<int>(action.durationMs);
        m_bubbleSink(action.text, duration);
        return true;
    }
    case RuleActionKind::PublishEvent: {
        if (m_eventApi == nullptr) {
            LOG_WARN("RuleEngineService: publish_event — no event API wired");
            return false;
        }
        // Host identity (the "pet.rules." prefix was compile-checked in
        // S8d); the payload is the action's verbatim object.
        pet::Event out;
        out.eventId = action.event;
        out.instanceUuid = event.instanceUuid;
        out.publisherId = QString(pet::kEventPublisherHost);
        out.payload = action.payload;
        const pet::PluginError err = m_eventApi->publish(out);
        if (err != pet::PluginError::Ok) {
            LOG_WARN("RuleEngineService: publish('{}') rejected ({}) — "
                     "Busy means the re-entrancy cap stopped a loop",
                     action.event.toStdString(), static_cast<unsigned>(err));
            return false;
        }
        return true;
    }
    }
    return false; // closed enum — unreachable
}

bool RuleEngineService::executePackAudioAction(const RuleAction& action,
                                               const pet::Event& event,
                                               const EvalContext& ctx)
{
    // Mounted gate (spec): the snapshot's mounted pack must BE the action's
    // pack — audio for a pack the user unmounted must never play. NOT
    // owner-gated (S8f design ruling: the mount check is this rule's
    // authority — a plugin rule cannot play audio for a pack the user has
    // not mounted on the target instance, which is the meaningful bound).
    if (ctx.mountedPackId != action.pack) {
        LOG_WARN("RuleEngineService: play_pack_audio — pack '{}' is not "
                 "mounted on instance '{}' (\"{}\" is)",
                 action.pack.toStdString(),
                 event.instanceUuid.toStdString(),
                 ctx.mountedPackId.toStdString());
        return false;
    }
    const QString packDir = resolvePackDir(action.pack);
    if (packDir.isEmpty()) {
        LOG_WARN("RuleEngineService: play_pack_audio — pack '{}' not "
                 "found in the scan sources", action.pack.toStdString());
        return false;
    }
    const std::optional<VoicePackInfo> info = parseMetaMko(packDir);
    if (!info.has_value()) {
        LOG_WARN("RuleEngineService: play_pack_audio — meta.mko of '{}' "
                 "unparseable", action.pack.toStdString());
        return false;
    }
    // buildAudioOnlyCommand semantics (MBE precedent, zero changes there):
    // random audio-only action of the group, path resolved against the
    // pack dir, play_audio envelope at volume 1.0.
    const MountedBehaviorEngine engine(&*info);
    const std::optional<Envelope> command =
        engine.buildAudioOnlyCommand(action.group);
    if (!command.has_value()) {
        LOG_WARN("RuleEngineService: play_pack_audio — no audio-only "
                 "action in group '{}' of pack '{}'",
                 action.group.toStdString(), action.pack.toStdString());
        return false;
    }
    if (!m_packAudioChannel) {
        LOG_WARN("RuleEngineService: play_pack_audio — no send channel "
                 "wired");
        return false;
    }
    const QJsonObject wire = serialize(*command).object();
    return m_packAudioChannel(event.instanceUuid, wire) == 0;
}

QString RuleEngineService::resolvePackDir(const QString& packId) const
{
    // packId is the DIRECTORY NAME (SDK-wide identity) — same resolution
    // discipline as TuningApiImpl::resolvePackDir: dual-source scan, user
    // packs win collisions, a caller path is never interpreted.
    const QString rendererDir = !m_packRendererDir.isEmpty()
        ? m_packRendererDir
        : defaultRendererDir();
    const QString userDir = !m_packUserDir.isEmpty()
        ? m_packUserDir
        : ConfigDir::userVoicePacksDir();
    const QStringList dirs = scanAvailableVoicePacks(rendererDir, userDir);
    for (const QString& dir : dirs) {
        if (QFileInfo(dir).fileName() == packId)
            return dir;
    }
    return QString();
}

} // namespace core
