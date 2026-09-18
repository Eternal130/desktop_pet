#pragma once

#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

struct Envelope;

// InteractionHandler (Phase 5 todo 9) — QObject that translates an
// inbound `hit` event (renderer → controller) into an outbound `play_motion`
// command (controller → renderer), looking up the motion group via a 3-tier
// case-folding strategy against the model config's hitActions map.
//
// Lookup order for `payload.area_id` (interface.md §F):
//   1. EXACT match in modelConfig.hitActions[area_id]      ("Body" → "Body")
//   2. CAPITALIZED form: area_id[0].toUpper() + rest.lower ("head" → "Head",
//      "HEAD" → "Head", "hEaD" → "Head" — same as Java's capitalize())
//   3. DEFAULT_MAPPINGS lowercased:
//        "head" → {group:"TapHead", priority:2}
//        "body" → {group:"TapBody", priority:2}
//      (priority 2 = PriorityNormal per interface.md §F)
//   4. Still nothing → no-op (debug log). NEVER throws.
//
// The renderer sends area_id verbatim from the model's .model3.json HitAreas
// (Hiyori has only "Body"). The model_config.json (optional, per-model) may
// use either casing — case-folding makes both work.
//
// Standalone for todo 9 (tested independently via InteractionHandlerTest).
// Todo 10 wires this into InstanceSession's `hit` event handler (after
// checking MountedBehaviorEngine in Phase 8 — todo 20).

// A single hit-area → motion mapping. Plain struct (mirrors Java record
// `HitAction(String motionGroup, int priority)`). `group` is the motion group
// name to play (e.g. "TapHead"); `priority` defaults to 2 (PriorityNormal).
struct HitAction {
    QString group;
    int priority = 2;
};

// Per-model interaction configuration. Loaded from
// `<Resources>/Models/<name>/model_config.json` if present (optional; the
// handler works fine with a default-constructed empty ModelConfig).
//   - hitActions:        area_id → HitAction (custom per-model overrides)
//   - idleMotions:       motion group names for the idle Scheduler (todo 10)
//   - defaultExpression: expression id to apply on model load (todo 10)
struct ModelConfig {
    QMap<QString, HitAction> hitActions;
    QStringList idleMotions;
    QString defaultExpression;
};

class InteractionHandler : public QObject {
    Q_OBJECT

public:
    // messageSender: WS send callback. Receives the serialized play_motion
    // JSON (compact). The caller owns the WS connection — this class never
    // touches WsServer directly (mirrors Java's Consumer<String>).
    using MessageSender = std::function<void(const QString&)>;

    explicit InteractionHandler(MessageSender messageSender,
                                QObject* parent = nullptr);
    ~InteractionHandler() override;

    // Replace the model config (called when a new model loads). The handler
    // stores a copy; subsequent handleHitEvent() calls consult the new map.
    void setModelConfig(const ModelConfig& config);

    // Process an inbound `hit` event. Extracts payload.area_id, resolves a
    // HitAction via the case-folding strategy above, builds a play_motion
    // command `{group, index:0, priority}` inline (todo 16 adds
    // Protocol::buildPlayMotion), serializes compact, and invokes the
    // messageSender. No-op (debug log) on missing/empty/unknown area_id —
    // NEVER throws.
    void handleHitEvent(const Envelope& hitEvent);

private:
    // Resolve a HitAction for `areaId` via the 3-tier case-folding strategy.
    // Returns std::nullopt when nothing matches (caller logs + no-ops).
    std::optional<HitAction> findHitAction(const QString& areaId) const;

    // Java's capitalize(): first char toUpper(), rest toLower(). "head" →
    // "Head", "HEAD" → "Head", "hEaD" → "Head". Empty input → empty output.
    static QString capitalize(const QString& s);

    MessageSender m_messageSender;
    ModelConfig m_modelConfig;
};
