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
#include <QJsonValue>

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

// pet.instance.audio_finished — VERBATIM copy of the WS `audio_finished`
// payload (audio_path; events.md §15 — natural playback end only).
QJsonObject makeAudioFinishedPayload(const QJsonObject& wsPayload);

// pet.instance.expression_applied — {expression_id} (events.md §16 —
// the set_expression success receipt, key verbatim).
QJsonObject makeExpressionAppliedPayload(const QString& expressionId);

// pet.instance.motion_progress, play_motion variant: {group, index} +
// progress — the progress key is inserted ONLY when hasProgress is true
// (the wire omits it for unknown-duration motions: looping / duration
// not yet resolved; events.md §17's optional field is mirrored verbatim,
// never fabricated).
QJsonObject makeMotionProgressPayload(const QString& group, int index,
                                      bool hasProgress, double progress);
// pet.instance.motion_progress, play_motion_ext variant:
// {motion_path} + optional progress (same omission rule). Overload pair
// in the makeMotionFinishedPayload shape — the emission site already
// knows the wire variant.
QJsonObject makeMotionProgressPayload(const QString& motionPath,
                                      bool hasProgress, double progress);

// pet.instance.focus_changed — {focused} (events.md §18 verbatim — the
// GLFW window-focus callback half; single-key echo in the
// makeExpressionAppliedPayload shape).
QJsonObject makeFocusChangedPayload(bool focused);

// pet.instance.drag_started — {x, y}.
QJsonObject makeDragStartedPayload(double x, double y);

// pet.instance.drag_ended — {x, y, window_x, window_y}.
QJsonObject makeDragEndedPayload(double x, double y, int windowX,
                                 int windowY);

// ── Instance domain, v1.7 additions (S9 catalog completion) ─────────────────
// (docs/protocol/events.md §9/§12/§13 wire mirrors + host-side lifecycle /
// dialogue observability channels; see api/EventIds.hpp for the frozen
// field tables.)

// pet.instance.error — VERBATIM copy of the WS `error` payload
// (error_code + error_message; events.md §9).
QJsonObject makeErrorPayload(const QJsonObject& wsPayload);

// pet.instance.dialogue — {text, duration_ms} exactly as handed to the
// dialogue sink (duration_ms 0 = the bubble stream applies its default).
QJsonObject makeDialoguePayload(const QString& text, int durationMs);

// pet.instance.restart_scheduled — {attempt, delay_ms} read from the
// RestartController at the retry wiring point (attempt is 1-based).
QJsonObject makeRestartScheduledPayload(int attempt, int delayMs);

// pet.instance.gave_up — {attempts} (the final attempt count).
QJsonObject makeGaveUpPayload(int attempts);

// pet.instance.layout_changed — VERBATIM copy of the WS `layout_changed`
// payload (offset_x/offset_y/scale; events.md §12 — already snake_case).
QJsonObject makeLayoutChangedPayload(const QJsonObject& wsPayload);

// pet.instance.window_resized — VERBATIM copy of the WS `window_resized`
// payload (window_width/window_height/window_x/window_y; events.md §13).
QJsonObject makeWindowResizedPayload(const QJsonObject& wsPayload);

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

// ── Panel domain (v1.7 — panel-level events, instanceUuid = "") ────────────

// pet.panel.settings_changed — {field, value}. `field` is the snake_case
// PanelConfig field name (close_action / confirm_on_exit / start_minimized /
// default_model_name / auto_launch_system — the last joined in v1.9 via
// PanelConfigController's host-direct path, see below); `value` carries the
// persisted value typed as written (string or bool — QJsonValue keeps the
// type). v1.9: autoLaunchSystem now EMITS from its host-direct setter's
// success path (the write itself still deliberately stays outside
// ISettingsApi — OS-level autostart is a host-shell capability; only the
// bus observability was added).
QJsonObject makeSettingsChangedPayload(const QString& field,
                                       const QJsonValue& value);

// pet.panel.plugin_loaded / pet.panel.plugin_unloaded — {plugin_id} (the
// manifest id, reverse-domain).
QJsonObject makePluginLifecyclePayload(const QString& pluginId);

// ── Panel domain, v1.9 additions (window/tray observability) ────────────────

// pet.panel.window_state — {state, visible}. `state` is the projected
// QQuickWindow::Visibility name ("normal"/"minimized"/"hidden"/"maximized"
// — see api/EventIds.hpp for the defensive-fallback table); `visible` is
// window->isVisible() sampled at signal time.
QJsonObject makeWindowStatePayload(const QString& state, bool visible);

// pet.panel.tray_activated — {action} (snake_case action identifier:
// toggle_visibility / settings / quit / context_menu).
QJsonObject makeTrayActivatedPayload(const QString& action);

} // namespace core
