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

} // namespace core
