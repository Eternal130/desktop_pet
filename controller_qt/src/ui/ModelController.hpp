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
// rescan() pre-parses every discovered <Name>/<Name>.model3.json via
// ModelInfoParser and caches the result; the accessors are pure cache lookups
// so the QML detail card never does file I/O per frame.
//
// Contract: NEVER throws (blueprint §9.5). Out-of-range index / unknown name
// → empty string / empty list / 0. An empty rendererDir → empty roster with
// every accessor still safe to call.

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class ModelController : public QObject {
    Q_OBJECT
    Q_PROPERTY(int modelCount READ modelCount NOTIFY modelsChanged)
    // Revision counter bumped on every rescan — see the header comment for
    // why QML bindings must reference it next to Q_INVOKABLE calls.
    Q_PROPERTY(int revision READ revision NOTIFY modelsChanged)

public:
    explicit ModelController(QObject* parent = nullptr);

    int modelCount() const { return static_cast<int>(m_models.size()); }
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
    // places both exes side-by-side in build/bin). Triggers a rescan so the
    // roster is populated before the page binds.
    void setRendererDir(const QString& dir);

signals:
    void modelsChanged();

private:
    // Pre-parsed cache entry — one per discovered model directory.
    struct Entry {
        QString dirName;
        QStringList motionGroups; // group names (QMap key order = sorted)
        QStringList expressions;
        QStringList hitAreas;
    };

    // Linear scan by model name; the roster is sidebar-sized. Nullptr when
    // the name is unknown (accessors then return empty/0).
    const Entry* findByName(const QString& name) const;

    QList<Entry> m_models;
    QString m_rendererDir;
    QString m_modelsDir;
    int m_revision = 0;
};
