#include "core/ModelScanner.hpp"

#include <algorithm>

#include <QDir>
#include <QFile>

// spdlog MUST be included before logging/Logging.hpp (see PathResolve.cpp /
// ModelInfoParser.cpp for the same rationale).
#include <spdlog/spdlog.h>
#include "logging/Logging.hpp"

namespace core {

namespace {

// The conventional subdirectory layout under a renderer's bundled Resources/
// tree. Matches the Java reference ModelScanner.RESOURCES_DIR + MODELS_SUBDIR
// and the layout shipped under build/bin/Resources/Models/.
constexpr const char* kResourcesDir = "Resources";
constexpr const char* kModelsSubdir = "Models";

// v1.13: the plugin-data-domain models segment (rev2 §2):
// <pluginDataRoot>/<owner>/Models/<Name>/<Name>.model3.json.
constexpr const char* kPluginModelsSegment = "Models";

// Qualify ONE models-directory (the dir that directly contains <Name>/
// subdirs): every subdir holding <subdir>.model3.json appends
// {name, modelJsonPath} to `out`. Hidden subdirs are NOT skipped — the
// original host-domain scanAvailableModels never filtered them, and the
// host projection must stay byte-identical. Never throws.
void scanSingleModelsSource(const QString& modelsDir,
                            QVector<ScannedModel>* out)
{
    const QDir dir(modelsDir);
    if (!dir.exists()) {
        LOG_DEBUG("ModelScanner: models directory not found: \"{}\"",
                  dir.absolutePath().toStdString());
        return;
    }

    // Dirs only, no . or .. — one subdir per model. Each qualifying subdir
    // contains <subdir>.model3.json.
    const QStringList subdirs = dir.entryList(
        QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& name : subdirs) {
        const QString expected = name + QStringLiteral(".model3.json");
        const QString candidate = QDir(dir.absoluteFilePath(name))
                                      .absoluteFilePath(expected);
        if (QFile::exists(candidate)) {
            out->append(ScannedModel{name,
                                     QString(),
                                     QDir::cleanPath(candidate)});
        }
    }
}

} // namespace

QStringList scanAvailableModels(const QString& rendererDir)
{
    // v1.13: host-only projection of scanModelsScoped — behavior (and the
    // existing tests) preserved byte-for-byte: same qualifier, same
    // case-insensitive sort of bare names, same empty-on-miss contract.
    if (rendererDir.isEmpty()) {
        LOG_DEBUG("ModelScanner: rendererDir is empty");
        return {};
    }

    const QDir modelsDir(QDir(rendererDir).absoluteFilePath(
        QDir::cleanPath(QString::fromLatin1(kResourcesDir) + '/'
                        + QString::fromLatin1(kModelsSubdir))));

    QVector<ScannedModel> entries;
    scanSingleModelsSource(modelsDir.absolutePath(), &entries);

    QStringList models;
    models.reserve(entries.size());
    for (const ScannedModel& e : entries)
        models.append(e.name);

    // Case-insensitive sort so the ComboBox is stable across platforms
    // (QDir::entryList ordering is filesystem-dependent on Linux ext4, where
    // it is case-sensitive; Java's Collections.sort is case-sensitive too,
    // but the UI consumer wants a predictable order regardless of FS).
    std::sort(models.begin(), models.end(),
              [](const QString& a, const QString& b) {
                  return a.compare(b, Qt::CaseInsensitive) < 0;
              });

    LOG_DEBUG("ModelScanner: found {} models under \"{}\": {}",
              models.size(),
              modelsDir.absolutePath().toStdString(),
              models.join(QStringLiteral(", ")).toStdString());
    return models;
}

QVector<ScannedModel> scanModelsScoped(const QString& rendererDir,
                                       const QString& pluginDataRoot)
{
    QVector<ScannedModel> entries;

    // Host domain first — exactly today's discovery (owner="").
    if (!rendererDir.isEmpty()) {
        const QDir modelsDir(QDir(rendererDir).absoluteFilePath(
            QDir::cleanPath(QString::fromLatin1(kResourcesDir) + '/'
                            + QString::fromLatin1(kModelsSubdir))));
        scanSingleModelsSource(modelsDir.absolutePath(), &entries);
    } else {
        LOG_DEBUG("ModelScanner: rendererDir is empty");
    }

    // Plugin data domains (rev2 §3.2c): PURE directory traversal of the
    // immediate subdirectories — never the plugin registry. Disabled or
    // unloaded plugins' domains stay scannable.
    if (!pluginDataRoot.isEmpty()) {
        const QDir root(pluginDataRoot);
        if (root.exists()) {
            const QStringList owners = root.entryList(
                QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QString& owner : owners) {
                if (owner.startsWith(QLatin1Char('.')))
                    continue;
                const int before = entries.size();
                scanSingleModelsSource(
                    QDir(root.absoluteFilePath(owner))
                        .absoluteFilePath(
                            QString::fromLatin1(kPluginModelsSegment)),
                    &entries);
                // Stamp the owner onto the just-appended entries (the
                // helper itself always writes owner="").
                for (int i = before; i < entries.size(); ++i)
                    entries[i].owner = owner;
            }
        } else {
            LOG_DEBUG("ModelScanner: pluginDataRoot not found: \"{}\"",
                      root.absolutePath().toStdString());
        }
    }

    // Deterministic order: (owner, name) case-insensitive — host ("")
    // entries first, then per-owner blocks.
    std::sort(entries.begin(), entries.end(),
              [](const ScannedModel& a, const ScannedModel& b) {
                  const int byOwner = a.owner.compare(b.owner,
                                                      Qt::CaseInsensitive);
                  if (byOwner != 0)
                      return byOwner < 0;
                  return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
              });

    LOG_DEBUG("ModelScanner: scoped scan found {} models "
              "(renderer=\"{}\" pluginDataRoot=\"{}\")",
              entries.size(), rendererDir.toStdString(),
              pluginDataRoot.toStdString());
    return entries;
}

QString resolvePluginModelPath(const QString& pluginDataRoot,
                               const QString& pluginId,
                               const QString& modelName)
{
    // Strict domain resolution (rev2 §3.2c): probe EXACTLY
    // <pluginDataRoot>/<pluginId>/Models/<name>/<name>.model3.json — no
    // host-domain fallback, no registry lookup. Pure existence check.
    if (pluginDataRoot.isEmpty() || pluginId.isEmpty() || modelName.isEmpty())
        return {};
    const QDir domain(QDir(pluginDataRoot).absoluteFilePath(pluginId));
    const QString candidate =
        QDir(domain.absoluteFilePath(
                 QString::fromLatin1(kPluginModelsSegment)))
            .absoluteFilePath(modelName + QLatin1Char('/')
                              + modelName + QStringLiteral(".model3.json"));
    return QFile::exists(candidate) ? QDir::cleanPath(candidate) : QString();
}

} // namespace core
