#pragma once

#include <QJsonObject>
#include <QString>

// InstanceConfig (task T18) — the complete persisted configuration for a single
// pet instance. Stored as JSON at ~/.config/desktop-pet/instances/{uuid}.json
// (configuration.md §5), consumed by T20 (InstanceConfigManager) and T28
// (window-state restore). This struct is the FULL superset of the minimal
// InstanceConfigLike view that StartupSalvo (T16) used provisionally.
//
// Field set & defaults: architecture-blueprint.md §5.1. The struct's default
// member initializers mirror that table. JSON keys are snake_case
// (configuration.md §5 examples) so instance files stay byte-for-byte
// compatible with the legacy config format.
//
// The 7 former subtitle_* fields were removed in the bubble-stream switch
// phase (the Qt controller no longer drives renderer subtitles). fromJson
// silently ignores those keys in existing files written by older builds
// (interface.md §1.5 unknown-field tolerance).
//
// voicePack nullability: the JSON value is a nullable string (null = none).
// Qt has no null QString, so the empty QString is the "none" sentinel.
// toJson writes JSON null for an empty value and a real string otherwise;
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

    // ── expression / audio ──────────────────────────────────────────────────
    QString currentExpression = QStringLiteral("F01");
    QString voicePack;                     // empty = no voice pack mounted
    double volume = 1.0;                   // 0.0–1.0
    bool muted = false;

    // ── layout (model placement within the window) ─────────────────────────
    double layoutOffsetX = 0.0;
    double layoutOffsetY = 0.0;
    double layoutScale = 1.0;

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
            && layoutScale == other.layoutScale;
    }
};

// Serialize an InstanceConfig to a QJsonObject with snake_case keys
// (configuration.md §5). Every field is written; voice_pack is written
// as JSON null when empty and a string otherwise. The result always has
// exactly one key per field (23).
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
