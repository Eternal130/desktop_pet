#pragma once

#include <QObject>

class QQmlApplicationEngine;
class PanelApplication;
class NotificationStreamController;

// P4 plugin bridges live in namespace core (mixed-namespace codebase).
namespace core {
class PluginManager;
class PluginPageModel;
}

// PanelUiBoot — the UI-bridge subtree carved out of main() (P3/M3,
// docs/refactor §B.2 M3 row + F1 v2): constructs every UI bridge under this
// object and registers the QML context properties in main()'s original
// order, names verbatim. 17 through P3; P4 appends pluginManager +
// pluginPages (18th/19th — added at the END of the registration sequence,
// after the original 17, so every historical name keeps its slot).
//
// Ownership: each bridge is a heap child of this PanelUiBoot; the boot object
// itself is parented to the QQmlApplicationEngine by registerAll(). The
// destruction-order contract (mount-point note at the top of the ctor in the
// .cpp): bridges die during engine destruction, AFTER the engine's own QML
// internals (QObject children are deleted last in ~QObject) so each bridge
// still outlives the QML context it serves, and BEFORE the PanelApplication
// service tree (stack-reverse in main) — the same bridge-vs-service relative
// order as the old stack layout.
//
// No Q_OBJECT: pure construction + registration scope, no signals of its own.
class PanelUiBoot : public QObject
{
public:
    // Constructs the bridges, registers all 17 context properties, wires the
    // cross-boundary seams (asset-ref detacher, dialogue sink, logo sync,
    // WS listen + port probe) — statement-for-statement main()'s old order.
    // coldStartT0Ms must be the anchor captured BEFORE QGuiApplication in
    // main() (T24); registration is consolidated here, the timestamp's
    // capture point cannot move.
    // Returns the boot object (parented to `engine`); callers must not delete.
    static PanelUiBoot* registerAll(QQmlApplicationEngine& engine,
                                    PanelApplication& app,
                                    qint64 coldStartT0Ms);

    // Seam for main(): the bubble stream is the only bridge main itself still
    // needs — ScreenshotRunner's --bubble mode pushes test bubbles through it.
    NotificationStreamController* notificationStream();

private:
    PanelUiBoot(QQmlApplicationEngine& engine, PanelApplication& app,
                qint64 coldStartT0Ms, QObject* parent);

    QQmlApplicationEngine& m_engine;
    PanelApplication& m_app;

    // Construction/registration order = old main() order; within this tree
    // children die registration-reverse (Qt 6.6+ implementation behavior,
    // not a contract — §B.2 v2).
    class TrayManager* m_trayManager;
    class AutoLaunchManager* m_autoLaunchManager;
    class PanelConfigController* m_panelConfigController;
    class AssetManager* m_assetManager;
    class NotificationStreamController* m_notificationStream;
    class EnvironmentChecker* m_envChecker;
    class WindowStateSaver* m_windowStateSaver;
    class VoicePackController* m_voicePackController;
    class ModelController* m_modelController;
    // P4 plugin bridges (constructed after the original bridges, before
    // the engine loads — see the .cpp for the mounting contract).
    core::PluginManager* m_pluginManager = nullptr;
    core::PluginPageModel* m_pluginPageModel = nullptr;
};
