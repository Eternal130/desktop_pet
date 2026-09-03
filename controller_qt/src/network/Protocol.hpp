#pragma once

#include "network/Envelope.hpp"

#include <QJsonArray>
#include <QString>

// Protocol typed factories (T9).
//
// Layer on top of Envelope.hpp's thin createCommand() that bakes the EXACT
// payload fields each startup-salvo command requires per interface.md §5.
// Consumed by T16 (startup salvo: ready → 7 commands → model_loaded →
// set_hit_areas). Every builder:
//   - delegates to createCommand("action", payload) so id/timestamp/type come
//     from the single source of truth in Envelope.cpp (UUIDv4 + current ms);
//   - emits snake_case keys matching interface.md §5 verbatim;
//   - never emits response fields (those live only on type=="response").
//
// Scope: the 7 salvo commands + set_hit_areas + shutdown (Phase 0-4 T9), PLUS
// the runtime command factories (T16) — play_motion / play_motion_ext /
// stop_motion / set_expression / play_audio / stop_audio / get_stats /
// get_layout / reset_layout. The runtime factories are required by Wave 8
// features: MonitorPage (get_stats), Voice Pack / MountedBehaviorEngine
// (play_motion_ext), LayoutPage (get_layout / reset_layout).
//
// Subtitle commands (show_subtitle / hide_subtitle / set_subtitle_style /
// set_subtitle_adjust_mode / set_subtitle_layout) were REMOVED in the
// bubble-stream switch phase: voice-pack dialogue text now flows through the
// controller-side notification stream, never through renderer subtitles.
//
// NOT introduced: set_scale. It is a renderer-side compatibility alias for
// set_layout's scale axis (implemented, responds, clamps 0.1–5.0); this
// controller carries scale via set_layout's `scale` field instead — one
// command, all layout axes. Also NOT introduced: response factories — the
// renderer emits those, the controller only builds commands here.

namespace Protocol {

// ── Startup salvo (7 commands, sent after `ready`, blueprint §6.1) ─────────

// §A.1 — load/replace model. modelPath is the short name ("Hiyori"); the
// renderer constructs Resources/<name>/<name>.model3.json internally.
Envelope buildLoadModel(const QString& modelPath);

// §A.2 — window top-left in screen pixels (origin top-left, Y down).
Envelope buildSetPosition(int x, int y);

// §A.3 — window size in pixels (renderer clamps to 100–2000).
Envelope buildSetSize(int width, int height);

// §A.4 — window opacity 0.0 (fully transparent) – 1.0 (opaque).
Envelope buildSetOpacity(double opacity);

// §A.5 — target FPS. 0 = adaptive (15–60 float); 1–120 = fixed.
Envelope buildSetFps(int targetFps);

// §C.3 (Phase 3b) — global volume + mute state. Both fields always present
// (matches the Java reference startup payload); the renderer applies each
// independently when the field exists.
Envelope buildSetVolume(double volume, bool muted);

// §E.1 — model user-layout offset (px) + scale. NOT set_scale (stub).
Envelope buildSetLayout(double offsetX, double offsetY, double scale);

// ── Post-model-loaded ──────────────────────────────────────────────────────

// §A.6 — hit-area names from the model's .model3.json HitAreas[*].Name.
// Sent once model_loaded arrives (not part of the initial 9-command salvo).
Envelope buildSetHitAreas(const QJsonArray& hitAreas);

// ── Lifecycle ──────────────────────────────────────────────────────────────

// §G.1 — graceful shutdown. Renderer replies Response(success) then exits.
Envelope buildShutdown();

// ── Runtime commands (T16) ─────────────────────────────────────────────────

// §B.1 — play a motion from the loaded model's motion group. priority per
// interface.md §B.1: 0=Force (interrupts current), 1=Idle, 2=Normal.
Envelope buildPlayMotion(const QString& group, int index, int priority);

// §B.3 — stop the currently-playing motion. Empty payload.
Envelope buildStopMotion();

// §B.4 — apply an expression from the loaded model's expression list.
// expressionId matches model3.json FileReferences.Expressions[].Name.
Envelope buildSetExpression(const QString& expressionId);

// §B.2 — play an external motion file (.motion3.json) with optional audio
// and lip-sync side-channels. audioPath/lipSyncPath are emitted ONLY when
// non-empty (caller opted in to that side-channel). fadeIn/fadeOut are
// seconds. (The former subtitle_text/subtitle_duration side-channel was
// removed in the bubble-stream switch — dialogue text now reaches the user
// via the controller-side notification stream, never the renderer.)
Envelope buildPlayMotionExt(const QString& motionPath, int priority,
                            double fadeIn, double fadeOut,
                            const QString& audioPath = {},
                            const QString& lipSyncPath = {});

// §C.1 — play an audio file (.ogg). volume is 0.0–1.0.
Envelope buildPlayAudio(const QString& audioPath, double volume);

// §C.2 — stop the currently-playing audio. Empty payload.
Envelope buildStopAudio();

// §F.1 — request current runtime stats. Renderer replies with a
// stats_state event (not a Response). Empty payload.
Envelope buildGetStats();

// §E.2 — query the current model layout. Empty payload; renderer replies
// via Response carrying {offset_x, offset_y, scale}.
Envelope buildGetLayout();

// §E.3 — reset model layout to defaults. Empty payload.
Envelope buildResetLayout();

} // namespace Protocol
