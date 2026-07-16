#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

// Environment readiness checker (task T27 / Phase 4.5 welcome page).
//
// Exposes six readiness probes to QML so the WelcomePage can render the
// "● Ready / ● Not found" detection panel. Probes are run on demand via
// runChecks() (called from WelcomePage's Component.onCompleted) and the
// results are cached in members until the next runChecks() call. Every
// getter is NOTIFY-bound to checksChanged so QML bindings refresh atomically
// after a re-run.
//
// Probes (architecture-blueprint.md §4.5 readiness checklist):
//   (1) openglRendererReady — desktop-pet-renderer[.exe] exists
//   (2) vulkanRendererReady — desktop-pet-renderer-vulkan[.exe] exists
//   (3) qtRuntimeReady      — always true (if we're running, Qt works)
//   (4) resourcesReady      — <rendererDir>/Resources/Models/ directory exists
//   (5) modelsAvailable     — ≥1 subdir of Models/ containing a *.model3.json
//   (6) portBindable        — a QTcpServer can listen on 127.0.0.1:9001
//
// Registration: set as a QML context property "envChecker" in main.cpp so it
// is globally available to every page without per-page instantiation. The
// class carries only Q_OBJECT (for signals/Q_INVOKABLE); QML_ELEMENT type
// registration is intentionally omitted — the context-property approach
// needs no QML type registration, and mixing both caused the auto-generated
// qmltyperegistrations to fail resolving the full type.
class EnvironmentChecker : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool openglRendererReady READ openglRendererReady NOTIFY checksChanged)
    Q_PROPERTY(bool vulkanRendererReady READ vulkanRendererReady NOTIFY checksChanged)
    Q_PROPERTY(bool qtRuntimeReady READ qtRuntimeReady CONSTANT)
    Q_PROPERTY(bool resourcesReady READ resourcesReady NOTIFY checksChanged)
    Q_PROPERTY(bool modelsAvailable READ modelsAvailable NOTIFY checksChanged)
    Q_PROPERTY(bool portBindable READ portBindable NOTIFY checksChanged)
    Q_PROPERTY(QStringList availableModels READ availableModels NOTIFY checksChanged)
    Q_PROPERTY(bool allReady READ allReady NOTIFY checksChanged)

    // Detail strings so the panel can show "Found: <path>" / "Not found".
    Q_PROPERTY(QString rendererDirectory READ rendererDirectory NOTIFY checksChanged)
    Q_PROPERTY(QString openglRendererPath READ openglRendererPath NOTIFY checksChanged)
    Q_PROPERTY(QString vulkanRendererPath READ vulkanRendererPath NOTIFY checksChanged)
    Q_PROPERTY(QString modelsDirectory READ modelsDirectory NOTIFY checksChanged)
    Q_PROPERTY(int port READ port CONSTANT)

public:
    explicit EnvironmentChecker(QObject* parent = nullptr);

    // Re-run all six probes and emit checksChanged. Idempotent; safe to call
    // repeatedly (e.g. a "Re-check" button later). Must be called at least once
    // before any getter returns a meaningful value.
    Q_INVOKABLE void runChecks();

    bool openglRendererReady() const;
    bool vulkanRendererReady() const;
    bool qtRuntimeReady() const { return true; }  // we're running → Qt is OK
    bool resourcesReady() const;
    bool modelsAvailable() const;
    bool portBindable() const;
    QStringList availableModels() const;
    bool allReady() const;

    QString rendererDirectory() const;
    QString openglRendererPath() const;
    QString vulkanRendererPath() const;
    QString modelsDirectory() const;
    int port() const { return kPort; }

signals:
    // Emitted after runChecks() refreshes every cached probe result.
    void checksChanged();

private:
    // Performs the six probes and caches results in the m_* members. Does NOT
    // emit checksChanged — the public runChecks() does that, so callers can
    // chain other work before notifying.
    void runAllChecks();

    // The directory the renderer exes + Resources/ live in. Uses
    // QCoreApplication::applicationDirPath() (same dir as the controller exe —
    // build.py places them side-by-side in build/bin). This deliberately does
    // NOT use core::defaultRendererDir(): that helper appends "/../build/bin"
    // which resolves to build/build/bin (nonexistent) for the side-by-side
    // dev layout. poc_main.cpp:277 established applicationDirPath() as the
    // correct anchor; EnvironmentChecker follows the proven pattern.
    static QString resolveRendererDir();

    // Cached results — populated by runAllChecks(). Defaults are the
    // pre-first-run state (everything false / empty).
    QString m_rendererDir;
    QString m_openglRendererPath;   // absolute path if found, else empty
    QString m_vulkanRendererPath;
    QString m_modelsDir;
    bool m_openglRendererReady = false;
    bool m_vulkanRendererReady = false;
    bool m_resourcesReady = false;
    bool m_modelsAvailable = false;
    bool m_portBindable = false;
    QStringList m_availableModels;

    // Default WebSocket port (docs/protocol/handshake.md §1, AGENTS.md:
    // Controller = WS Server on port 9001).
    static constexpr int kPort = 9001;
};
