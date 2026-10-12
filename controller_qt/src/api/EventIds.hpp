#pragma once

// Plugin SDK — host event-bus channel IDs + syntax rules (S8a, v1.5).
//
// This DIRECTORY IS THE CONTRACT for pet.events channel names and payload
// shapes (the same role docs/protocol/events.md plays for the WS wire
// events; the bus is a separate, in-process notification channel, and the
// two vocabularies are deliberately distinct — a bus ID names a CHANNEL,
// not a WS event). Rules frozen at v1.5:
//   - fields are ADD-ONLY: never remove a field, never rename a field;
//     new fields may be appended to any payload (consumers must ignore
//     unknown keys);
//   - optional/absent values use SENTINELS, never absent keys: numeric
//     fields carry -1, string fields carry "" (the same discipline as
//     MonitorSample's -1 GPU sentinel — std::optional must not cross the
//     boundary, §B.3);
//   - event IDs are NEVER renamed or removed (a plugin compiled against
//     kEventInstanceHit must keep receiving hits forever).
//
// Payload field tables below follow docs/protocol/events.md's column
// style (字段/类型/语义/哨兵) so the two documents read alike.

#include <QLatin1String>
#include <QtGlobal>

namespace pet {

// ── Event ID syntax rules (C.1; enforced by core::EventBus at subscribe
// AND publish, and by the per-plugin publish proxy's prefix check) ────────
// An event ID is a dot-separated channel name where EVERY segment is
// non-empty and only [a-z0-9_] (lowercase). Wildcards ('*') and any other
// character are rejected with PluginError::InvalidArgument — there is no
// pattern subscription in v1.5.
inline constexpr int kEventIdMinSegments = 3;

// Plugin publish namespace: a PLUGIN may only publish channels under
// "plugin.<ownId>.*" (the per-context EventApiProxy enforces this —
// anti-forgery: a plugin must not be able to impersonate host channels
// like pet.instance.hit). The HOST may publish any syntactically legal ID.
inline constexpr QLatin1StringView kEventPluginNamespace =
    QLatin1StringView("plugin.");

// publisherId the host stamps on its own emissions (emission sites set it
// explicitly; the shared EventApiImpl does NOT overwrite publisherId —
// only the per-plugin proxy forces its own id).
inline constexpr QLatin1StringView kEventPublisherHost =
    QLatin1StringView("host");

// ── Instance-domain channels (renderer-lifecycle mirrors) ─────────────────
// Payloads mirror the WS events (docs/protocol/events.md) field-for-field
// unless a host-side supplement is noted. instanceUuid in the Event header
// carries the instance identity; payloads never duplicate it.

// Source: WS `ready`. Payload (verbatim WS payload copy):
//   | version      | string   | renderer protocol version ("1.0.0")   |
//   | capabilities | string[] | renderer capability list (["live2d"]) |
inline constexpr QLatin1StringView kEventInstanceReady =
    QLatin1StringView("pet.instance.ready");

// Source: WS `hit`. CANCELLABLE (the designated pre-action gate — S8b
// consumers may cancel the hit reaction; S8a publishes but nobody reads
// event.cancelled yet). Payload (keys verbatim per events.md §hit):
//   | area_id | string | hit-area identifier ("head"/"body", lowercase) |
//   | x       | double | mouse X in window client coordinates           |
//   | y       | double | mouse Y in window client coordinates           |
//   | button  | int    | mouse button (0 = left)                        |
inline constexpr QLatin1StringView kEventInstanceHit =
    QLatin1StringView("pet.instance.hit");

// Source: WS `motion_started`. Payload:
//   | group | string | motion group name          |
//   | index | int    | index within the group     |
inline constexpr QLatin1StringView kEventInstanceMotionStarted =
    QLatin1StringView("pet.instance.motion_started");

// Source: WS `motion_finished` (BOTH wire variants normalized to ONE
// shape — the discriminator is which half carries its sentinel):
//   | group       | string | group name; "" for the play_motion_ext variant |
//   | index       | int    | group index; -1 for the play_motion_ext variant |
//   | motion_path | string | external motion file path; "" for the
//                           play_motion variant                             |
inline constexpr QLatin1StringView kEventInstanceMotionFinished =
    QLatin1StringView("pet.instance.motion_finished");

// Source: the idle Scheduler's trigger callback (NOT a WS event). Fires
// right before the idle play_motion is sent. CANCELLABLE (S8b: a consumer
// may cancel the idle motion; S8a does not read it). Payload:
//   | group | string | configured idle group (default "Idle") |
//   | index | int    | randomized 0..count-1                  |
inline constexpr QLatin1StringView kEventInstanceIdleTick =
    QLatin1StringView("pet.instance.idle_tick");

// Source: WS `drag_start`. Payload:
//   | x | double | drag-start mouse X (window client coordinates) |
//   | y | double | drag-start mouse Y                             |
inline constexpr QLatin1StringView kEventDragStarted =
    QLatin1StringView("pet.instance.drag_started");

// Source: WS `drag_end`. Published AFTER the position is persisted
// (post-commit). Payload:
//   | x         | double | release mouse X (window client coordinates) |
//   | y         | double | release mouse Y                            |
//   | window_x  | int    | final window top-left X (screen pixels)    |
//   | window_y  | int    | final window top-left Y                    |
inline constexpr QLatin1StringView kEventDragEnded =
    QLatin1StringView("pet.instance.drag_ended");

// ── Model-domain channels ──────────────────────────────────────────────────

// Source: WS `model_loaded` + HOST-SIDE SUPPLEMENT. The wire payload's
// motions/expressions are ALWAYS empty (events.md §2 "模型能力发现"), so
// the bus payload carries the controller-parsed metadata instead (the
// same ModelInfoParser data the detail page shows). Payload:
//   | model_id      | string   | model short name ("Hiyori")               |
//   | motion_groups | string[] | parsed motion-group names (host-supplied;
//                               empty when the .model3.json failed to parse)|
//   | expressions   | string[] | parsed expression names (host-supplied;
//                               empty sentinel, same rule)                  |
inline constexpr QLatin1StringView kEventModelLoaded =
    QLatin1StringView("pet.model.loaded");

// Source: WS `model_load_failed`. Published after the failure state is
// recorded + the modelLoadFailed signal fires (post-commit). Payload:
//   | error_code    | int    | renderer error code                      |
//   | error_message | string | renderer message; "" when the wire event
//                             carried none (verbatim — the UI-side
//                             degraded string is a QML concern)         |
inline constexpr QLatin1StringView kEventModelLoadFailed =
    QLatin1StringView("pet.model.load_failed");

// ── Pack-domain channels ───────────────────────────────────────────────────

// Source: InstanceSession::mountVoicePack at the mountedVoicePackChanged
// NOTIFY point (fires only on a REAL mount transition — re-mounting the
// same pack re-parses but is not a change). Payload:
//   | pack_id | string | pack DIRECTORY name (SDK-wide pack identity) |
inline constexpr QLatin1StringView kEventPackMounted =
    QLatin1StringView("pet.pack.mounted");

// Source: InstanceSession::unmountVoicePack at the same NOTIFY point.
// Payload: identical shape to pet.pack.mounted.
inline constexpr QLatin1StringView kEventPackUnmounted =
    QLatin1StringView("pet.pack.unmounted");

} // namespace pet
