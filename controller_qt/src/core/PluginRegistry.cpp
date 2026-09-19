#include "core/PluginRegistry.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>

#include <algorithm>

#include <spdlog/spdlog.h>

#include "api/PluginTypes.hpp"
#include "logging/Logging.hpp"

// Manifest schema (§B.6): the authoritative field/type rules. The
// configure-time CMake check only verifies field existence + api_version
// shape; everything below is the runtime contract.

namespace core {

QString pluginStatusToString(PluginStatus status)
{
    switch (status) {
    case PluginStatus::Discovered: return QStringLiteral("discovered");
    case PluginStatus::Validated:   return QStringLiteral("validated");
    case PluginStatus::Registered:  return QStringLiteral("registered");
    case PluginStatus::Started:     return QStringLiteral("started");
    case PluginStatus::Stopped:     return QStringLiteral("stopped");
    case PluginStatus::Failed:      return QStringLiteral("failed");
    }
    return QStringLiteral("unknown");
}

namespace {

bool requireString(const QJsonObject& obj, const char* key, QString* out,
                   QString* error, const QString& context)
{
    const QJsonValue v = obj.value(QLatin1String(key));
    if (!v.isString() || v.toString().trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("%1: field '%2' must be a non-empty string")
                         .arg(context, QLatin1String(key));
        return false;
    }
    *out = v.toString().trimmed();
    return true;
}

} // namespace

std::optional<PluginManifest> PluginRegistry::parseManifest(const QString& manifestJson,
                                                            QString* error)
{
    const QString ctx = QStringLiteral("plugin.json");
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(manifestJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error)
            *error = QStringLiteral("%1: not valid JSON (%2)")
                         .arg(ctx, parseError.errorString());
        return std::nullopt;
    }
    const QJsonObject obj = doc.object();

    PluginManifest m;
    if (!requireString(obj, "id", &m.id, error, ctx))
        return std::nullopt;
    // Reverse-domain shape: at least one dot, [A-Za-z0-9.-] only (§B.6).
    if (!m.id.contains(QLatin1Char('.')) ||
        m.id.contains(QLatin1Char(' ')) ||
        m.id.contains(QLatin1Char('/'))) {
        if (error)
            *error = QStringLiteral("%1: id '%2' must be reverse-domain "
                                    "(contain '.'; no spaces or path separators)")
                         .arg(ctx, m.id);
        return std::nullopt;
    }
    if (!requireString(obj, "version", &m.version, error, ctx))
        return std::nullopt;

    // api_version: "<major>.<minor>" numeric string (never floats — a
    // "1.10" minor must not collapse to 1.1).
    const QString apiVersion = obj.value(QStringLiteral("api_version")).toString();
    const QStringList parts = apiVersion.split(QLatin1Char('.'));
    bool majorOk = false, minorOk = false;
    const int major = parts.size() >= 1 ? parts.at(0).toInt(&majorOk) : -1;
    const int minor = parts.size() >= 2 ? parts.at(1).toInt(&minorOk) : -1;
    if (parts.size() != 2 || !majorOk || !minorOk || major < 0 || minor < 0) {
        if (error)
            *error = QStringLiteral("%1: api_version '%2' must be \"<major>.<minor>\" "
                                    "with non-negative integers")
                         .arg(ctx, apiVersion);
        return std::nullopt;
    }
    m.apiMajor = major;
    m.apiMinor = minor;

    // capabilities: optional array of strings; anything else is invalid.
    const QJsonValue caps = obj.value(QStringLiteral("capabilities"));
    if (!caps.isUndefined()) {
        if (!caps.isArray()) {
            if (error)
                *error = QStringLiteral("%1: 'capabilities' must be an array").arg(ctx);
            return std::nullopt;
        }
        const QJsonArray arr = caps.toArray();
        for (const QJsonValue& c : arr) {
            if (!c.isString()) {
                if (error)
                    *error = QStringLiteral("%1: capabilities entries must be strings")
                                 .arg(ctx);
                return std::nullopt;
            }
            m.capabilities.append(c.toString());
        }
    }

    // UI fields (optional): entry.qml, title, icon, order, vendor.
    m.entryQml = obj.value(QStringLiteral("entry.qml")).toString().trimmed();
    m.title = obj.value(QStringLiteral("title")).toString().trimmed();
    m.icon = obj.value(QStringLiteral("icon")).toString().trimmed();
    m.vendor = obj.value(QStringLiteral("vendor")).toString().trimmed();
    m.order = obj.value(QStringLiteral("order")).toInt(100);
    if (obj.contains(QStringLiteral("order")) &&
        !obj.value(QStringLiteral("order")).isDouble()) {
        if (error)
            *error = QStringLiteral("%1: 'order' must be an integer").arg(ctx);
        return std::nullopt;
    }

    return m;
}

bool PluginRegistry::validateApiVersion(int pluginMajor, int pluginMinor, QString* error)
{
    if (pluginMajor > pet::kApiMajor ||
        (pluginMajor == pet::kApiMajor && pluginMinor > pet::kApiMinor)) {
        if (error)
            *error = QStringLiteral("api_version %1.%2 exceeds host API %3.%4")
                         .arg(pluginMajor).arg(pluginMinor)
                         .arg(pet::kApiMajor).arg(pet::kApiMinor);
        return false;
    }
    return true;
}

namespace {

// "6.10.2" → {6, 10} (patch ignored per §A.2 v2).
std::pair<int, int> majorMinor(const QString& version)
{
    const QStringList parts = version.split(QLatin1Char('.'));
    bool okMaj = false, okMin = false;
    const int maj = parts.size() >= 1 ? parts.at(0).toInt(&okMaj) : -1;
    const int min = parts.size() >= 2 ? parts.at(1).toInt(&okMin) : 0;
    if (!okMaj || !okMin)
        return {-1, -1};
    return {maj, min};
}

} // namespace

bool PluginRegistry::validateAbi(const QJsonObject& pluginAbi, const QJsonObject& hostAbi,
                                 QString* error)
{
    const auto strField = [](const QJsonObject& o, const char* k) {
        return o.value(QLatin1String(k)).toString();
    };
    struct StrictEqual { const char* key; };
    const StrictEqual strict[] = {{"compiler"}, {"compiler_version"},
                                  {"qt_build"}, {"build_type"}};
    for (const StrictEqual& f : strict) {
        const QString p = strField(pluginAbi, f.key);
        const QString h = strField(hostAbi, f.key);
        if (p.isEmpty() || h.isEmpty()) {
            if (error)
                *error = QStringLiteral("abi field '%1' missing (plugin='%2' host='%3')")
                             .arg(QLatin1String(f.key), p, h);
            return false;
        }
        if (p != h) {
            if (error)
                *error = QStringLiteral("abi mismatch on '%1': plugin='%2' host='%3'")
                             .arg(QLatin1String(f.key), p, h);
            return false;
        }
    }
    // qt_version: same major, plugin minor <= host minor, patch free.
    const auto [pMaj, pMin] = majorMinor(strField(pluginAbi, "qt_version"));
    const auto [hMaj, hMin] = majorMinor(strField(hostAbi, "qt_version"));
    if (pMaj < 0 || hMaj < 0) {
        if (error)
            *error = QStringLiteral("abi 'qt_version' unparseable (plugin='%1' host='%2')")
                         .arg(strField(pluginAbi, "qt_version"),
                              strField(hostAbi, "qt_version"));
        return false;
    }
    if (pMaj != hMaj || pMin > hMin) {
        if (error)
            *error = QStringLiteral("abi qt_version mismatch: plugin=%1.%2 host=%3.%4 "
                                    "(same major required, plugin minor <= host minor)")
                         .arg(pMaj).arg(pMin).arg(hMaj).arg(hMin);
        return false;
    }
    return true;
}

bool PluginRegistry::legalTransition(PluginStatus from, PluginStatus to) const
{
    if (to == PluginStatus::Failed)
        return true; // Failed is reachable from anywhere (§B.6)
    switch (from) {
    case PluginStatus::Discovered: return to == PluginStatus::Validated;
    case PluginStatus::Validated:  return to == PluginStatus::Registered;
    case PluginStatus::Registered: return to == PluginStatus::Started;
    case PluginStatus::Started:    return to == PluginStatus::Stopped;
    case PluginStatus::Stopped:
    case PluginStatus::Failed:     return false; // terminal
    }
    return false;
}

bool PluginRegistry::transition(const QString& id, PluginStatus to,
                                const QString& errorMessage)
{
    PluginEntry* e = entry(id);
    if (e == nullptr)
        return false;
    if (!legalTransition(e->status, to)) {
        LOG_WARN("PluginRegistry: illegal transition for '{}' ({} → {})",
                 id.toStdString(),
                 pluginStatusToString(e->status).toStdString(),
                 pluginStatusToString(to).toStdString());
        return false;
    }
    e->status = to;
    if (to == PluginStatus::Failed)
        e->errorMessage = errorMessage;
    return true;
}

void PluginRegistry::addStaticPlugin(const QString& manifestJson, CreateFn create,
                                     const QString& abiJson, const QString& qmlUrl)
{
    PluginEntry e;
    e.create = create;
    e.source = QStringLiteral("static");
    e.abiExempt = true; // §A.2: same-repo same-build — no abi gating

    QString parseError;
    auto manifest = parseManifest(manifestJson, &parseError);
    if (!manifest.has_value()) {
        // Failed entries stay visible for the management UI (§B.6: never
        // silently drop a broken plugin).
        if (manifestJson.isEmpty()) {
            // zero-plugin builds generate no rows; only reachable via tests
            e.manifest.id = QStringLiteral("<unknown>");
        } else {
            // recover the id for display when at least the id parsed
            const auto doc = QJsonDocument::fromJson(manifestJson.toUtf8());
            if (doc.isObject())
                e.manifest.id = doc.object().value(QStringLiteral("id")).toString(
                    QStringLiteral("<invalid-manifest>"));
            else
                e.manifest.id = QStringLiteral("<invalid-manifest>");
        }
        e.status = PluginStatus::Failed;
        e.errorMessage = parseError;
        LOG_ERROR("PluginRegistry: manifest rejected for '{}': {}",
                  e.manifest.id.toStdString(), parseError.toStdString());
        m_entries.append(e);
        return;
    }
    e.manifest = *manifest;
    // Build-injected page URL (§B.6): overrides whatever the manifest's
    // entry.qml file name implies — qrc layout knowledge lives in CMake.
    if (!qmlUrl.isEmpty())
        e.manifest.qmlUrl = qmlUrl;

    QString versionError;
    if (!validateApiVersion(e.manifest.apiMajor, e.manifest.apiMinor, &versionError)) {
        e.status = PluginStatus::Failed;
        e.errorMessage = versionError;
        LOG_ERROR("PluginRegistry: api_version gate rejected '{}': {}",
                  e.manifest.id.toStdString(), versionError.toStdString());
        m_entries.append(e);
        return;
    }

    if (!abiJson.isEmpty()) {
        // Embedded for provenance/display; static plugins are exempt from
        // gating (§A.2) — the abi block documents the build it shipped in.
        LOG_DEBUG("PluginRegistry: plugin '{}' built with abi {}",
                  e.manifest.id.toStdString(), abiJson.toStdString());
    }

    // Discovered → Validated → Registered compressed: static plugins are
    // validated at registration (no discovery phase, §B.6 stage-1).
    e.status = PluginStatus::Registered;
    LOG_INFO("PluginRegistry: registered '{}' v{} (api {}.{}, order {}, capabilities: {})",
             e.manifest.id.toStdString(), e.manifest.version.toStdString(),
             e.manifest.apiMajor, e.manifest.apiMinor, e.manifest.order,
             e.manifest.capabilities.join(QLatin1Char(',')).toStdString());
    m_entries.append(e);
}

PluginEntry* PluginRegistry::entry(const QString& id)
{
    for (PluginEntry& e : m_entries) {
        if (e.manifest.id == id)
            return &e;
    }
    return nullptr;
}

const PluginEntry* PluginRegistry::entry(const QString& id) const
{
    for (const PluginEntry& e : m_entries) {
        if (e.manifest.id == id)
            return &e;
    }
    return nullptr;
}

QList<PluginEntry> PluginRegistry::orderedEntries() const
{
    QList<PluginEntry> sorted = m_entries;
    std::sort(sorted.begin(), sorted.end(),
              [](const PluginEntry& a, const PluginEntry& b) {
                  if (a.manifest.order != b.manifest.order)
                      return a.manifest.order < b.manifest.order;
                  return a.manifest.id < b.manifest.id;
              });
    return sorted;
}

} // namespace core
