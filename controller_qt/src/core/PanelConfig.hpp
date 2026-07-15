#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

// PanelConfig (task T19) — the persisted UI state of the control panel itself.
// Stored as JSON at ~/.config/desktop-pet/panel.json (configuration.md §5),
// consumed by T21 (PanelStateManager) and T28 (window-state restore). This is
// the Qt-side analogue of the Java controller/.../model/PanelConfig.java record.
//
// Field set & defaults: architecture-blueprint.md §5.2 (the authoritative
// table). The struct's default member initializers mirror that table exactly.
// JSON keys are snake_case (matching the Java Gson FieldNamingPolicy +
// configuration.md §5 examples) so the Qt and JavaFX controllers read/write the
// SAME panel.json byte-for-byte.
//
// NOTE on field count: blueprint §5.2 lists 12 fields (panelX, panelY,
// panelWidth, panelHeight, theme, fontSize, panelOpacity, instanceIds,
// autoLaunchSystem, startMinimized, closeAction, confirmOnExit). Some task
// prose says "11"; that is an off-by-one in the narrative — the blueprint
// table, this struct, and the Java record each define 12 fields, and the test
// asserts exactly 12 serialized keys.
//
// NOTE on geometry types: blueprint §5.2 lists panelX/Y/Width/Height as
// "double", but this struct uses int — matching the InstanceConfig precedent
// (int windowX/Y/Width/Height) and the Java PanelConfig, because window
// geometry is pixel-integer. Using double would let fractional junk survive a
// round-trip; int narrows cleanly and matches the rest of the window model.
struct PanelConfig {
    // ── window geometry ─────────────────────────────────────────────────────
    int panelX = -1;                 // -1 = center on screen on first launch
    int panelY = -1;
    int panelWidth = 1200;
    int panelHeight = 760;

    // ── appearance ───────────────────────────────────────────────────────────
    QString theme = QStringLiteral("深紫梦幻");      // base theme name
    int fontSize = 13;
    double panelOpacity = 1.0;       // panel alpha 0.0–1.0

    // ── instance roster ──────────────────────────────────────────────────────
    QStringList instanceIds;         // instance UUIDs; order = sidebar order

    // ── startup / exit behavior ──────────────────────────────────────────────
    bool autoLaunchSystem = false;   // system-level auto-start
    bool startMinimized = false;     // launch minimized to tray
    QString closeAction = QStringLiteral("exit");    // "exit" | "minimize"
    bool confirmOnExit = false;      // prompt before quitting

    // Member-wise equality — used by round-trip identity tests (QJsonObject,
    // QString, and QStringList all compare by content, so ordering of
    // instanceIds matters but JSON key ordering does not).
    bool operator==(const PanelConfig& other) const {
        return panelX == other.panelX
            && panelY == other.panelY
            && panelWidth == other.panelWidth
            && panelHeight == other.panelHeight
            && theme == other.theme
            && fontSize == other.fontSize
            && panelOpacity == other.panelOpacity
            && instanceIds == other.instanceIds
            && autoLaunchSystem == other.autoLaunchSystem
            && startMinimized == other.startMinimized
            && closeAction == other.closeAction
            && confirmOnExit == other.confirmOnExit;
    }
};

// Serialize a PanelConfig to a QJsonObject with snake_case keys for JavaFX
// interop (configuration.md §5). Every field is written; instance_ids becomes a
// JSON array of strings. The result always has exactly 12 keys (one per field).
QJsonObject panelConfigToJson(const PanelConfig& cfg);

// Deserialize a QJsonObject into a PanelConfig. Missing fields are merged from
// the struct defaults; unknown fields are silently ignored (interface.md §1.5
// "unknown field tolerance"). Type-mismatched or absent values fall back to the
// default. instance_ids elements that are not strings are skipped (tolerated).
// NEVER throws — garbage / non-object input returns a default-constructed
// PanelConfig (all field defaults, empty instanceIds).
PanelConfig panelConfigFromJson(const QJsonObject& json);

// A default-constructed PanelConfig (every field at its blueprint §5.2 default,
// empty instanceIds). Unlike defaultInstanceConfig there is no UUID to mint —
// the panel is a singleton with no identity field.
PanelConfig defaultPanelConfig();
