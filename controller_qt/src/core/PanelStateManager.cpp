#include "core/PanelStateManager.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonParseError>
#include <QUuid>
#include <QSaveFile>

#include "core/ConfigDir.hpp"
#include "core/InstanceConfig.hpp"
#include "core/InstanceConfigManager.hpp"

// spdlog MUST be included before logging/Logging.hpp — Logging.hpp references
// the SPDLOG_* macros but does NOT include spdlog headers itself (T5 finding in
// .omo/notepads/qt-controller-foundation/learnings.md).
#include <spdlog/spdlog.h>
#include "logging/Logging.hpp"

namespace {

// ── Typed readers for the LEGACY panel-state.json format ────────────────────
// The legacy file the JavaFX controller wrote (controller/.../core/
// PanelStateManager.java) nests panel geometry under a `panel` object and
// lists per-instance state under `instances`. These readers mirror the Java
// getXxx() helpers: missing/mistyped keys fall back to the caller-supplied
// default. Used ONLY by migrateLegacy() — the new panel.json is parsed by
// panelConfigFromJson (T19), which reads the FLAT snake_case layout.
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
    const QJsonValue v = json.value(key);
    return v.isDouble() ? v.toInt() : fallback;
}

bool readBool(const QJsonObject& json, QLatin1String key, bool fallback)
{
    const QJsonValue v = json.value(key);
    return v.isBool() ? v.toBool() : fallback;
}

} // namespace

PanelStateManager::PanelStateManager(const QString& basePath, QObject* parent)
    : QObject(parent), m_basePath(basePath)
{
}

PanelConfig PanelStateManager::load()
{
    const QString panelPath = panelJsonPath();

    // Branch 1: panel.json exists → parse via T19 serde.
    if (QFile::exists(panelPath)) {
        QFile f(panelPath);
        if (!f.open(QIODevice::ReadOnly)) {
            LOG_WARN("Failed to open panel.json \"{}\" for reading; using defaults",
                     panelPath.toStdString());
            return defaultPanelConfig();
        }
        const QByteArray raw = f.readAll();
        f.close();

        QJsonParseError parseErr;
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseErr);
        if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
            // Corrupt panel.json — degrade to defaults. Unlike the legacy case,
            // we do NOT rename/backup the user's panel.json (it may be manually
            // recoverable); we just ignore it for this run.
            LOG_WARN("Corrupt panel.json \"{}\" ({}); using defaults",
                     panelPath.toStdString(),
                     parseErr.errorString().toStdString());
            return defaultPanelConfig();
        }
        // panelConfigFromJson never throws; merges missing/wrong-typed fields
        // from the struct defaults (interface.md §1.5).
        return panelConfigFromJson(doc.object());
    }

    // Branch 2: legacy panel-state.json exists → migrate.
    const QString legacyPath = legacyStatePath();
    if (QFile::exists(legacyPath)) {
        LOG_INFO("Migrating legacy panel-state.json to new format");
        return migrateLegacy();
    }

    // Branch 3: fresh install — write defaults so the next load() hits branch 1.
    const PanelConfig defaults = defaultPanelConfig();
    if (!save(defaults)) {
        LOG_WARN("Failed to write default panel.json on fresh install \"{}\"",
                 panelPath.toStdString());
    }
    return defaults;
}

bool PanelStateManager::save(const PanelConfig& cfg)
{
    // Serialize via T19 serde (FLAT snake_case layout, 12 keys) and write
    // atomically so a crash mid-write cannot leave a half-written panel.json.
    const QJsonDocument doc(panelConfigToJson(cfg));
    const QByteArray data = doc.toJson(QJsonDocument::Compact);

    // Ensure the config dir exists before the atomic write — a fresh install
    // (or a temp-dir basePath in tests) has no config root yet. mkpath is
    // idempotent. Production callers usually pre-create the tree via
    // ConfigDir::ensureDirectories(); doing it here means save() is
    // self-contained.
    const QString dir = QFileInfo(panelJsonPath()).absolutePath();
    if (!QDir().mkpath(dir)) {
        LOG_WARN("save: failed to create config dir \"{}\"", dir.toStdString());
        return false;
    }
    return atomicWrite(panelJsonPath(), data);
}

PanelConfig PanelStateManager::migrateLegacy()
{
    // Legacy format (Java PanelStateManager.java, the authoritative source for
    // what real users have on disk):
    //   {
    //     "panel": {"x":100, "y":200, "width":1200, "height":760, "theme":"..."},
    //     "instances": [
    //       {"label":"...", "model":"...", "renderer_path":"...",
    //        "pos_x":1200, "pos_y":600, "window_width":400, "window_height":500,
    //        "opacity":1.0, "drag_mode":"direct", "idle_interval":10,
    //        "target_fps":0, "auto_start":false, "current_expression":"F01"}
    //     ]
    //   }
    // Each legacy instance gets a FRESH UUID (the Java InstanceState had no id
    // field — ids were runtime-generated). The migrated instance is written to
    // instances/<uuid>.json via InstanceConfigManager (T20 atomic write).
    const QString legacyPath = legacyStatePath();
    QFile f(legacyPath);
    if (!f.open(QIODevice::ReadOnly)) {
        LOG_WARN("migrateLegacy: failed to open \"{}\" for reading; using defaults",
                 legacyPath.toStdString());
        return defaultPanelConfig();
    }
    const QByteArray raw = f.readAll();
    f.close();

    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        // Unparseable legacy → defaults + PRESERVE the old file (no rename).
        // The task spec forbids throwing and forbids deleting/renameing on
        // failure; the user may recover it manually.
        LOG_WARN("Unparseable legacy panel-state.json \"{}\" ({}); "
                 "using defaults, old file preserved",
                 legacyPath.toStdString(),
                 parseErr.errorString().toStdString());
        return defaultPanelConfig();
    }
    const QJsonObject root = doc.object();

    // ── Panel-level fields (nested under "panel") ───────────────────────────
    const PanelConfig defaults = defaultPanelConfig();
    const QJsonObject panelObj = root.value(QStringLiteral("panel")).toObject();
    PanelConfig cfg = defaults;
    cfg.panelX = readInt(panelObj, QLatin1String("x"), cfg.panelX);
    cfg.panelY = readInt(panelObj, QLatin1String("y"), cfg.panelY);
    cfg.panelWidth = readInt(panelObj, QLatin1String("width"), cfg.panelWidth);
    cfg.panelHeight = readInt(panelObj, QLatin1String("height"), cfg.panelHeight);
    cfg.theme = readString(panelObj, QLatin1String("theme"), cfg.theme);

    // ── Per-instance migration → instances/<uuid>.json ──────────────────────
    InstanceConfigManager mgr(m_basePath);
    QStringList instanceIds;
    const QJsonArray instances = root.value(QStringLiteral("instances")).toArray();
    for (const QJsonValue& elem : instances) {
        if (!elem.isObject())
            continue;
        const QJsonObject obj = elem.toObject();

        // Mint a fresh UUID — the legacy InstanceState had no id field.
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);

        // Build the InstanceConfig from the legacy fields, defaulting every
        // field the legacy format didn't carry (layout/subtitle fields etc.
        // take the InstanceConfig struct defaults).
        InstanceConfig inst;
        inst.id = id;
        inst.label = readString(obj, QLatin1String("label"),
                                QStringLiteral("新实例"));
        inst.rendererPath = readString(obj, QLatin1String("renderer_path"),
                                       inst.rendererPath);
        inst.modelName = readString(obj, QLatin1String("model"),
                                    inst.modelName);
        inst.windowX = readInt(obj, QLatin1String("pos_x"), inst.windowX);
        inst.windowY = readInt(obj, QLatin1String("pos_y"), inst.windowY);
        inst.windowWidth = readInt(obj, QLatin1String("window_width"),
                                   inst.windowWidth);
        inst.windowHeight = readInt(obj, QLatin1String("window_height"),
                                    inst.windowHeight);
        inst.opacity = readDouble(obj, QLatin1String("opacity"), inst.opacity);
        inst.dragMode = readString(obj, QLatin1String("drag_mode"),
                                   inst.dragMode);
        inst.idleInterval = readInt(obj, QLatin1String("idle_interval"),
                                    inst.idleInterval);
        inst.targetFps = readInt(obj, QLatin1String("target_fps"),
                                 inst.targetFps);
        inst.autoStart = readBool(obj, QLatin1String("auto_start"),
                                  inst.autoStart);
        inst.currentExpression = readString(obj,
                                            QLatin1String("current_expression"),
                                            inst.currentExpression);

        if (!mgr.save(inst)) {
            LOG_WARN("migrateLegacy: failed to write instance \"{}\"; skipping",
                     id.toStdString());
            continue;
        }
        instanceIds.append(id);
        LOG_INFO("Migrated instance '{}' → {}", inst.label.toStdString(),
                 id.toStdString());
    }
    cfg.instanceIds = instanceIds;

    // Write the new panel.json atomically BEFORE renaming the legacy file, so
    // a crash between the two leaves the legacy file intact (re-runnable).
    if (!save(cfg)) {
        LOG_WARN("migrateLegacy: failed to write panel.json; "
                 "legacy file preserved");
        return cfg;
    }

    // Rename panel-state.json → panel-state.json.bak (backup).
    const QString backupPath = legacyBackupPath();
    // QFile::rename overwrites on POSIX but FAILS on Windows if the target
    // exists. Remove a stale .bak first (idempotent — a prior migration's .bak
    // is disposable; the live state is now in panel.json + instances/).
    if (QFile::exists(backupPath))
        QFile::remove(backupPath);
    if (!QFile::rename(legacyPath, backupPath)) {
        LOG_WARN("migrateLegacy: failed to rename \"{}\" → \"{}\" "
                 "(panel.json already written; legacy file left in place)",
                 legacyPath.toStdString(), backupPath.toStdString());
    } else {
        LOG_INFO("Legacy panel-state.json backed up to \"{}\"",
                 backupPath.toStdString());
    }

    return cfg;
}

QString PanelStateManager::panelJsonPath() const
{
    // Empty basePath → real ConfigDir::configDir() (already ends in '/').
    // Non-empty → normalize to trailing '/' so "<root>panel.json" joins
    // cleanly (mirrors InstanceConfigManager::instancesDir normalization).
    if (m_basePath.isEmpty())
        return ConfigDir::configDir() + QStringLiteral("panel.json");
    QString root = m_basePath;
    if (!root.endsWith(QLatin1Char('/')))
        root += QLatin1Char('/');
    return root + QStringLiteral("panel.json");
}

QString PanelStateManager::legacyStatePath() const
{
    if (m_basePath.isEmpty())
        return ConfigDir::configDir() + QStringLiteral("panel-state.json");
    QString root = m_basePath;
    if (!root.endsWith(QLatin1Char('/')))
        root += QLatin1Char('/');
    return root + QStringLiteral("panel-state.json");
}

QString PanelStateManager::legacyBackupPath() const
{
    if (m_basePath.isEmpty())
        return ConfigDir::configDir() + QStringLiteral("panel-state.json.bak");
    QString root = m_basePath;
    if (!root.endsWith(QLatin1Char('/')))
        root += QLatin1Char('/');
    return root + QStringLiteral("panel-state.json.bak");
}

bool PanelStateManager::atomicWrite(const QString& path,
                                    const QByteArray& data) const
{
    // QSaveFile is Qt's purpose-built atomic-write primitive: it commits via
    // MoveFileEx(MOVEFILE_REPLACE_EXISTING) on Windows and rename(2) on POSIX,
    // so the target is overwritten atomically even when it already exists.
    // abort() cleans up the internal .tmp so a failed write leaves nothing
    // behind. Same pattern as InstanceConfigManager::atomicWrite (T20).
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        LOG_WARN("atomicWrite: failed to open \"{}\" for writing",
                 path.toStdString());
        return false;
    }
    const qint64 written = f.write(data);
    if (written != data.size() || !f.flush()) {
        LOG_WARN("atomicWrite: short write to \"{}\" ({} of {} bytes)",
                 path.toStdString(), written, data.size());
        f.cancelWriting();  // discards the staged .tmp
        return false;
    }
    if (!f.commit()) {
        // commit() already cleaned up the .tmp on failure; just report.
        LOG_WARN("atomicWrite: commit failed for \"{}\"", path.toStdString());
        return false;
    }
    return true;
}
