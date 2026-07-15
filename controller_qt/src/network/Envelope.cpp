#include "network/Envelope.hpp"

#include <QDateTime>
#include <QLatin1String>
#include <QUuid>

namespace {

// The three legal Envelope types (interface.md §2.1).
bool isValidType(const QString& t)
{
    return t == QLatin1String("command")
        || t == QLatin1String("event")
        || t == QLatin1String("response");
}

} // namespace

QJsonDocument serialize(const Envelope& env)
{
    QJsonObject obj;
    obj.insert("type", env.type);
    obj.insert("action", env.action);
    obj.insert("id", env.id);
    obj.insert("payload", env.payload); // always an object, even when empty (§1.5)
    obj.insert("timestamp", env.timestamp); // qint64 -> QJsonValue(int64)

    // §2.3: success / error_code / error_message are written ONLY for responses,
    // and they sit at the JSON TOP LEVEL (siblings of type/action/id), never
    // inside payload. command/event JSON never carries these three keys.
    if (env.type == QLatin1String("response")) {
        obj.insert("success", env.success);
        obj.insert("error_code", env.errorCode);
        obj.insert("error_message", env.errorMessage);
    }
    return QJsonDocument(obj);
}

std::optional<Envelope> deserialize(const QJsonObject& json)
{
    // Required envelope fields (§2.1). Each must be present and correctly typed;
    // any failure => silent drop (§2.4). Order: cheap existence checks first.
    if (!json.contains("type") || !json.value("type").isString())
        return std::nullopt;
    if (!json.contains("action") || !json.value("action").isString())
        return std::nullopt;
    if (!json.contains("id") || !json.value("id").isString())
        return std::nullopt;
    if (!json.contains("payload") || !json.value("payload").isObject())
        return std::nullopt; // payload missing OR payload:null/number/array (§1.5)
    if (!json.contains("timestamp") || !json.value("timestamp").isDouble())
        return std::nullopt;

    Envelope env;
    env.type = json.value("type").toString();
    if (!isValidType(env.type))
        return std::nullopt; // "notification" etc. => drop (§2.4)

    env.action = json.value("action").toString();
    env.id = json.value("id").toString();
    if (env.id.isEmpty())
        return std::nullopt; // empty id cannot match a pending request

    env.payload = json.value("payload").toObject();
    // toInteger() preserves int64. toInt() would silently truncate any value
    // past INT32_MAX (e.g. 1710000000000) — see interface.md §1.1.
    env.timestamp = json.value("timestamp").toInteger();

    if (env.type == QLatin1String("response")) {
        // §2.2: a response must carry all three response fields at top level.
        const QJsonValue success = json.value("success");
        const QJsonValue errorCode = json.value("error_code");
        const QJsonValue errorMessage = json.value("error_message");
        if (!success.isBool() || !errorCode.isDouble() || !errorMessage.isString())
            return std::nullopt;
        env.hasResponseFields = true;
        env.success = success.toBool();
        env.errorCode = static_cast<int>(errorCode.toInteger());
        env.errorMessage = errorMessage.toString();
    }
    return env;
}

std::optional<Envelope> deserialize(const QJsonDocument& doc)
{
    // A bare JSON array or scalar (e.g. a stray "ok" string) is not an envelope
    // => drop silently (§2.4 "JSON root is not an object"). Raw parse errors are
    // the caller's concern; this overload assumes a successfully parsed document.
    if (!doc.isObject())
        return std::nullopt;
    return deserialize(doc.object());
}

Envelope createCommand(const QString& action, const QJsonObject& payload)
{
    Envelope env;
    env.type = QLatin1String("command");
    env.action = action;
    env.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    env.payload = payload;
    env.timestamp = QDateTime::currentMSecsSinceEpoch();
    return env;
}

Envelope createEvent(const QString& action, const QJsonObject& payload)
{
    Envelope env;
    env.type = QLatin1String("event");
    env.action = action;
    env.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    env.payload = payload;
    env.timestamp = QDateTime::currentMSecsSinceEpoch();
    return env;
}

Envelope createResponse(const QString& originalId, const QString& action,
                        bool success, int errorCode, const QString& errorMessage)
{
    Envelope env;
    env.type = QLatin1String("response");
    env.action = action;
    env.id = originalId; // responses reuse the original command id for matching (§2.3)
    env.payload = QJsonObject{}; // response payload is always {} (§2.2)
    env.timestamp = QDateTime::currentMSecsSinceEpoch();
    env.hasResponseFields = true;
    env.success = success;
    env.errorCode = errorCode;
    env.errorMessage = errorMessage;
    return env;
}
