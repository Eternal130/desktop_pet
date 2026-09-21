#include "ui/ModelController.hpp"

#include <QDesktopServices>
#include <QDir>
#include <QUrl>

#include <spdlog/spdlog.h>
#include "logging/Logging.hpp"
#include "core/ModelInfoParser.hpp"
#include "core/ModelScanner.hpp"
#include "core/PluginContextImpl.hpp" // complete core::ModelApiImpl

ModelController::ModelController(core::ModelApiImpl* api, QObject* parent)
    : QObject(parent), m_api(api)
{
    if (m_api == nullptr) {
        // Owned fallback (tests / standalone) — same object type the panel
        // shares in production, so both paths exercise identical code.
        m_api = new core::ModelApiImpl(this);
        m_apiOwned = true;
    }
    // Mirror VoicePackController: scan once at construction so a QML page
    // binding modelCount before setRendererDir sees a valid (if empty)
    // roster. scanAvailableModels on an empty dir is a cheap no-op.
    rescan();
}

ModelController::~ModelController()
{
    // The owned fallback is parented to this controller — Qt deletes it.
    // The shared production impl is NOT owned here (service tree).
}

void ModelController::setRendererDir(const QString& dir)
{
    m_rendererDir = dir;
    // ONE cache: the dir flows into the shared impl (its rescan feeds the
    // plugin pet.model family too); this controller then re-reads the
    // fresh scan below, exactly like a manual rescan.
    m_api->setRendererDir(dir);
    rescan();
}

void ModelController::rescan()
{
    // S5: the scan + pre-parse live in the impl (single cache); here we
    // only mirror the name roster + bump the revision the QML bindings
    // re-evaluate on.
    m_api->refreshScan();
    m_names.clear();
    const QVector<pet::ModelSummary> models = m_api->availableModels();
    for (const pet::ModelSummary& summary : models)
        m_names.append(summary.name);

    ++m_revision;
    LOG_DEBUG("ModelController: rescan found {} model(s) under \"{}\"",
              m_names.size(), modelsDir().toStdString());
    emit modelsChanged();
}

QString ModelController::modelsDir() const
{
    return m_api->modelsDir();
}

void ModelController::openModelDir()
{
    // mkpath first: the dir often does not exist until the renderer ships its
    // Resources/ tree — opening a missing dir is a silent no-op on Linux and
    // an Explorer error on Windows. Guarded for the empty-rendererDir case.
    if (modelsDir().isEmpty())
        return;
    QDir().mkpath(modelsDir());
    QDesktopServices::openUrl(QUrl::fromLocalFile(modelsDir()));
}

QString ModelController::modelDirName(int index) const
{
    if (index < 0 || index >= m_names.size()) return {};
    return m_names.at(index);
}

namespace {
// One model's cached summary via the impl (empty summary on a miss —
// every accessor below degrades to 0/{} and never throws).
pet::ModelSummary summaryFor(const core::ModelApiImpl* api, const QString& name)
{
    // modelInfo() is non-const only because it is an interface override;
    // the lookup is a pure cache read.
    pet::ModelSummary summary;
    const_cast<core::ModelApiImpl*>(api)->modelInfo(name, &summary);
    return summary;
}
} // namespace

int ModelController::modelMotionGroupCount(int index) const
{
    if (index < 0 || index >= m_names.size()) return 0;
    return summaryFor(m_api, m_names.at(index)).motionGroups.size();
}

int ModelController::modelExpressionCount(int index) const
{
    if (index < 0 || index >= m_names.size()) return 0;
    return summaryFor(m_api, m_names.at(index)).expressions.size();
}

int ModelController::modelHitAreaCount(int index) const
{
    if (index < 0 || index >= m_names.size()) return 0;
    return summaryFor(m_api, m_names.at(index)).hitAreas.size();
}

QStringList ModelController::modelMotionGroups(const QString& name) const
{
    return summaryFor(m_api, name).motionGroups;
}

QStringList ModelController::modelExpressions(const QString& name) const
{
    return summaryFor(m_api, name).expressions;
}

QStringList ModelController::modelHitAreas(const QString& name) const
{
    return summaryFor(m_api, name).hitAreas;
}
