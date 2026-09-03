#include "network/Protocol.hpp"

#include <QJsonObject>
#include <QLatin1String>

namespace Protocol {

// ── Startup salvo ──────────────────────────────────────────────────────────

Envelope buildLoadModel(const QString& modelPath) {
    QJsonObject p;
    p.insert(QStringLiteral("model_path"), modelPath);
    return createCommand(QLatin1String("load_model"), p);
}

Envelope buildSetPosition(int x, int y) {
    QJsonObject p;
    p.insert(QStringLiteral("x"), x);
    p.insert(QStringLiteral("y"), y);
    return createCommand(QLatin1String("set_position"), p);
}

Envelope buildSetSize(int width, int height) {
    QJsonObject p;
    p.insert(QStringLiteral("width"), width);
    p.insert(QStringLiteral("height"), height);
    return createCommand(QLatin1String("set_size"), p);
}

Envelope buildSetOpacity(double opacity) {
    QJsonObject p;
    p.insert(QStringLiteral("opacity"), opacity);
    return createCommand(QLatin1String("set_opacity"), p);
}

Envelope buildSetFps(int targetFps) {
    QJsonObject p;
    p.insert(QStringLiteral("fps"), targetFps);
    return createCommand(QLatin1String("set_fps"), p);
}

Envelope buildSetVolume(double volume, bool muted) {
    // §C.3: both fields optional at the protocol level, but the controller's
    // startup payload always carries both (the Java reference does the same),
    // so the renderer applies volume + mute in one round-trip.
    QJsonObject p;
    p.insert(QStringLiteral("volume"), volume);
    p.insert(QStringLiteral("muted"), muted);
    return createCommand(QLatin1String("set_volume"), p);
}

Envelope buildSetLayout(double offsetX, double offsetY, double scale) {
    QJsonObject p;
    p.insert(QStringLiteral("offset_x"), offsetX);
    p.insert(QStringLiteral("offset_y"), offsetY);
    p.insert(QStringLiteral("scale"), scale);
    return createCommand(QLatin1String("set_layout"), p);
}

// ── Post-model-loaded ──────────────────────────────────────────────────────

Envelope buildSetHitAreas(const QJsonArray& hitAreas) {
    QJsonObject p;
    p.insert(QStringLiteral("hit_areas"), hitAreas);
    return createCommand(QLatin1String("set_hit_areas"), p);
}

// ── Lifecycle ──────────────────────────────────────────────────────────────

Envelope buildShutdown() {
    // §G.1: empty payload ({}) — the renderer replies Response(success) then
    // exits its main loop. createCommand already fills payload={} by default.
    return createCommand(QLatin1String("shutdown"));
}

// ── Runtime commands (T16) ─────────────────────────────────────────────────

Envelope buildPlayMotion(const QString& group, int index, int priority) {
    QJsonObject p;
    p.insert(QStringLiteral("group"), group);
    p.insert(QStringLiteral("index"), index);
    p.insert(QStringLiteral("priority"), priority);
    return createCommand(QLatin1String("play_motion"), p);
}

Envelope buildStopMotion() {
    return createCommand(QLatin1String("stop_motion"));
}

Envelope buildSetExpression(const QString& expressionId) {
    QJsonObject p;
    p.insert(QStringLiteral("expression_id"), expressionId);
    return createCommand(QLatin1String("set_expression"), p);
}

Envelope buildPlayMotionExt(const QString& motionPath, int priority,
                            double fadeIn, double fadeOut,
                            const QString& audioPath,
                            const QString& lipSyncPath) {
    // §B.2: motion_path / priority / fade_in / fade_out are always emitted.
    // audio_path / lip_sync_path emitted only when non-empty (caller opted in
    // to that side-channel). Matches interface.md §B.2 optional-field rule.
    QJsonObject p;
    p.insert(QStringLiteral("motion_path"), motionPath);
    p.insert(QStringLiteral("priority"), priority);
    p.insert(QStringLiteral("fade_in"), fadeIn);
    p.insert(QStringLiteral("fade_out"), fadeOut);
    if (!audioPath.isEmpty()) {
        p.insert(QStringLiteral("audio_path"), audioPath);
    }
    if (!lipSyncPath.isEmpty()) {
        p.insert(QStringLiteral("lip_sync_path"), lipSyncPath);
    }
    return createCommand(QLatin1String("play_motion_ext"), p);
}

Envelope buildPlayAudio(const QString& audioPath, double volume) {
    QJsonObject p;
    p.insert(QStringLiteral("audio_path"), audioPath);
    p.insert(QStringLiteral("volume"), volume);
    return createCommand(QLatin1String("play_audio"), p);
}

Envelope buildStopAudio() {
    return createCommand(QLatin1String("stop_audio"));
}

Envelope buildGetStats() {
    return createCommand(QLatin1String("get_stats"));
}

Envelope buildGetLayout() {
    return createCommand(QLatin1String("get_layout"));
}

Envelope buildResetLayout() {
    return createCommand(QLatin1String("reset_layout"));
}

} // namespace Protocol
