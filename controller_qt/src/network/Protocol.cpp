#include "network/Protocol.hpp"

#include <QJsonObject>
#include <QLatin1String>

namespace Protocol {

namespace {

// Promote a uint32 color (AABBGGRR, §1.3) to the JSON value that survives a
// round-trip as an INTEGER, not a double-with-decimal. Values past INT_MAX
// (e.g. 0x80000000 = 2147483648) MUST NOT go through QJsonValue(int) — that
// overload would overflow. qint64 holds the full uint32 range cleanly and
// QJsonValue serializes it without a trailing ".0".
inline QJsonValue color(quint32 abgr) {
    return QJsonValue(static_cast<qint64>(abgr));
}

// Emit all 16 SubtitleStyle fields into `p` using the exact snake_case keys
// from interface.md §D.1 / §D.3 (the same field set show_subtitle and
// set_subtitle_style share, minus text/duration). Shared between
// buildSetSubtitleStyle (§D.3) and buildShowSubtitle (§D.1) so the two
// payloads cannot drift apart.
void insertStyle(QJsonObject& p, const SubtitleStyle& s) {
    p.insert(QStringLiteral("font_name"),        s.fontName);
    p.insert(QStringLiteral("font_size"),        s.fontSize);
    p.insert(QStringLiteral("primary_color"),    color(s.primaryColor));
    p.insert(QStringLiteral("outline_color"),    color(s.outlineColor));
    p.insert(QStringLiteral("outline_width"),    s.outlineWidth);
    p.insert(QStringLiteral("shadow_color"),     color(s.shadowColor));
    p.insert(QStringLiteral("shadow_depth"),     s.shadowDepth);
    p.insert(QStringLiteral("alignment"),        s.alignment);
    p.insert(QStringLiteral("margin_v"),         s.marginV);
    p.insert(QStringLiteral("edge_blur"),        s.edgeBlur);
    p.insert(QStringLiteral("font_weight"),      s.fontWeight);
    p.insert(QStringLiteral("letter_spacing"),   s.letterSpacing);
    p.insert(QStringLiteral("bg_box_enabled"),   s.bgBoxEnabled);
    p.insert(QStringLiteral("bg_box_color"),     color(s.bgBoxColor));
    p.insert(QStringLiteral("bg_box_padding_x"), s.bgBoxPaddingX);
    p.insert(QStringLiteral("bg_box_padding_y"), s.bgBoxPaddingY);
}

} // namespace

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

Envelope buildSetSubtitleLayout(double offsetX, double offsetY,
                                int areaWidth, int areaHeight, double fontSize) {
    QJsonObject p;
    p.insert(QStringLiteral("offset_x"), offsetX);
    p.insert(QStringLiteral("offset_y"), offsetY);
    p.insert(QStringLiteral("area_width"), areaWidth);
    p.insert(QStringLiteral("area_height"), areaHeight);
    p.insert(QStringLiteral("font_size"), fontSize);
    return createCommand(QLatin1String("set_subtitle_layout"), p);
}

Envelope buildSetSubtitleStyle(const SubtitleStyle& s) {
    // §D.3: same 16 style fields as show_subtitle minus text/duration.
    // Emitted unconditionally — the renderer applies each field to ASS style 0.
    QJsonObject p;
    insertStyle(p, s);
    return createCommand(QLatin1String("set_subtitle_style"), p);
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
                            const QString& lipSyncPath,
                            const QString& subtitleText,
                            qint64 subtitleDurationMs) {
    // §B.2: motion_path / priority / fade_in / fade_out are always emitted.
    // audio_path / lip_sync_path emitted only when non-empty (caller opted in
    // to that side-channel). subtitle_text + subtitle_duration are emitted as
    // a pair when subtitle_text is non-empty — subtitle_duration without text
    // is meaningless. Matches the Java reference (MountedBehaviorEngine) and
    // the fixture payload (command_all_25.json play_motion_ext case).
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
    if (!subtitleText.isEmpty()) {
        p.insert(QStringLiteral("subtitle_text"), subtitleText);
        p.insert(QStringLiteral("subtitle_duration"), subtitleDurationMs);
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

Envelope buildShowSubtitle(const QString& text, qint64 durationMs,
                           const SubtitleStyle& style) {
    // §D.1: text + duration at the top, then all 16 style fields. The
    // duration field name is `duration` (NOT `duration_ms`) per interface.md
    // §D.1 and the Java reference (Protocol.java showSubtitle).
    QJsonObject p;
    p.insert(QStringLiteral("text"), text);
    p.insert(QStringLiteral("duration"), durationMs);
    insertStyle(p, style);
    return createCommand(QLatin1String("show_subtitle"), p);
}

Envelope buildHideSubtitle() {
    return createCommand(QLatin1String("hide_subtitle"));
}

Envelope buildSetSubtitleAdjustMode(bool enabled) {
    QJsonObject p;
    p.insert(QStringLiteral("enabled"), enabled);
    return createCommand(QLatin1String("set_subtitle_adjust_mode"), p);
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
