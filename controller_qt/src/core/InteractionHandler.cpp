#include "core/InteractionHandler.hpp"

#include <QChar>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1String>

#include <spdlog/spdlog.h>
#include "logging/Logging.hpp"
#include "network/Envelope.hpp"
#include "network/Protocol.hpp"

namespace {

// DEFAULT_MAPPINGS — the built-in fallback for unrecognized-but-common area
// ids. Keys are LOWERCASE so the lookup lowercases the incoming area_id
// first (interface.md §F: head→TapHead, body→TapBody, priority 2).
//
// Built as a function-local static QMap so it is constructed once on first
// use (thread-safe initialization since C++11). Mirrors Java's private
// static final Map<String, HitAction> DEFAULT_MAPPINGS.
const QMap<QString, HitAction>& defaultMappings()
{
    static const QMap<QString, HitAction> map = {
        {QStringLiteral("head"), HitAction{QStringLiteral("TapHead"), 2}},
        {QStringLiteral("body"), HitAction{QStringLiteral("TapBody"), 2}},
    };
    return map;
}

} // namespace

InteractionHandler::InteractionHandler(MessageSender messageSender, QObject* parent)
    : QObject(parent)
    , m_messageSender(std::move(messageSender))
{
}

InteractionHandler::~InteractionHandler() = default;

void InteractionHandler::setModelConfig(const ModelConfig& config)
{
    m_modelConfig = config;
}

void InteractionHandler::handleHitEvent(const Envelope& hitEvent)
{
    // payload is ALWAYS a QJsonObject (Envelope invariant — never null), so
    // there is no null-check equivalent to the Java `hitEvent.payload() ==
    // null` guard. An empty payload simply yields an empty area_id below.
    const QJsonValue areaVal = hitEvent.payload.value(QStringLiteral("area_id"));
    if (!areaVal.isString()) {
        // Missing, null, or non-string area_id → silent no-op (Java returns
        // "" from extractAreaId on any of these; same effect here).
        LOG_DEBUG("InteractionHandler: hit event has no string area_id; ignoring");
        return;
    }

    const QString areaId = areaVal.toString().trimmed();
    if (areaId.isEmpty()) {
        LOG_DEBUG("InteractionHandler: hit event has empty area_id; ignoring");
        return;
    }

    const std::optional<HitAction> action = findHitAction(areaId);
    if (!action.has_value()) {
        LOG_DEBUG("InteractionHandler: no hit action configured for area_id='{}'",
                  areaId.toStdString());
        return;
    }

    // Build play_motion via the typed factory. index=0 — the renderer cycles
    // or picks the first motion in the group. (todo 16: previously inline.)
    const Envelope command = Protocol::buildPlayMotion(action->group, 0, action->priority);
    const QString json = QString::fromUtf8(
        serialize(command).toJson(QJsonDocument::Compact));

    if (m_messageSender) {
        m_messageSender(json);
    }
    LOG_DEBUG("InteractionHandler: play_motion sent for area_id='{}' group='{}'",
              areaId.toStdString(), action->group.toStdString());
}

std::optional<HitAction> InteractionHandler::findHitAction(const QString& areaId) const
{
    // Tier 1 — exact match in the per-model hitActions map.
    const auto& actions = m_modelConfig.hitActions;
    const auto exactIt = actions.constFind(areaId);
    if (exactIt != actions.constEnd()) {
        return exactIt.value();
    }

    // Tier 2 — capitalized form of the INPUT ("head" → "Head", "HEAD" →
    // "Head"). This is the common case: renderer sends the model3.json
    // HitArea name verbatim ("Body"), but model_config.json might use the
    // capitalized form ("Body") while the renderer sent lowercase ("body"),
    // or vice versa.
    const QString capitalized = capitalize(areaId);
    if (capitalized != areaId) {
        const auto capIt = actions.constFind(capitalized);
        if (capIt != actions.constEnd()) {
            return capIt.value();
        }
    }

    // Tier 3 — DEFAULT_MAPPINGS lowercased. Handles the universal defaults
    // (head/body) regardless of model_config.json presence or casing.
    const QString lowered = areaId.toLower();
    const auto& defaults = defaultMappings();
    const auto defIt = defaults.constFind(lowered);
    if (defIt != defaults.constEnd()) {
        return defIt.value();
    }

    return std::nullopt;
}

QString InteractionHandler::capitalize(const QString& s)
{
    if (s.isEmpty()) {
        return s;
    }
    // Java: Character.toUpperCase(s.charAt(0)) + s.substring(1).toLowerCase()
    // toUpper/toLower without a locale (defaults to Unicode default
    // case-folding — close enough to Java's no-arg Character methods which
    // also use the default locale-insensitive case conversion).
    QString result;
    result.reserve(s.size());
    result.append(s.at(0).toUpper());
    if (s.size() > 1) {
        result.append(s.mid(1).toLower());
    }
    return result;
}
