#pragma once

#include <QJsonObject>
#include <QJsonDocument>
#include <QString>
#include <optional>

// Protocol Envelope — the single wire format shared by every WebSocket message
// (interface.md §2). Three message types share the same shape:
//   - command  : controller (WS Server) -> renderer (WS Client)
//   - event    : renderer -> controller
//   - response : renderer -> controller, replying to a command by reused id
//
// Invariants enforced by serialize()/deserialize() and asserted by EnvelopeTest:
//   - payload is ALWAYS a JSON object, never null and never omitted (§1.5, §2.1).
//     Empty payload serializes as {}.
//   - success / error_code / error_message are written to JSON ONLY when
//     type == "response" (§2.3), and they sit at the JSON TOP LEVEL as siblings
//     of type/action/id — they are NEVER placed inside payload (§2.2).
//   - timestamp is a 64-bit Unix-millisecond value. It is serialized via
//     QJsonValue(qint64) and parsed via QJsonValue::toInteger(); toInt() would
//     truncate values past INT32_MAX such as 1710000000000 (§1.1).
struct Envelope {
    QString type;         // "command" | "event" | "response"
    QString action;
    QString id;           // unique id, reused by responses for request matching
    QJsonObject payload;  // ALWAYS an object (never null); {} when empty
    qint64 timestamp = 0; // Unix ms (int64)

    // Response-only fields. hasResponseFields mirrors (type == "response");
    // the other three are meaningful only when hasResponseFields is true.
    bool hasResponseFields = false;
    bool success = false;
    int errorCode = 0;
    QString errorMessage;

    // Value equality — used by round-trip identity tests. QJsonObject compares
    // by content, so payloads must match key-for-key (order-independent).
    bool operator==(const Envelope& other) const {
        return type == other.type
            && action == other.action
            && id == other.id
            && payload == other.payload
            && timestamp == other.timestamp
            && hasResponseFields == other.hasResponseFields
            && success == other.success
            && errorCode == other.errorCode
            && errorMessage == other.errorMessage;
    }
};

// Serialize an Envelope to a JSON document with snake_case keys (§2.3).
// Writes success / error_code / error_message ONLY when type == "response".
QJsonDocument serialize(const Envelope& env);

// Deserialize a JSON object into an Envelope. Returns std::nullopt for any
// invalid message, which the caller silently drops (§2.4). Invalid means:
//   - any of type/action/id/payload/timestamp missing or wrong-typed;
//   - type not one of command / event / response;
//   - id present but empty (cannot match a pending request);
//   - payload present but not an object (null / array / scalar — §1.5);
//   - type == "response" but any of success/error_code/error_message missing
//     or wrong-typed.
std::optional<Envelope> deserialize(const QJsonObject& json);

// Deserialize a full JSON document — the realistic wire-message entry point.
// Returns std::nullopt when the document root is not an object (e.g. a bare
// JSON array or string), or when the object form (above) rejects it. Raw JSON
// parse failures are the caller's concern; this overload assumes a parsed doc.
std::optional<Envelope> deserialize(const QJsonDocument& doc);

// Thin construction helpers. Full factories (id strategy, timestamp source,
// payload validation, error-code policy) arrive in T9; these intentionally do
// only the minimum so T4 tests can build well-formed envelopes concisely.
Envelope createCommand(const QString& action, const QJsonObject& payload = {});
Envelope createEvent(const QString& action, const QJsonObject& payload = {});
Envelope createResponse(const QString& originalId, const QString& action,
                        bool success, int errorCode = 0,
                        const QString& errorMessage = {});
