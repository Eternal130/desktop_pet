#pragma once

#include "network/Envelope.hpp"

#include <QJsonArray>
#include <QString>

// Protocol typed factories (T9).
//
// Layer on top of Envelope.hpp's thin createCommand() that bakes the EXACT
// payload fields each startup-salvo command requires per interface.md §5.
// Consumed by T16 (startup salvo: ready → 9 commands → model_loaded →
// set_hit_areas). Every builder:
//   - delegates to createCommand("action", payload) so id/timestamp/type come
//     from the single source of truth in Envelope.cpp (UUIDv4 + current ms);
//   - emits snake_case keys matching interface.md §5 verbatim;
//   - never emits response fields (those live only on type=="response").
//
// Scope: the 9 salvo commands + set_hit_areas + shutdown. Runtime commands
// (play_motion / show_subtitle / etc.) get their own factories in a later task
// when the call sites that need them arrive — not speculatively here.
//
// NOT introduced: set_scale. Per interface.md §H.2 it is a renderer-side stub
// (logs only, no effect); scale is carried by set_layout's `scale` field.

// The 16-field subtitle style block shared by show_subtitle (§D.1) and
// set_subtitle_style (§D.3). Colors are AABBGGRR uint32 decimals (§1.3) —
// 0x00FFFFFF = 16777215 opaque white, 0x80000000 = 2147483648 half-transparent
// black. Defaults mirror the §D.1 "默认值" column so a default-constructed
// SubtitleStyle reproduces the fixture payload in command_all_25.json exactly.
struct SubtitleStyle {
    QString fontName      = QStringLiteral("Microsoft YaHei");
    double  fontSize      = 48.0;
    quint32 primaryColor  = 0x00FFFFFFu; // opaque white
    quint32 outlineColor  = 0x00111111u;
    double  outlineWidth  = 1.8;
    quint32 shadowColor   = 0x00000000u; // opaque black
    double  shadowDepth   = 0.0;
    int     alignment     = 2;           // ASS numpad layout: 2 = bottom-center
    double  marginV       = 30.0;
    double  edgeBlur      = 0.6;
    int     fontWeight    = -1;          // -1 = do not inject
    double  letterSpacing = 0.5;
    bool    bgBoxEnabled  = false;
    quint32 bgBoxColor    = 0x80000000u; // 50% transparent black
    double  bgBoxPaddingX = 12.0;
    double  bgBoxPaddingY = 6.0;
};

namespace Protocol {

// ── Startup salvo (9 commands, sent after `ready`, blueprint §6.1) ─────────

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

// §D.5 — subtitle region offset (px), area size (0 = auto), font size.
Envelope buildSetSubtitleLayout(double offsetX, double offsetY,
                                int areaWidth, int areaHeight,
                                double fontSize = 48.0);

// §D.3 — default subtitle style (writes ASS style 0). All 16 fields emitted;
// caller overrides via the struct, defaults match interface.md §D.1.
Envelope buildSetSubtitleStyle(const SubtitleStyle& style = {});

// ── Post-model-loaded ──────────────────────────────────────────────────────

// §A.6 — hit-area names from the model's .model3.json HitAreas[*].Name.
// Sent once model_loaded arrives (not part of the initial 9-command salvo).
Envelope buildSetHitAreas(const QJsonArray& hitAreas);

// ── Lifecycle ──────────────────────────────────────────────────────────────

// §G.1 — graceful shutdown. Renderer replies Response(success) then exits.
Envelope buildShutdown();

} // namespace Protocol
