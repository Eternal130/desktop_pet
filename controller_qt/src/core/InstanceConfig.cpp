#include "core/InstanceConfig.hpp"

#include <QJsonValue>
#include <QLatin1String>
#include <QUuid>

// spdlog MUST be included before logging/Logging.hpp — Logging.hpp references
// the SPDLOG_* macros but does NOT include spdlog headers itself (T5 finding,
// .omo/notepads/qt-controller-foundation/learnings.md).
#include <spdlog/spdlog.h>
#include "logging/Logging.hpp"

namespace {

// ── Typed readers ───────────────────────────────────────────────────────────
// Each reads a snake_case key from JSON only when it is present AND correctly
// typed; anything else (missing key, null, wrong type) yields the caller-
// supplied fallback. This is the per-field expression of interface.md §1.5:
// "optional fields missing → use default; unknown/mistyped → tolerate". None
// of these throw — QJsonValue accessors return a default-converted value for
// any type, and we guard with isXxx() before trusting the conversion.

QString readString(const QJsonObject& json, QLatin1String key, const QString& fallback)
{
    const QJsonValue v = json.value(key);
    return v.isString() ? v.toString() : fallback;
}

double readDouble(const QJsonObject& json, QLatin1String key, double fallback)
{
    const QJsonValue v = json.value(key);
    return v.isDouble() ? v.toDouble() : fallback;
}

int readInt(const QJsonObject& json, QLatin1String key, int fallback)
{
    // QJsonValue stores every JSON number as a double internally; isDouble()
    // is the right presence guard for any number. toInt() is safe here because
    // all InstanceConfig int fields hold small values (window coords, fps,
    // intervals, pixel dimensions) — nowhere near INT32_MAX. (Contrast with
    // Envelope::timestamp which MUST use toInteger() — T4 learning.)
    const QJsonValue v = json.value(key);
    return v.isDouble() ? v.toInt() : fallback;
}

bool readBool(const QJsonObject& json, QLatin1String key, bool fallback)
{
    const QJsonValue v = json.value(key);
    return v.isBool() ? v.toBool() : fallback;
}

} // namespace

QJsonObject instanceConfigToJson(const InstanceConfig& cfg)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), cfg.id);
    obj.insert(QStringLiteral("label"), cfg.label);
    obj.insert(QStringLiteral("avatar"), cfg.avatar);
    obj.insert(QStringLiteral("renderer_path"), cfg.rendererPath);
    obj.insert(QStringLiteral("graphics_backend"), cfg.graphicsBackend);
    obj.insert(QStringLiteral("current_model_name"), cfg.modelName);
    obj.insert(QStringLiteral("model_scale"), cfg.modelScale);
    obj.insert(QStringLiteral("window_x"), cfg.windowX);
    obj.insert(QStringLiteral("window_y"), cfg.windowY);
    obj.insert(QStringLiteral("window_width"), cfg.windowWidth);
    obj.insert(QStringLiteral("window_height"), cfg.windowHeight);
    obj.insert(QStringLiteral("opacity"), cfg.opacity);
    obj.insert(QStringLiteral("drag_mode"), cfg.dragMode);
    obj.insert(QStringLiteral("idle_interval"), cfg.idleInterval);
    obj.insert(QStringLiteral("target_fps"), cfg.targetFps);
    obj.insert(QStringLiteral("auto_start"), cfg.autoStart);
    obj.insert(QStringLiteral("current_expression"), cfg.currentExpression);
    // voice_pack: null when empty (byte-compatible with Java's null
    // default), the string otherwise. fromJson accepts both forms.
    obj.insert(QStringLiteral("voice_pack"),
               cfg.voicePack.isEmpty() ? QJsonValue(QJsonValue::Null)
                                       : QJsonValue(cfg.voicePack));
    obj.insert(QStringLiteral("volume"), cfg.volume);
    obj.insert(QStringLiteral("muted"), cfg.muted);
    obj.insert(QStringLiteral("layout_offset_x"), cfg.layoutOffsetX);
    obj.insert(QStringLiteral("layout_offset_y"), cfg.layoutOffsetY);
    obj.insert(QStringLiteral("layout_scale"), cfg.layoutScale);
    return obj;
}

InstanceConfig instanceConfigFromJson(const QJsonObject& json)
{
    // Construct with every default member initializer active, then overwrite
    // each field that is genuinely present (and correctly typed) in the JSON.
    // Unknown keys are never read — they are silently tolerated (§1.5). The
    // whole function is noexcept in practice: QJsonValue accessors never throw.
    InstanceConfig cfg;

    cfg.id = readString(json, QLatin1String("id"), cfg.id);
    cfg.label = readString(json, QLatin1String("label"), cfg.label);
    cfg.avatar = readString(json, QLatin1String("avatar"), cfg.avatar);
    cfg.rendererPath = readString(json, QLatin1String("renderer_path"), cfg.rendererPath);
    cfg.graphicsBackend = readString(json, QLatin1String("graphics_backend"), cfg.graphicsBackend);
    cfg.modelName = readString(json, QLatin1String("current_model_name"), cfg.modelName);
    cfg.modelScale = readDouble(json, QLatin1String("model_scale"), cfg.modelScale);
    cfg.windowX = readInt(json, QLatin1String("window_x"), cfg.windowX);
    cfg.windowY = readInt(json, QLatin1String("window_y"), cfg.windowY);
    cfg.windowWidth = readInt(json, QLatin1String("window_width"), cfg.windowWidth);
    cfg.windowHeight = readInt(json, QLatin1String("window_height"), cfg.windowHeight);
    cfg.opacity = readDouble(json, QLatin1String("opacity"), cfg.opacity);
    cfg.dragMode = readString(json, QLatin1String("drag_mode"), cfg.dragMode);
    cfg.idleInterval = readInt(json, QLatin1String("idle_interval"), cfg.idleInterval);
    cfg.targetFps = readInt(json, QLatin1String("target_fps"), cfg.targetFps);
    cfg.autoStart = readBool(json, QLatin1String("auto_start"), cfg.autoStart);
    cfg.currentExpression = readString(json, QLatin1String("current_expression"), cfg.currentExpression);
    cfg.voicePack = readString(json, QLatin1String("voice_pack"), cfg.voicePack);
    cfg.volume = readDouble(json, QLatin1String("volume"), cfg.volume);
    cfg.muted = readBool(json, QLatin1String("muted"), cfg.muted);
    cfg.layoutOffsetX = readDouble(json, QLatin1String("layout_offset_x"), cfg.layoutOffsetX);
    cfg.layoutOffsetY = readDouble(json, QLatin1String("layout_offset_y"), cfg.layoutOffsetY);
    cfg.layoutScale = readDouble(json, QLatin1String("layout_scale"), cfg.layoutScale);

    return cfg;
}

InstanceConfig defaultInstanceConfig()
{
    // Take every struct default, then stamp a fresh UUID as the persistence
    // primary key (the instances/{uuid}.json filename). label stays empty here
    // (the struct has no default for it) — the UI layer assigns a user-facing
    // label at instance-creation time.
    InstanceConfig cfg;
    cfg.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return cfg;
}
