#include "core/ModelInfoParser.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

// spdlog MUST be included before logging/Logging.hpp — Logging.hpp references
// the SPDLOG_* macros but does NOT include spdlog headers itself (T5 finding
// in .omo/notepads/qt-controller-foundation/learnings.md, reaffirmed by
// PathResolve.cpp).
#include <spdlog/spdlog.h>
#include "logging/Logging.hpp"

namespace core {

namespace {

// Return obj.value(key) as a QJsonObject only when it is actually an object;
// otherwise return a default-constructed (empty) QJsonObject. Used to navigate
// the .model3.json tree without ever throwing on a structural mismatch.
QJsonObject objectField(const QJsonObject& obj, const QString& key)
{
    const QJsonValue v = obj.value(key);
    return v.isObject() ? v.toObject() : QJsonObject{};
}

// Return obj.value(key) as a QJsonArray only when it is actually an array;
// otherwise return a default-constructed (empty) QJsonArray.
QJsonArray arrayField(const QJsonObject& obj, const QString& key)
{
    const QJsonValue v = obj.value(key);
    return v.isArray() ? v.toArray() : QJsonArray{};
}

// Extract FileReferences.Motions.<group> → group → array length.
// Iterates every key under Motions; keys whose value is not an array are
// recorded with length 0 (matches the Java reference, which puts every key
// into the map regardless of value type).
QMap<QString, int> extractMotionGroups(const QJsonObject& root)
{
    QMap<QString, int> groups;
    const QJsonObject fileRefs = objectField(root, QStringLiteral("FileReferences"));
    const QJsonObject motions = objectField(fileRefs, QStringLiteral("Motions"));
    for (auto it = motions.begin(); it != motions.end(); ++it) {
        const QJsonValue v = it.value();
        groups.insert(it.key(), v.isArray() ? v.toArray().size() : 0);
    }
    return groups;
}

// Extract FileReferences.Expressions[].Name → QStringList in document order.
// Elements that are not objects or lack a non-string Name are skipped (the
// Java reference guards `isJsonNull` the same way).
QStringList extractExpressions(const QJsonObject& root)
{
    QStringList expressions;
    const QJsonObject fileRefs = objectField(root, QStringLiteral("FileReferences"));
    const QJsonArray arr = arrayField(fileRefs, QStringLiteral("Expressions"));
    for (const QJsonValue& item : arr) {
        if (!item.isObject())
            continue;
        const QJsonObject expr = item.toObject();
        const QJsonValue name = expr.value(QStringLiteral("Name"));
        if (name.isString())
            expressions.append(name.toString());
    }
    return expressions;
}

// Extract HitAreas[].Name → QStringList in document order.
QStringList extractHitAreas(const QJsonObject& root)
{
    QStringList hitAreas;
    const QJsonArray arr = arrayField(root, QStringLiteral("HitAreas"));
    for (const QJsonValue& item : arr) {
        if (!item.isObject())
            continue;
        const QJsonObject area = item.toObject();
        const QJsonValue name = area.value(QStringLiteral("Name"));
        if (name.isString())
            hitAreas.append(name.toString());
    }
    return hitAreas;
}

} // namespace

std::optional<ModelInfo> parseModelInfo(const QString& model3JsonPath)
{
    QFile file(model3JsonPath);
    if (!file.exists()) {
        LOG_WARN("ModelInfoParser: file does not exist: \"{}\"",
                 model3JsonPath.toStdString());
        return std::nullopt;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        LOG_WARN("ModelInfoParser: cannot open for read: \"{}\"",
                 model3JsonPath.toStdString());
        return std::nullopt;
    }

    const QByteArray bytes = file.readAll();
    file.close();

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        LOG_WARN("ModelInfoParser: invalid JSON in \"{}\": {}",
                 model3JsonPath.toStdString(),
                 parseError.errorString().toStdString());
        return std::nullopt;
    }

    const QJsonObject root = doc.object();
    ModelInfo info;
    info.motionGroups = extractMotionGroups(root);
    info.expressions = extractExpressions(root);
    info.hitAreas = extractHitAreas(root);
    return info;
}

} // namespace core
