#include <QApplication>
#include <QQmlApplicationEngine>
#include <QJsonDocument>
#include <QTimer>
#include <QGuiApplication>
#include <QCoreApplication>
#include <QFont>
#include <QFontDatabase>
#include <QStringList>

#include <cstdlib>
#include <chrono>

#include <spdlog/spdlog.h>
#include "logging/Logging.hpp"

#include "app/ScreenshotRunner.hpp"
#include "app/PanelApplication.hpp"
#include "app/PanelUiBoot.hpp"
#include "core/ConfigDir.hpp"
#include "core/InstanceManager.hpp"
#include "core/InstanceSession.hpp"
#include "core/StartupSalvo.hpp"
#include "core/ProcessManager.hpp"
#include "network/Envelope.hpp"
#include "network/Protocol.hpp"
#include "network/WsServer.hpp"

// ── CJK default-font guard (tofu fix) ─────────────────────────────────────
// Hosts without a system CJK font (fc-list :lang=zh empty — the dev machine
// that reported the bug) render every Chinese glyph as tofu; English text
// works because the Latin fallback resolves. Ship Noto Sans CJK SC next to
// the exe (<appDir>/fonts/, deployed by the controller's own POST_BUILD in
// CMakeLists.txt) and make it the application default with the previous
// default family kept as fallback (Latin glyphs still resolve there first
// if it ranked higher). Never fatal — any failure logs a WARN and keeps the
// system default (§9.5 never-crash contract). Called before engine.load so
// screenshot mode benefits identically.
static void installDefaultFontWithCjk()
{
    const QString fontPath = QCoreApplication::applicationDirPath()
        + QStringLiteral("/fonts/NotoSansCJKsc-Regular.otf");

    // Qt 6: QFontDatabase is a static-only API surface.
    const int fontId = QFontDatabase::addApplicationFont(fontPath);
    if (fontId < 0) {
        LOG_WARN("main: CJK font not loaded from '{}' — Chinese text may "
                 "render as tofu on hosts without a system CJK font",
                 fontPath.toStdString());
        return;
    }
    const QStringList families =
        QFontDatabase::applicationFontFamilies(fontId);
    if (families.isEmpty()) {
        LOG_WARN("main: CJK font '{}' loaded but exposed no family — "
                 "keeping system default", fontPath.toStdString());
        return;
    }

    // Copy the current default (keeps point size / style hints) and prepend
    // the CJK family: ["Noto Sans CJK SC", <original default>].
    QFont font = QGuiApplication::font();
    QStringList familiesChain{families.first()};
    const QString originalFamily = font.family();
    if (!originalFamily.isEmpty() && !familiesChain.contains(originalFamily))
        familiesChain.append(originalFamily);
    font.setFamilies(familiesChain);
    QGuiApplication::setFont(font);
    LOG_INFO("main: CJK default font installed ('{}' -> family '{}') — "
             "tofu guard active",
             fontPath.toStdString(), families.first().toStdString());
}

int main(int argc, char *argv[])
{
    // Use the Basic QuickControls style: the default native (Windows) style
    // forbids customizing Control background/contentItem, which (a) breaks
    // Theme-bound CheckBox/ComboBox colors and (b) crashes the renderer when
    // QtCharts' ChartView loads under a ComboBox with a custom background.
    // Basic imposes no palette of its own, so our Theme singleton owns all
    // visuals. Must be set before QGuiApplication construction.
    // Ref: https://doc.qt.io/qt-6/qtquickcontrols2-styles.html
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");

    // ── T24 cold-start timing anchor ─────────────────────────────────────
    // Captured BEFORE QGuiApplication construction so Qt framework init cost
    // is included in the cold-start delta. Exposed to QML as the
    // `coldStartT0Ms` context property so Main.qml's Component.onCompleted can
    // log the wall-clock-visible delta ("COLD_START_MS=<n>"). std::chrono is
    // used (not QDateTime) because it is provably safe pre-QCoreApplication.
    // The matching log line in Main.qml is the measurement point; this anchor
    // is only set once per process and never read again after Component.
    // onCompleted logs the delta.
    const auto t0SinceEpoch = std::chrono::system_clock::now().time_since_epoch();
    const qint64 coldStartT0Ms = static_cast<qint64>(
        std::chrono::duration_cast<std::chrono::milliseconds>(t0SinceEpoch).count());

    // QApplication (NOT QGuiApplication): Qt 6.10 QtCharts' ChartView depends
    // on QtWidgets initialization — under a bare QGuiApplication the ChartView
    // creation crashes with 0xC0000005 inside Qt6Widgets.dll (MonitorPage).
    // Verified by minimal repro: QGuiApplication+ChartView crashes,
    // QApplication+ChartView renders fine. QtWidgets is already linked for
    // QSystemTrayIcon (T13), so this costs nothing extra.
    QApplication app(argc, argv);

    // Boot logging BEFORE any LOG_* call. ConfigDir::ensureDirectories creates
    // ~/.config/desktop-pet/{,instances,logs}/ so Logging::init can attach its
    // rotating file sink. A failure to create the tree is non-fatal (Logging
    // degrades to console-only — see Logging.hpp). A failed init() is likewise
    // non-fatal: it already installed the console sink + Qt message bridge.
    ConfigDir::ensureDirectories();
    Logging::init(ConfigDir::logsDir());

    // CJK tofu guard: must run before the QML engine loads so every default-
    // fonted Text element (and screenshot mode) renders Chinese. See the
    // utility above; WARN-only on failure.
    installDefaultFontWithCjk();

    // ── Application service tree (P3/M2) ─────────────────────────────────
    // DatabaseManager / PanelStateManager / WsServer / PendingRequests /
    // InstanceManager and all their wiring now live in app/PanelApplication
    // — a QObject parent tree (heap + parent) that replaces the stack-reverse
    // discipline for these five; construction and connect order are the
    // original main() order. Declared here BEFORE every UI bridge and the
    // engine, so by stack-reverse the whole tree is destroyed after the QML
    // context (the cross-subtree invariant — the §B.2 v2 annotation in
    // PanelApplication.cpp covers what holds inside the tree). UI-bridge
    // construction + all context-property registration moved on to
    // PanelUiBoot (M3); the two aliases below serve main's own remaining
    // uses (salvo/PM wiring + the auto-start loop).
    PanelApplication panelApp;
    WsServer* const wsServer = panelApp.wsServer();
    InstanceManager* const instanceManager = panelApp.instanceManager();

    // StartupSalvo + ProcessManager: Phase-5 single-instance holders wired
    // here for sender injection; todo 2 (InstanceSession) owns them
    // per-instance. These globals are now redundant (InstanceSession owns
    // its own), but they are harmless and NetworkWiringTest asserts the
    // sender wiring, so they stay until a dedicated cleanup task. (M2 note:
    // they used to be constructed between PendingRequests and the roster;
    // with the service tree in one ctor they now construct after it — inert
    // reorder, nothing between the old points observes them and no renderer
    // connects until the event loop runs.)
    StartupSalvo startupSalvo;
    ProcessManager processManager;

    // Wire sender-injection seams → WsServer::sendText(instanceId, ...). These
    // legacy globals always targeted instance_id 0 (Phase 0-4 single-instance
    // assumption); InstanceSession owns its own per-instance senders below.
    startupSalvo.setCommandSender([wsServer](const QString& json) {
        wsServer->sendText(0, json);
    });
    processManager.setShutdownSender([wsServer]() {
        const QByteArray json =
            serialize(Protocol::buildShutdown()).toJson(QJsonDocument::Compact);
        wsServer->sendText(0, QString::fromUtf8(json));
    });

    QQmlApplicationEngine engine;

    // ── UI bridge boot (P3/M3) ────────────────────────────────────────
    // All 17 context properties + the UI-bridge constructions and wiring
    // now live in app/PanelUiBoot (names & registration order verbatim —
    // QML unchanged). The boot object is parented to the engine; the
    // destruction-order contract is documented at the mount point in
    // PanelUiBoot.cpp. main keeps only the bubble-stream seam that
    // ScreenshotRunner's --bubble mode needs (below).
    PanelUiBoot* const uiBoot =
        PanelUiBoot::registerAll(engine, panelApp, coldStartT0Ms);

    // T24: log the C++ side pre-load cost (everything between main() entry
    // and the QML load call). The QML side logs the residual (loadFromModule
    // cost + first-frame). Their sum is the wall-clock cold-start number.
    {
        const auto nowSinceEpoch = std::chrono::system_clock::now().time_since_epoch();
        const qint64 nowMs = static_cast<qint64>(
            std::chrono::duration_cast<std::chrono::milliseconds>(nowSinceEpoch).count());
        LOG_INFO("COLD_START_PRE_LOAD_MS={} t0={}", nowMs - coldStartT0Ms, coldStartT0Ms);
    }

    // Loads type "Main" from the QML module registered in CMakeLists.txt
    // (qt_add_qml_module, URI "DesktopPet").
    engine.loadFromModule("DesktopPet", "Main");

    // Auto-start (per-instance config.autoStart): launch every instance whose
    // flag is set once the QML UI is up — the renderer windows appear
    // alongside the panel. Queued via QTimer::singleShot(0) so the first
    // frame paints before the (blocking, process-spawning) start() calls run.
    QTimer::singleShot(0, [instanceManager]() {
        for (int i = 0; i < instanceManager->rowCount(); ++i) {
            InstanceSession* s = instanceManager->instanceAt(i);
            if (s != nullptr && s->autoStartEnabled()) {
                LOG_INFO("autoStart: launching instance \"{}\"",
                         s->label().toStdString());
                s->start();
            }
        }
    });

    // ── Visual-QA modes (--screenshot / --bubble) ─────────────────────────
    // Extracted verbatim to app/ScreenshotRunner (P3/M1). Sits exactly where
    // the inline block used to (after the auto-start singleShot registration,
    // before the normal run loop) so behavior is unchanged. Returns only when
    // no QA flag was passed; an active QA mode ends the process via
    // std::exit inside (teardown-history notes live in ScreenshotRunner.cpp).
    runScreenshotQa(app, engine, *uiBoot->notificationStream(), argc, argv);

    const int exitCode = app.exec();
    Logging::shutdown();
    return exitCode;
}
