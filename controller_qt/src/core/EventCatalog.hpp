#pragma once

// EventCatalog (S8a, v1.5) — typed payload builders for every host
// emission point on the event bus (the same discipline as the Protocol
// command factories: ONE place owns the exact payload keys; emission
// sites are FORBIDDEN from hand-writing key names).
//
// Key spelling is verbatim from the field tables in api/EventIds.hpp
// (which mirror docs/protocol/events.md where the source is a WS event).
// All keys are snake_case; every optional value uses its sentinel
// (-1 / "") — never an absent key.
//
// Pure functions; never throw; Qt6::Core only.

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace core {

// ── Instance domain ─────────────────────────────────────────────────────────

// pet.instance.ready — VERBATIM copy of the WS `ready` payload (version +
// capabilities; the bus adds nothing, the host removes nothing).
QJsonObject makeReadyPayload(const QJsonObject& wsPayload);

// pet.instance.hit — keys per events.md §hit, letter for letter.
QJsonObject makeHitPayload(const QString& areaId, double x, double y,
                           int button);

// pet.instance.motion_started — {group, index}.
QJsonObject makeMotionStartedPayload(const QString& group, int index);

// pet.instance.motion_finished, variant A (play_motion): {group, index,
// motion_path:""} — the motion_path sentinel marks the wire variant.
QJsonObject makeMotionFinishedPayload(const QString& group, int index);
// pet.instance.motion_finished, variant B (play_motion_ext):
// {group:"", index:-1, motion_path} — the group/index sentinels mark the
// wire variant. Both builders emit the SAME three keys so consumers see
// one normalized shape (the -1 / "" sentinels are the discriminator).
QJsonObject makeMotionFinishedPayload(const QString& motionPath);

// pet.instance.idle_tick — {group, index} as handed to the scheduler
// callback.
QJsonObject makeIdleTickPayload(const QString& group, int index);

// pet.instance.drag_started — {x, y}.
QJsonObject makeDragStartedPayload(double x, double y);

// pet.instance.drag_ended — {x, y, window_x, window_y}.
QJsonObject makeDragEndedPayload(double x, double y, int windowX,
                                 int windowY);

// ── Model domain ────────────────────────────────────────────────────────────

// pet.model.loaded — model_id + the HOST-SUPPLIED metadata halves (the
// wire event's motions/expressions are always empty; these lists come
// from ModelInfoParser and degrade to empty lists on a parse failure —
// the sentinel, never an absent key).
QJsonObject makeModelLoadedPayload(const QString& modelId,
                                   const QStringList& motionGroups,
                                   const QStringList& expressions);

// pet.model.load_failed — verbatim wire halves.
QJsonObject makeModelLoadFailedPayload(int errorCode,
                                       const QString& errorMessage);

// ── Pack domain ─────────────────────────────────────────────────────────────

// pet.pack.mounted / pet.pack.unmounted — {pack_id} where packId is the
// pack DIRECTORY name (the SDK-wide pack identity).
QJsonObject makePackPayload(const QString& packId);

} // namespace core
