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
// and the layout build.py ships under build/bin/Resources/Models/.
constexpr const char* kResourcesDir = "Resources";
constexpr const char* kModelsSubdir = "Models";

} // namespace

QStringList scanAvailableModels(const QString& rendererDir)
{
    if (rendererDir.isEmpty()) {
        LOG_DEBUG("ModelScanner: rendererDir is empty");
        return {};
    }

    const QDir modelsDir(QDir(rendererDir).absoluteFilePath(
        QDir::cleanPath(QString::fromLatin1(kResourcesDir) + '/'
                        + QString::fromLatin1(kModelsSubdir))));
    if (!modelsDir.exists()) {
        LOG_DEBUG("ModelScanner: models directory not found: \"{}\"",
                  modelsDir.absolutePath().toStdString());
        return {};
    }

    // Dirs only, no . or .. — the Cubism Samples layout is one subdir per
    // model. Each qualifying subdir contains <subdir>.model3.json.
    const QStringList subdirs = modelsDir.entryList(
        QDir::Dirs | QDir::NoDotAndDotDot);

    QStringList models;
    models.reserve(subdirs.size());
    for (const QString& name : subdirs) {
        const QString expected = name + QStringLiteral(".model3.json");
        const QString candidate = QDir(modelsDir.absoluteFilePath(name))
                                      .absoluteFilePath(expected);
        if (QFile::exists(candidate)) {
            models.append(name);
        }
    }

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

} // namespace core
