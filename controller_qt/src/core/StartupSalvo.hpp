#pragma once

#include "network/Envelope.hpp"

#include <QJsonArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

// StartupSalvo (task T16) — the controller→renderer bootstrap command stream.
//
// Two firing points (architecture-blueprint.md §6.1 + handshake.md §1):
//   1. After the `ready` event arrives, fire sendSalvo() exactly once: it emits
//      the 9 startup commands IN ORDER (load_model, set_position, set_size,
//      set_opacity, set_fps, set_volume, set_layout, set_subtitle_layout,
//      set_subtitle_style) via the Protocol (T9) factories. Each command is
//      serialized compact and pushed through the configured CommandSender
//      (wired to WsServer::sendText in production).
//   2. After the `model_loaded` event arrives, fire sendSetHitAreas() once with
//      the parsed HitAreas array from .model3.json — the renderer needs the
//      hit-area names to route click coordinates (interface.md §A.6).
//
// NOT sent here:
//   - `set_scale` — interface.md §H.2 marks it as a renderer-side stub (logs
//     only, no effect). Scale is carried by `set_layout`'s `scale` field, so
//     the salvo sends set_layout instead. The "no set_scale" rule is a PRIMARY
//     acceptance criterion and is asserted in StartupSalvoTest.
//   - Any runtime command (play_motion, show_subtitle, etc.) — those land via
//     the runtime dispatcher, not the bootstrap salvo.
//
// Threading: StartupSalvo lives on the Qt main thread. WsServer's
// messageReceived signal is marshaled to the main thread (T12), so by the time
// the caller invokes sendSalvo()/sendSetHitAreas() we are on the same thread
// that owns the WsServer. No locks needed.
//
// Config: InstanceConfigLike is a MINIMAL view of the eventual InstanceConfig
// (T18). The salvo reads only the 13 fields it needs to build the 9 commands.
// The full InstanceConfig struct (with id/label/voicePack/graphicsBackend/etc.)
// arrives in T18 and will be implicitly convertible to this view.

// Minimal config struct consumed by the salvo. The full InstanceConfig (T18)
// will supply the same fields; this struct deliberately exposes ONLY what the
// 9 commands need so the salvo's input surface is auditable in isolation.
struct InstanceConfigLike {
    QString modelName = QStringLiteral("Hiyori");
    int windowX = 1200;
    int windowY = 600;
    int windowWidth = 400;
    int windowHeight = 500;
    double opacity = 1.0;
    int targetFps = 0;        // 0 = adaptive (interface.md §A.5)
    double volume = 1.0;
    bool muted = false;
    double layoutOffsetX = 0.0;
    double layoutOffsetY = 0.0;
    double layoutScale = 1.0;
    double subtitleOffsetX = 0.0;
    double subtitleOffsetY = 0.0;
    int subtitleAreaWidth = 0;   // 0 = auto
    int subtitleAreaHeight = 0;  // 0 = auto
};

class StartupSalvo : public QObject {
    Q_OBJECT
public:
    explicit StartupSalvo(QObject* parent = nullptr);

    // Sender function type: takes a compact JSON string and pushes it to the
    // renderer (wired to WsServer::sendText in production, captured by tests).
    using CommandSender = std::function<void(const QString& json)>;

    // Set the sender. Must be called before sendSalvo / sendSetHitAreas.
    // Passing a nullptr sender is allowed; calls become no-ops (with a WARN).
    void setCommandSender(CommandSender sender);

    // Send the 9 startup commands (called once after the `ready` event).
    // Returns the action names sent, in order, so the caller (and tests) can
    // verify the salvo shape without inspecting the JSON stream.
    // No-op (returns empty list) if the sender is unset.
    QStringList sendSalvo(const InstanceConfigLike& config);

    // Send set_hit_areas (called once after the `model_loaded` event).
    // hitAreas carries the model's HitAreas[*].Id strings parsed from the
    // .model3.json file (the renderer's model_loaded payload is empty for
    // these — Java/Qt side must parse the file, per AGENTS.md "model_loaded"
    // pitfall). The renderer echoes them back as click area_id values.
    void sendSetHitAreas(const QJsonArray& hitAreas);

signals:
    // Emitted for each command sent (synchronously, from inside sendSalvo /
    // sendSetHitAreas). Tests capture the stream via QSignalSpy to assert
    // ordering + content + the "no set_scale" rule. `action` is the envelope
    // action (e.g. "load_model"); `json` is the compact wire form.
    void commandSent(const QString& action, const QString& json);

private:
    // Build, serialize, send, and emit commandSent for a single envelope.
    // Returns the action string (empty if the sender is unset and the call
    // was a no-op). Centralizes the serialize→send→emit pattern so the salvo
    // body stays declarative.
    QString sendOne(const Envelope& env);

    CommandSender m_sender;
};
