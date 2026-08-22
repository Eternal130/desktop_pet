#pragma once

#include <QJsonObject>
#include <QString>

// InstanceConfig (task T18) — the complete persisted configuration for a single
// pet instance. Stored as JSON at ~/.config/desktop-pet/instances/{uuid}.json
// (configuration.md §5), consumed by T20 (InstanceConfigManager) and T28
// (window-state restore). This is the Qt-side analogue of the Java
// controller/.../model/InstanceConfig.java record, and the FULL superset of the
// minimal InstanceConfigLike view that StartupSalvo (T16) used provisionally.
//
// Field set & defaults: architecture-blueprint.md §5.1 (the authoritative 28-
// field table). The struct's default member initializers mirror that table
// exactly. JSON keys are snake_case (matching the Java Gson FieldNamingPolicy +
// configuration.md §5 examples) so the Qt and JavaFX controllers read/write the
// SAME instance files byte-for-byte.
//
// NOTE on field count: blueprint §5.1, the Java record, AND this struct each
// define 28 fields (id … subtitleStylePreset). Some task prose says "29"; that
// is an off-by-one in the narrative — every ground-truth source agrees on 28,
// and the test asserts exactly 28 serialized keys.
//
// voicePack nullability: the Java side treats voice_pack as a nullable string
// (null = not mounted). Qt has no null QString, so the empty QString is the
// "not mounted" sentinel. toJson writes JSON null for an empty voicePack
// (byte-compatible with Java's null default) and a real string otherwise;
// fromJson accepts both null and a string (and treats a missing key as the
// default empty string).
struct InstanceConfig {
    // ── identity / paths ───────────────────────────────────────────────────
    QString id;                            // UUID; persistence primary key
    QString label;                         // user-visible display name
    // Single-glyph avatar (emoji or letter) shown on cards/nav. Empty → 🐱.
    QString avatar = QStringLiteral("🐱");
    QString rendererPath;                  // absolute path to renderer exe

    // ── model + window ─────────────────────────────────────────────────────
    QString graphicsBackend = QStringLiteral("opengl"); // "opengl" | "vulkan"
    QString modelName = QStringLiteral("Hiyori");        // current model short name
    double modelScale = 1.0;
    int windowX = 1200;
    int windowY = 600;
    int windowWidth = 400;
    int windowHeight = 500;
    double opacity = 1.0;                  // window alpha 0.0–1.0

    // ── behavior ───────────────────────────────────────────────────────────
    QString dragMode = QStringLiteral("direct"); // "direct" | "physics"
    int idleInterval = 10;                 // seconds between idle motions
    int targetFps = 0;                     // 0 = adaptive; 15–120 = fixed
    bool autoStart = false;                // start with the panel

    // ── expression / audio ─────────────────────────────────────────────────
    QString currentExpression = QStringLiteral("F01");
    QString voicePack;                     // empty = no voice pack mounted
    double volume = 1.0;                   // 0.0–1.0
    bool muted = false;

    // ── layout (model placement within the window) ─────────────────────────
    double layoutOffsetX = 0.0;
    double layoutOffsetY = 0.0;
    double layoutScale = 1.0;

    // ── subtitle overlay ───────────────────────────────────────────────────
    double subtitleOffsetX = 0.0;
    double subtitleOffsetY = 0.0;
    int subtitleAreaWidth = 0;             // 0 = auto
    int subtitleAreaHeight = 0;            // 0 = auto
    double subtitleFontSize = 48.0;
    QString subtitleStylePreset = QStringLiteral("默认"); // "default" (Chinese)
    // Phase 5 Wave 8 todo 21: subtitle auto-adjust mode (font scaling to area).
    // Bound to the InstanceDetailPage adjust-mode CheckBox; toggled via
    // InstanceSession::setSubtitleAdjustMode which sends set_subtitle_adjust_mode
    // (§D.4) + persists. NOTE: added in todo 21 — the Java reference kept this
    // as runtime-only state (no persisted field); the Qt port persists it so
    // the user's preference survives restarts. Field count goes 28 → 29.
    bool subtitleAdjustMode = false;

    // Member-wise equality — used by round-trip identity tests (QJsonObject and
    // QString compare by content, so ordering is irrelevant).
    bool operator==(const InstanceConfig& other) const {
        return id == other.id
            && label == other.label
            && avatar == other.avatar
            && rendererPath == other.rendererPath
            && graphicsBackend == other.graphicsBackend
            && modelName == other.modelName
            && modelScale == other.modelScale
            && windowX == other.windowX
            && windowY == other.windowY
            && windowWidth == other.windowWidth
            && windowHeight == other.windowHeight
            && opacity == other.opacity
            && dragMode == other.dragMode
            && idleInterval == other.idleInterval
            && targetFps == other.targetFps
            && autoStart == other.autoStart
            && currentExpression == other.currentExpression
            && voicePack == other.voicePack
            && volume == other.volume
            && muted == other.muted
            && layoutOffsetX == other.layoutOffsetX
            && layoutOffsetY == other.layoutOffsetY
            && layoutScale == other.layoutScale
            && subtitleOffsetX == other.subtitleOffsetX
            && subtitleOffsetY == other.subtitleOffsetY
            && subtitleAreaWidth == other.subtitleAreaWidth
            && subtitleAreaHeight == other.subtitleAreaHeight
            && subtitleFontSize == other.subtitleFontSize
            && subtitleStylePreset == other.subtitleStylePreset
            && subtitleAdjustMode == other.subtitleAdjustMode;
    }
};

// Serialize an InstanceConfig to a QJsonObject with snake_case keys for JavaFX
// interop (configuration.md §5). Every field is written; voice_pack is written
// as JSON null when empty (matching Java's null default) and a string
// otherwise. The result always has exactly 28 keys (one per field).
QJsonObject instanceConfigToJson(const InstanceConfig& cfg);

// Deserialize a QJsonObject into an InstanceConfig. Missing fields are merged
// from the struct defaults; unknown fields are silently ignored
// (interface.md §1.5 "unknown field tolerance"). Type-mismatched or absent
// values fall back to the default. NEVER throws — garbage / non-object input
// returns a default-constructed InstanceConfig (empty id + field defaults).
InstanceConfig instanceConfigFromJson(const QJsonObject& json);

// A default-constructed InstanceConfig with a freshly generated UUID in `id`.
// All other fields take their struct defaults. Used when creating a brand-new
// instance (the id is the persistence filename: instances/{uuid}.json).
InstanceConfig defaultInstanceConfig();
