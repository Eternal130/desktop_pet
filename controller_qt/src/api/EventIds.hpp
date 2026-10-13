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

#include <array>

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
// right before the idle play_motion is sent. CANCELLABLE — S8b honors it:
// a listener that sets event.cancelled skips the idle play_motion (the
// poster reads the final value after publish returns). Payload:
//   | group | string | configured idle group (default "Idle") |
//   | index | int    | randomized 0..count-1                  |
inline constexpr QLatin1StringView kEventInstanceIdleTick =
    QLatin1StringView("pet.instance.idle_tick");

// Source: WS `audio_finished` (docs/protocol/events.md §15 — an audio
// track reached its NATURAL end; stop_audio and preemption deliberately
// never emit it). Pure telemetry on the panel side (S8b wires the
// publish-only handler; no local behavior). NOT cancellable. Payload
// (verbatim WS payload copy):
//   | audio_path | string | the audio file path as passed to play_audio /
//                           play_motion_ext (echoed verbatim)             |
inline constexpr QLatin1StringView kEventInstanceAudioFinished =
    QLatin1StringView("pet.instance.audio_finished");

// Source: WS `expression_applied` (docs/protocol/events.md §16 — the
// set_expression SUCCESS receipt; unknown ids and unloaded models
// deliberately never emit it, the caller infers "not applied" from the
// absence of a receipt). Pure telemetry on the panel side (v1.8 wires
// the publish-only handler in the audio_finished shape). NOT
// cancellable. Payload (verbatim WS payload copy):
//   | expression_id | string | the applied expression id (echoed from
//                              the set_expression command payload)      |
inline constexpr QLatin1StringView kEventInstanceExpressionApplied =
    QLatin1StringView("pet.instance.expression_applied");

// Source: WS `motion_progress` (docs/protocol/events.md §17 — a motion
// playback progress snapshot, renderer-throttled to at most one per
// 500ms per playing motion; short motions may never emit one). Pure
// telemetry on the panel side (v1.8 wires the publish-only handler —
// high-frequency by design, the override only passes the payload
// through, zero additional processing). NOT cancellable. TWO wire
// variants, mirrored verbatim (the motion_finished §5/§10 dual-variant
// convention):
//   play_motion variant:
//   | group    | string | motion group name (same source/value as
//                         motion_started / motion_finished)             |
//   | index    | int    | index within the group                        |
//   play_motion_ext variant:
//   | motion_path | string | external motion file path (same source as
//                            the motion_finished path variant)          |
//   both variants:
//   | progress | double | OPTIONAL — playback progress 0.0..1.0 from
//                         the Cubism motion queue entry's own timeline.
//                         OMITTED when the duration is unknown (looping
//                         motions / duration not yet resolved): the
//                         renderer never fabricates a value, and the bus
//                         payload mirrors that omission verbatim (this
//                         is the ONE documented absent-key exception —
//                         the wire contract owns the shape, the host
//                         copies it byte-for-byte).                      |
inline constexpr QLatin1StringView kEventInstanceMotionProgress =
    QLatin1StringView("pet.instance.motion_progress");

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

// ── Instance-domain channels, v1.7 additions (S9 catalog completion) ───────

// Source: WS `error` (docs/protocol/events.md §9 — renderer runtime error
// report). Publish-only override of the EventRegistry default (the
// audio_finished pattern: the default's INFO log is reproduced verbatim).
// NOT cancellable. Payload (verbatim WS payload copy):
//   | error_code    | int    | renderer error code (error-codes.md) |
//   | error_message | string | renderer error description           |
inline constexpr QLatin1StringView kEventInstanceError =
    QLatin1StringView("pet.instance.error");

// Source: HOST POINT — the dialogue-sink inflow inside the session's
// MountedBehaviorAdapter (the earliest single point where the instance
// uuid, the text and the display duration coexist; InstanceManager /
// PanelUiBoot merely forward the sink onward to the bubble stream).
// DUAL-CHANNEL SPLIT: the bubble stream remains the DISPLAY channel;
// this event is the OBSERVABILITY channel (plugins / rules may react to
// spoken lines without touching the display). NOT cancellable. Payload:
//   | text        | string | the dialogue line (the .mko action's doc) |
//   | duration_ms | int    | display duration handed to the sink; 0 = |
//                           | the stream applies its own default      |
inline constexpr QLatin1StringView kEventInstanceDialogue =
    QLatin1StringView("pet.instance.dialogue");

// Source: HOST POINT — the RestartController::retry wiring inside the
// InstanceSession constructor (the retry just FIRED; the relaunch start()
// runs right after the publish). NOT cancellable. Payload:
//   | attempt  | int | 1-based retry number (RestartController::attempts) |
//   | delay_ms | int | backoff delay that was armed for THIS retry        |
inline constexpr QLatin1StringView kEventInstanceRestartScheduled =
    QLatin1StringView("pet.instance.restart_scheduled");

// Source: HOST POINT — the RestartController::gaveUp wiring (fired when
// the attempt ceiling kMaxAttempts is reached; the session has already
// flipped to terminal error). NOT cancellable. Payload:
//   | attempts | int | final attempt count (= RestartController::kMaxAttempts) |
inline constexpr QLatin1StringView kEventInstanceGaveUp =
    QLatin1StringView("pet.instance.gave_up");

// Source: WS `layout_changed` (docs/protocol/events.md §12 — user layout
// edit via Shift+drag / Shift+scroll). Published AFTER the layout is
// persisted (post-commit). NOT cancellable. Payload (verbatim WS payload
// copy):
//   | offset_x | float | horizontal offset |
//   | offset_y | float | vertical offset   |
//   | scale    | float | scale factor      |
inline constexpr QLatin1StringView kEventInstanceLayoutChanged =
    QLatin1StringView("pet.instance.layout_changed");

// Source: WS `window_resized` (docs/protocol/events.md §13 — Ctrl+scroll
// center-preserving window resize). Published AFTER the window geometry
// is persisted (post-commit). NOT cancellable. Payload (verbatim WS
// payload copy):
//   | window_width  | int | new window width (px)          |
//   | window_height | int | new window height (px)         |
//   | window_x      | int | window top-left X (screen px)  |
//   | window_y      | int | window top-left Y (screen px)  |
inline constexpr QLatin1StringView kEventInstanceWindowResized =
    QLatin1StringView("pet.instance.window_resized");

// ── Panel-domain channels (v1.7 — first host use of the reserved
//    pet.panel.* domain; these are PANEL-LEVEL events: instanceUuid = ""
//    in the Event header) ───────────────────────────────────────────────────

// Source: HOST POINT — SettingsApiImpl's four typed setters, each AFTER a
// successful panel_config persist (post-commit). Panel-level. NOT
// cancellable. Payload:
//   | field | string       | snake_case field name: close_action /
//   |       |              | confirm_on_exit / start_minimized /
//   |       |              | default_model_name                       |
//   | value | string|bool  | the persisted value (typed as written)    |
// (autoLaunchSystem deliberately does NOT emit — it walks the host-direct
// path outside ISettingsApi; see EventCatalog.cpp / docs/bus-events.md.)
inline constexpr QLatin1StringView kEventPanelSettingsChanged =
    QLatin1StringView("pet.panel.settings_changed");

// Source: HOST POINT — PluginHost::initializeAll right after a plugin's
// transition to Started. Panel-level. NOT cancellable. Payload:
//   | plugin_id | string | the manifest plugin id (reverse-domain) |
inline constexpr QLatin1StringView kEventPanelPluginLoaded =
    QLatin1StringView("pet.panel.plugin_loaded");

// Source: HOST POINT — PluginHost::shutdownAll after a plugin's
// event/rule unload sweeps complete (same position). Panel-level. NOT
// cancellable. Payload: identical shape to pet.panel.plugin_loaded.
inline constexpr QLatin1StringView kEventPanelPluginUnloaded =
    QLatin1StringView("pet.panel.plugin_unloaded");

// ── Completeness array (v1.7; 23 ids since v1.8) ────────────────────────────
// EVERY host event id, in catalog order. makeKnownEventTable (the rule
// engine's cancel_default static-WARN table) and the completeness-guard
// test both check against THIS array — an audio_finished-style gap
// (constant added, table entry forgotten) turns the test red.
// 新增事件必须同步本数组 (adding an event without extending this array
// fails EventBusEmissionTest::testKnownEventTableCoversCatalog).
inline constexpr std::array<QLatin1StringView, 23> kAllEventIds = {
    kEventInstanceReady,
    kEventInstanceHit,
    kEventInstanceMotionStarted,
    kEventInstanceMotionFinished,
    kEventInstanceIdleTick,
    kEventInstanceAudioFinished,
    kEventInstanceExpressionApplied,
    kEventInstanceMotionProgress,
    kEventDragStarted,
    kEventDragEnded,
    kEventInstanceError,
    kEventInstanceDialogue,
    kEventInstanceRestartScheduled,
    kEventInstanceGaveUp,
    kEventInstanceLayoutChanged,
    kEventInstanceWindowResized,
    kEventModelLoaded,
    kEventModelLoadFailed,
    kEventPackMounted,
    kEventPackUnmounted,
    kEventPanelSettingsChanged,
    kEventPanelPluginLoaded,
    kEventPanelPluginUnloaded,
};

} // namespace pet
