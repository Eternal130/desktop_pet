#include "ui/ModelController.hpp"

#include <QDesktopServices>
#include <QDir>
#include <QUrl>

#include <spdlog/spdlog.h>
#include "logging/Logging.hpp"
#include "core/ModelInfoParser.hpp"
#include "core/ModelScanner.hpp"

ModelController::ModelController(QObject* parent)
    : QObject(parent)
{
    // Mirror VoicePackController: scan once at construction so a QML page
    // binding modelCount before setRendererDir sees a valid (if empty)
    // roster. scanAvailableModels on an empty dir is a cheap no-op.
    rescan();
}

void ModelController::setRendererDir(const QString& dir)
{
    m_rendererDir = dir;
    // Re-scan immediately — PanelUiBoot injects the dir during boot, before
    // QML binds, so the roster is populated by the time the page renders.
    rescan();
}

void ModelController::rescan()
{
    m_models.clear();
    // Display dir: the conventional location even when nothing is bundled
    // yet (the empty-state hint points the user there); empty rendererDir →
    // empty dir (all accessors stay safe, nothing to point at).
    m_modelsDir = m_rendererDir.isEmpty()
        ? QString()
        : QDir(m_rendererDir).absoluteFilePath(
              QStringLiteral("Resources/Models"));

    const QStringList names = core::scanAvailableModels(m_rendererDir);
    for (const QString& name : names) {
        Entry entry;
        entry.dirName = name;
        // Pre-parse the .model3.json NOW (rescan is user-initiated, not a
        // per-frame path) so the accessors are pure cache lookups. A missing
        // / corrupt file degrades to empty detail lists — the roster entry
        // survives (the model still launches; the detail card shows zeros).
        const QString model3Json = QDir(m_modelsDir).absoluteFilePath(
            name + QLatin1Char('/') + name + QStringLiteral(".model3.json"));
        const auto info = core::parseModelInfo(model3Json);
        if (info.has_value()) {
            entry.motionGroups = info->motionGroups.keys();
            entry.expressions = info->expressions;
            entry.hitAreas = info->hitAreas;
        } else {
            LOG_WARN("ModelController: could not parse \"{}\" — empty details",
                     model3Json.toStdString());
        }
        m_models.append(std::move(entry));
    }

    ++m_revision;
    LOG_DEBUG("ModelController: rescan found {} model(s) under \"{}\"",
              m_models.size(), m_modelsDir.toStdString());
    emit modelsChanged();
}

QString ModelController::modelsDir() const
{
    return m_modelsDir;
}

void ModelController::openModelDir()
{
    // mkpath first: the dir often does not exist until the renderer ships its
    // Resources/ tree — opening a missing dir is a silent no-op on Linux and
    // an Explorer error on Windows. Guarded for the empty-rendererDir case.
    if (m_modelsDir.isEmpty())
        return;
    QDir().mkpath(m_modelsDir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_modelsDir));
}

QString ModelController::modelDirName(int index) const
{
    if (index < 0 || index >= m_models.size()) return {};
    return m_models.at(index).dirName;
}

int ModelController::modelMotionGroupCount(int index) const
{
    if (index < 0 || index >= m_models.size()) return 0;
    return m_models.at(index).motionGroups.size();
}

int ModelController::modelExpressionCount(int index) const
{
    if (index < 0 || index >= m_models.size()) return 0;
    return m_models.at(index).expressions.size();
}

int ModelController::modelHitAreaCount(int index) const
{
    if (index < 0 || index >= m_models.size()) return 0;
    return m_models.at(index).hitAreas.size();
}

QStringList ModelController::modelMotionGroups(const QString& name) const
{
    const Entry* entry = findByName(name);
    return entry != nullptr ? entry->motionGroups : QStringList{};
}

QStringList ModelController::modelExpressions(const QString& name) const
{
    const Entry* entry = findByName(name);
    return entry != nullptr ? entry->expressions : QStringList{};
}

QStringList ModelController::modelHitAreas(const QString& name) const
{
    const Entry* entry = findByName(name);
    return entry != nullptr ? entry->hitAreas : QStringList{};
}

const ModelController::Entry* ModelController::findByName(
    const QString& name) const
{
    for (const Entry& entry : m_models) {
        if (entry.dirName == name)
            return &entry;
    }
    return nullptr;
}
