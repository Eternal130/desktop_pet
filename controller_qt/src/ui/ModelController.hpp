#pragma once

// ModelController — QML bridge for the model-library page (模型库 B 档).
// Read-only discovery surface over the existing ModelScanner + ModelInfoParser
// core utilities (deliberately NO new scanning/parsing code — those already
// have their own tests):
//
//   Q_PROPERTY modelCount                  — number of discovered models
//   Q_PROPERTY revision                    — bumped on every rescan; QML
//                                             bindings that call the
//                                             Q_INVOKABLE accessors reference
//                                             it in the same binding
//                                             expression to re-evaluate on
//                                             modelsChanged (invokables are
//                                             not bindable properties — same
//                                             trick as VoicePackController::
//                                             mountsRevision)
//   Q_INVOKABLE rescan()                   — re-scan + pre-parse ALL models
//   Q_INVOKABLE modelsDir()                — <rendererDir>/Resources/Models
//   Q_INVOKABLE openModelDir()             — create-if-missing + open in the
//                                             OS file manager (QUrl::
//                                             fromLocalFile, NOT string-
//                                             concatenated file:/// URLs
//                                             which break on leading-slash
//                                             paths)
//   Q_INVOKABLE modelDirName(i)            — directory name == model id
//                                             ("Hiyori" — the value the
//                                             renderer's --model accepts)
//   Q_INVOKABLE modelMotionGroupCount(i)   — motion groups in the model
//   Q_INVOKABLE modelExpressionCount(i)    — expressions in the model
//   Q_INVOKABLE modelHitAreaCount(i)       — hit areas in the model
//   Q_INVOKABLE modelMotionGroups(name)    — motion-group names (detail card)
//   Q_INVOKABLE modelExpressions(name)     — expression names (detail card)
//   Q_INVOKABLE modelHitAreas(name)        — hit-area names (detail card)
//   setRendererDir(dir)                    — injection seam for PanelUiBoot
//                                             (triggers a rescan)
//
// S5 (v1.3) internal migration: the scan + metadata CACHE now lives in
// the shared core::ModelApiImpl (the same object behind the plugin
// queryApi("pet.model") family). This controller keeps its QML surface
// byte-identical but delegates storage/lookups to the injected impl —
// the panel page and the plugin API consume ONE scan cache, not two.
// Constructor injection with an owned fallback: api == nullptr (tests,
// standalone use) constructs a private ModelApiImpl, so every historical
// call site keeps compiling and behaving identically.
//
// Contract: NEVER throws (blueprint §9.5). Out-of-range index / unknown name
// → empty string / empty list / 0. An empty rendererDir → empty roster with
// every accessor still safe to call.

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include "api/PluginTypes.hpp"

namespace core {
class ModelApiImpl;
}

class ModelController : public QObject {
    Q_OBJECT
    Q_PROPERTY(int modelCount READ modelCount NOTIFY modelsChanged)
    // Revision counter bumped on every rescan — see the header comment for
    // why QML bindings must reference it next to Q_INVOKABLE calls.
    Q_PROPERTY(int revision READ revision NOTIFY modelsChanged)

public:
    // api: the SHARED core::ModelApiImpl from the PanelApplication service
    // tree (S5 dogfooding — the plugin pet.model family reads the same
    // cache). Null → a private impl is constructed (owned, parented here)
    // so standalone/test construction behaves exactly as before.
    explicit ModelController(core::ModelApiImpl* api = nullptr,
                             QObject* parent = nullptr);
    ~ModelController() override;

    int modelCount() const { return static_cast<int>(m_names.size()); }
    int revision() const { return m_revision; }

    Q_INVOKABLE void rescan();
    // The scanned directory (<rendererDir>/Resources/Models). Empty when no
    // renderer dir is known. Used by the page header hint + empty state.
    Q_INVOKABLE QString modelsDir() const;
    // Creates the dir if missing (fresh installs have no Resources/Models
    // until the renderer ships) and opens it via QDesktopServices. Mirrors
    // VoicePackController::openVoicePackDir.
    Q_INVOKABLE void openModelDir();

    Q_INVOKABLE QString modelDirName(int index) const;
    Q_INVOKABLE int modelMotionGroupCount(int index) const;
    Q_INVOKABLE int modelExpressionCount(int index) const;
    Q_INVOKABLE int modelHitAreaCount(int index) const;

    Q_INVOKABLE QStringList modelMotionGroups(const QString& name) const;
    Q_INVOKABLE QStringList modelExpressions(const QString& name) const;
    Q_INVOKABLE QStringList modelHitAreas(const QString& name) const;

    // Injection seam: PanelUiBoot passes the same renderer base dir the
    // VoicePackController derives internally (applicationDirPath — the build
    // places both exes side-by-side in build/bin). Forwards to the shared
    // impl (one cache) and triggers this controller's rescan so the roster
    // is populated before the page binds.
    void setRendererDir(const QString& dir);

signals:
    void modelsChanged();

private:
    // Names in scan order (the summary bodies live in the impl's cache;
    // lookups below are modelInfo() calls, not local copies).
    QStringList m_names;
    QString m_rendererDir;
    int m_revision = 0;

    core::ModelApiImpl* m_api; // shared (injected) or owned fallback
    bool m_apiOwned = false;
};
