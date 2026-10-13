#include "core/EventCatalog.hpp"

#include <QJsonArray>

// EventCatalog (S8a, v1.5) — implementation. Every builder emits the
// exact key set documented in api/EventIds.hpp; QJsonObject::insert
// never throws on string keys, so the whole file stays inside the
// never-throws contract.

namespace core {

QJsonObject makeReadyPayload(const QJsonObject& wsPayload)
{
    // Verbatim: the bus consumer sees exactly what the renderer sent.
    return wsPayload;
}

QJsonObject makeHitPayload(const QString& areaId, double x, double y,
                           int button)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("area_id"), areaId);
    payload.insert(QStringLiteral("x"), x);
    payload.insert(QStringLiteral("y"), y);
    payload.insert(QStringLiteral("button"), button);
    return payload;
}

QJsonObject makeMotionStartedPayload(const QString& group, int index)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("group"), group);
    payload.insert(QStringLiteral("index"), index);
    return payload;
}

QJsonObject makeMotionFinishedPayload(const QString& group, int index)
{
    // Variant A (play_motion): the motion_path sentinel is "".
    QJsonObject payload;
    payload.insert(QStringLiteral("group"), group);
    payload.insert(QStringLiteral("index"), index);
    payload.insert(QStringLiteral("motion_path"), QString());
    return payload;
}

QJsonObject makeMotionFinishedPayload(const QString& motionPath)
{
    // Variant B (play_motion_ext): the group/index sentinels are ""/-1.
    // (Overload pair, not a discriminator inside one function — the
    // emission site already knows the wire variant, and the two shapes
    // stay impossible to confuse at the call site.)
    QJsonObject payload;
    payload.insert(QStringLiteral("group"), QString());
    payload.insert(QStringLiteral("index"), -1);
    payload.insert(QStringLiteral("motion_path"), motionPath);
    return payload;
}

QJsonObject makeIdleTickPayload(const QString& group, int index)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("group"), group);
    payload.insert(QStringLiteral("index"), index);
    return payload;
}

QJsonObject makeAudioFinishedPayload(const QJsonObject& wsPayload)
{
    // Verbatim (the makeReadyPayload discipline): the bus consumer sees
    // exactly what the renderer sent — audio_path is already snake_case.
    return wsPayload;
}

QJsonObject makeExpressionAppliedPayload(const QString& expressionId)
{
    // events.md §16: one key, verbatim — the set_expression receipt.
    QJsonObject payload;
    payload.insert(QStringLiteral("expression_id"), expressionId);
    return payload;
}

QJsonObject makeFocusChangedPayload(bool focused)
{
    // events.md §18: one key, verbatim — the GLFW focus-callback half.
    QJsonObject payload;
    payload.insert(QStringLiteral("focused"), focused);
    return payload;
}

QJsonObject makeMotionProgressPayload(const QString& group, int index,
                                      bool hasProgress, double progress)
{
    // play_motion variant (events.md §17): {group, index} + the
    // OPTIONAL progress — inserted only when the wire carried it
    // (unknown-duration motions omit the key; the host never
    // fabricates a value).
    QJsonObject payload;
    payload.insert(QStringLiteral("group"), group);
    payload.insert(QStringLiteral("index"), index);
    if (hasProgress)
        payload.insert(QStringLiteral("progress"), progress);
    return payload;
}

QJsonObject makeMotionProgressPayload(const QString& motionPath,
                                      bool hasProgress, double progress)
{
    // play_motion_ext variant (events.md §17): {motion_path} + the
    // optional progress (same omission rule as the group variant).
    QJsonObject payload;
    payload.insert(QStringLiteral("motion_path"), motionPath);
    if (hasProgress)
        payload.insert(QStringLiteral("progress"), progress);
    return payload;
}

QJsonObject makeDragStartedPayload(double x, double y)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("x"), x);
    payload.insert(QStringLiteral("y"), y);
    return payload;
}

QJsonObject makeDragEndedPayload(double x, double y, int windowX,
                                 int windowY)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("x"), x);
    payload.insert(QStringLiteral("y"), y);
    payload.insert(QStringLiteral("window_x"), windowX);
    payload.insert(QStringLiteral("window_y"), windowY);
    return payload;
}

QJsonObject makeErrorPayload(const QJsonObject& wsPayload)
{
    // Verbatim (the makeReadyPayload discipline): the bus consumer sees
    // exactly what the renderer sent — error_code/error_message are
    // already snake_case per events.md §9.
    return wsPayload;
}

QJsonObject makeDialoguePayload(const QString& text, int durationMs)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("text"), text);
    payload.insert(QStringLiteral("duration_ms"), durationMs);
    return payload;
}

QJsonObject makeRestartScheduledPayload(int attempt, int delayMs)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("attempt"), attempt);
    payload.insert(QStringLiteral("delay_ms"), delayMs);
    return payload;
}

QJsonObject makeGaveUpPayload(int attempts)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("attempts"), attempts);
    return payload;
}

QJsonObject makeLayoutChangedPayload(const QJsonObject& wsPayload)
{
    // Verbatim: offset_x/offset_y/scale arrive snake_case on the wire
    // (events.md §12) — same passthrough as makeReadyPayload.
    return wsPayload;
}

QJsonObject makeWindowResizedPayload(const QJsonObject& wsPayload)
{
    // Verbatim: window_width/window_height/window_x/window_y arrive
    // snake_case on the wire (events.md §13).
    return wsPayload;
}

QJsonObject makeModelLoadedPayload(const QString& modelId,
                                   const QStringList& motionGroups,
                                   const QStringList& expressions)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("model_id"), modelId);
    payload.insert(QStringLiteral("motion_groups"),
                   QJsonArray::fromStringList(motionGroups));
    payload.insert(QStringLiteral("expressions"),
                   QJsonArray::fromStringList(expressions));
    return payload;
}

QJsonObject makeModelLoadFailedPayload(int errorCode,
                                       const QString& errorMessage)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("error_code"), errorCode);
    payload.insert(QStringLiteral("error_message"), errorMessage);
    return payload;
}

QJsonObject makePackPayload(const QString& packId)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("pack_id"), packId);
    return payload;
}

QJsonObject makeSettingsChangedPayload(const QString& field,
                                       const QJsonValue& value)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("field"), field);
    payload.insert(QStringLiteral("value"), value);
    return payload;
}

QJsonObject makePluginLifecyclePayload(const QString& pluginId)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("plugin_id"), pluginId);
    return payload;
}

QJsonObject makeWindowStatePayload(const QString& state, bool visible)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("state"), state);
    payload.insert(QStringLiteral("visible"), visible);
    return payload;
}

QJsonObject makeTrayActivatedPayload(const QString& action)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("action"), action);
    return payload;
}

} // namespace core
