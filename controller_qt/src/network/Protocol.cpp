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

} // namespace Protocol
