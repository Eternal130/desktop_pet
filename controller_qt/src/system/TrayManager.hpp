#pragma once

#include <QObject>
#include <QSystemTrayIcon>

namespace pet { class IEventApi; }

// TrayManager (Wave 7 todo 13) — QSystemTrayIcon wrapper.
//
// Blueprint §4.1.6 spec:
//   - Tray icon (double-click toggles window visibility).
//   - Right-click → 3-item menu: 显示/隐藏, 设置, 退出.
//   - setQuitOnLastWindowClosed(false) so hiding to tray does not quit.
//
// ── M3 resolution — AVOID QtWidgets (in spirit) ─────────────────────────
// QSystemTrayIcon physically lives in QtWidgets on Qt 6.10 (qsystemtrayicon.h
// includes qtwidgetsglobal.h) — see T13 finding in
// .omo/notepads/controller-qt-phase5-9/learnings.md. CMakeLists.txt links
// Qt6::Widgets purely for the header + symbol dependency; the M3 spirit is
// still respected:
//   - We use QGuiApplication (NOT QApplication).
//   - We instantiate NO QWidget subclasses.
//   - We use NO QMenu / setContextMenu — the QML Menu (Main.qml) is the popup.
// QSystemTrayIcon itself inherits QObject + uses QIcon (QtGui) — it does NOT
// require a QApplication instance or any QWidget to function. The activation
// routing:
//   - QSystemTrayIcon::activated(Context)        → requestContextMenu()
//   - QSystemTrayIcon::activated(DoubleClick)    → visibilityToggled()
//   - QML QtQuick.Controls Menu (Main.qml)       → trayMenu.popup()
// The QML Menu items call the Q_INVOKABLE activators below so menu clicks
// emit the same signals tray-icon gestures do — uniform + testable via
// QSignalSpy without a real tray.
//
// ── m6 fix — 3 items, NOT 4 ─────────────────────────────────────────────
// Show/Hide is ONE toggle entry (activateToggleVisibility), not separate
// Show + Hide. Blueprint §4.1.6 specifies exactly 3 menu items.
//
// ── R5 risk — Linux GNOME ───────────────────────────────────────────────
// On GNOME (default Ubuntu desktop) the shell ships NO legacy SNI tray
// support — QSystemTrayIcon::isSystemTrayAvailable() returns false unless
// the user installs the AppIndicator / KStatusNotifierItem extension. This
// is a known platform limitation, NOT a bug in our code. When false, the
// constructor LOG_WARNs and early-returns; the app keeps running (close
// button still works via closeAction todo 15). The SettingsPage should
// surface a hint to GNOME users if tray features are accessed.
class TrayManager : public QObject
{
    Q_OBJECT

public:
    // Constructs the tray icon if the platform supports one; otherwise logs
    // WARN and leaves the object inert (isAvailable() returns false). Calls
    // QGuiApplication::setQuitOnLastWindowClosed(false) unconditionally —
    // the blueprint §4.1.6 close-to-tray behavior requires the panel to
    // survive its window being hidden, regardless of whether the tray is
    // actually visible. parent=nullptr in production (main.cpp owns it on
    // the stack); QSystemTrayIcon is parented to this.
    explicit TrayManager(QObject* parent = nullptr);

    // Hides the tray icon (if any). QSystemTrayIcon is parented to this, so
    // Qt would tear it down anyway, but explicit hide() avoids a visible
    // "ghost icon" lingering during shutdown.
    ~TrayManager() override;

    // True iff a system tray is available AND the tray icon is initialized.
    // False in headless / GNOME-without-extension / CI environments.
    // Exposed QML-side so SettingsPage can show "tray unavailable" hints.
    Q_INVOKABLE bool isAvailable() const { return m_available; }

    // Wave 7 todo 15 — explicit early-hide of the tray icon, called from
    // Main.qml's exit path BEFORE Qt.quit() so the icon vanishes immediately
    // (not lingering during the ~N×5s stopAll window). The destructor also
    // calls hide(), but that runs AFTER app.exec() returns — too late for a
    // clean visual transition. No-op when m_tray is null (no tray available).
    Q_INVOKABLE void shutdown() { if (m_tray) m_tray->hide(); }

    // ── Menu-action entry points (QML Menu items call these) ────────────
    // Each emits its corresponding signal so the QML Connections block (and
    // tests via QSignalSpy) observes menu clicks uniformly — the same
    // signals fire from the C++ activated() handler (DoubleClick →
    // visibilityToggled) and from the QML Menu items. Trivial inline impls
    // so they are visible to moc + inlinable.
    // v1.9: each ALSO publishes pet.panel.tray_activated AFTER the signal
    // (post-commit; null publisher = pre-v1.9 behavior, publishTrayActivated
    // guards).

    // "显示/隐藏" menu item — toggles window visibility (Main.qml hides /
    // shows + raises the window).
    Q_INVOKABLE void activateToggleVisibility()
    {
        emit visibilityToggled();
        publishTrayActivated(QStringLiteral("toggle_visibility"));
    }

    // "设置" menu item — Main.qml navigates to SettingsPage + shows window.
    Q_INVOKABLE void activateSettings()
    {
        emit showSettings();
        publishTrayActivated(QStringLiteral("settings"));
    }

    // "退出" menu item — Main.qml calls Qt.quit() (graceful shutdown, which
    // flushes logs via Logging::shutdown in main.cpp's post-exec() path).
    Q_INVOKABLE void activateQuit()
    {
        emit quitRequested();
        publishTrayActivated(QStringLiteral("quit"));
    }

    // Logo sync (AssetManager) — replace the tray icon with the PNG at
    // `path`. No-op (WARN) when no tray is available or the file is not a
    // loadable image. Never throws.
    Q_INVOKABLE void setIconPixmap(const QString& path);

    // v1.9 event-publisher seam (the SettingsApiImpl::setEventPublisher
    // precedent, wired by PanelUiBoot onto the shared event API): every
    // interaction signal below additionally publishes
    // pet.panel.tray_activated {action} right AFTER the Qt signal fires
    // (post-commit). Interface pointer (NOT the concrete impl) so tests
    // inject fakes; plain C++ setter, deliberately NOT a Q_INVOKABLE /
    // Q_PROPERTY WRITE (the api-boundary gate's pattern table stays
    // untouched). Null (the default) = publish nothing (pre-v1.9
    // behavior). Action mapping (signal → payload):
    //   visibilityToggled  → "toggle_visibility"
    //   showSettings       → "settings"
    //   quitRequested      → "quit"
    //   requestContextMenu → "context_menu"
    void setEventPublisher(pet::IEventApi* publisher)
    { m_eventPublisher = publisher; }

signals:
    // Right-click on the tray icon (ActivationReason::Context). Main.qml's
    // Connections handler calls trayMenu.popup() — opens the QML Menu.
    void requestContextMenu();

    // Double-click on the tray icon OR "显示/隐藏" menu item. Main.qml
    // toggles ApplicationWindow.visible.
    void visibilityToggled();

    // "设置" menu item. Main.qml switches to the SettingsPage and shows the
    // window (in case it was hidden).
    void showSettings();

    // "退出" menu item. Main.qml calls Qt.quit().
    void quitRequested();

public slots:
    // Routes QSystemTrayIcon::activated(reason) to one of our signals.
    //   Context      → requestContextMenu
    //   DoubleClick  → visibilityToggled
    //   Trigger / MiddleClick / Unknown → ignored
    //
    // Public slot (not private) so TrayManagerTest can invoke it directly
    // with each ActivationReason value — exercises the exact production
    // routing without depending on a real mouse interaction with the tray
    // icon. Qt slots are meta-object-invokable anyway, so making them public
    // is the common Qt idiom (no real encapsulation cost).
    void onActivated(QSystemTrayIcon::ActivationReason reason);

private:
    // v1.9: publish pet.panel.tray_activated {action} as a PANEL-LEVEL
    // host event (instanceUuid ""). No-op without an injected publisher.
    // Payload via the EventCatalog builder (keys never hand-written).
    void publishTrayActivated(const QString& action);

    // Loads the tray icon: tries `:/icons/tray-icon.png` first, falls back
    // to a generated 32×32 blue-circle pixmap. The fallback matches the
    // Java reference's createTrayImage() default (RGB 74,144,217). Always
    // returns a non-null QIcon.
    QIcon loadIcon();

    QSystemTrayIcon* m_tray = nullptr;
    bool m_available = false;
    pet::IEventApi* m_eventPublisher = nullptr; // v1.9, not owned
};
