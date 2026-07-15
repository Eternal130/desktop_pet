#include "core/PanelConfig.hpp"

#include <QJsonArray>
#include <QJsonValue>
#include <QLatin1String>

// spdlog MUST be included before logging/Logging.hpp — Logging.hpp references
// the SPDLOG_* macros but does NOT include spdlog headers itself (T5 finding,
// .omo/notepads/qt-controller-foundation/learnings.md). Mirrors InstanceConfig.cpp.
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
// (Same helpers as InstanceConfig.cpp plus readStringList for instance_ids.)

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
    // all PanelConfig int fields hold small values (pixel coords/dimensions).
    const QJsonValue v = json.value(key);
    return v.isDouble() ? v.toInt() : fallback;
}

bool readBool(const QJsonObject& json, QLatin1String key, bool fallback)
{
    const QJsonValue v = json.value(key);
    return v.isBool() ? v.toBool() : fallback;
}

QStringList readStringList(const QJsonObject& json, QLatin1String key, const QStringList& fallback)
{
    // instance_ids is a JSON array of strings. A non-array (missing, null,
    // wrong type) yields the fallback. Individual non-string elements are
    // skipped (tolerated) rather than aborting the whole list — §1.5.
    const QJsonValue v = json.value(key);
    if (!v.isArray())
        return fallback;
    const QJsonArray arr = v.toArray();
    QStringList result;
    result.reserve(arr.size());
    for (const QJsonValue& elem : arr) {
        if (elem.isString())
            result.append(elem.toString());
    }
    return result;
}

} // namespace

QJsonObject panelConfigToJson(const PanelConfig& cfg)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("panel_x"), cfg.panelX);
    obj.insert(QStringLiteral("panel_y"), cfg.panelY);
    obj.insert(QStringLiteral("panel_width"), cfg.panelWidth);
    obj.insert(QStringLiteral("panel_height"), cfg.panelHeight);
    obj.insert(QStringLiteral("theme"), cfg.theme);
    obj.insert(QStringLiteral("font_size"), cfg.fontSize);
    obj.insert(QStringLiteral("panel_opacity"), cfg.panelOpacity);
    // instance_ids → JSON array of strings (empty array when none).
    QJsonArray ids;
    for (const QString& id : cfg.instanceIds)
        ids.append(id);
    obj.insert(QStringLiteral("instance_ids"), ids);
    obj.insert(QStringLiteral("auto_launch_system"), cfg.autoLaunchSystem);
    obj.insert(QStringLiteral("start_minimized"), cfg.startMinimized);
    obj.insert(QStringLiteral("close_action"), cfg.closeAction);
    obj.insert(QStringLiteral("confirm_on_exit"), cfg.confirmOnExit);
    return obj;
}

PanelConfig panelConfigFromJson(const QJsonObject& json)
{
    // Construct with every default member initializer active, then overwrite
    // each field that is genuinely present (and correctly typed) in the JSON.
    // Unknown keys are never read — they are silently tolerated (§1.5). The
    // whole function is noexcept in practice: QJsonValue accessors never throw.
    PanelConfig cfg;

    cfg.panelX = readInt(json, QLatin1String("panel_x"), cfg.panelX);
    cfg.panelY = readInt(json, QLatin1String("panel_y"), cfg.panelY);
    cfg.panelWidth = readInt(json, QLatin1String("panel_width"), cfg.panelWidth);
    cfg.panelHeight = readInt(json, QLatin1String("panel_height"), cfg.panelHeight);
    cfg.theme = readString(json, QLatin1String("theme"), cfg.theme);
    cfg.fontSize = readInt(json, QLatin1String("font_size"), cfg.fontSize);
    cfg.panelOpacity = readDouble(json, QLatin1String("panel_opacity"), cfg.panelOpacity);
    cfg.instanceIds = readStringList(json, QLatin1String("instance_ids"), cfg.instanceIds);
    cfg.autoLaunchSystem = readBool(json, QLatin1String("auto_launch_system"), cfg.autoLaunchSystem);
    cfg.startMinimized = readBool(json, QLatin1String("start_minimized"), cfg.startMinimized);
    cfg.closeAction = readString(json, QLatin1String("close_action"), cfg.closeAction);
    cfg.confirmOnExit = readBool(json, QLatin1String("confirm_on_exit"), cfg.confirmOnExit);

    return cfg;
}

PanelConfig defaultPanelConfig()
{
    // Every field at its blueprint §5.2 default, empty instanceIds. PanelConfig
    // has no identity field (no UUID), so — unlike defaultInstanceConfig —
    // there is nothing to mint; a value-initialized struct IS the default.
    return PanelConfig{};
}
