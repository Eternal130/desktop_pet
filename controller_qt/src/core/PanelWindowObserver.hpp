#pragma once

// PanelWindowObserver (v1.9) — the pet.panel.window_state emission point.
//
// Observes the QQmlApplicationEngine root window (an ApplicationWindow —
// a QQuickWindow, taken as its QWindow base: visibilityChanged /
// isVisible are QWindow APIs) and publishes the panel-level host event
// pet.panel.window_state {state, visible} on every visibility
// transition. PanelUiBoot constructs one, injects the shared
// pet::IEventApi, and attaches it in the engine's objectCreated handler
// (the root window does not exist at PanelUiBoot construction time —
// loadFromModule runs later in main()).
//
// Why a standalone class (not inline PanelUiBoot wiring): PanelUiBoot
// lives in the exe target (outside pet_panel_core), so its code is not
// unit-testable; this observer is a pet_panel_core member and the
// emission contract (mapping + payload + panel-level header) is
// exercised directly by PanelWindowObserverTest with an offscreen
// QQuickWindow.
//
// visibility → state mapping (the docs/bus-events.md §6.24 table):
//   Windowed            → "normal"
//   Maximized           → "maximized"
//   Minimized           → "minimized"
//   Hidden              → "hidden"   (hide-to-tray semantics — Main.qml's
//                                     closeAction "minimize" branch and
//                                     toggleVisibility() both call
//                                     root.hide(), which lands here)
//   FullScreen          → "maximized"  (defensive — this UI has no
//                     fullscreen path; nearest bucket, documented)
//   AutomaticVisibility → "normal"     (defensive — embedded-only value)
//
// Post-commit single point: QWindow::visibilityChanged is an
// after-the-change notification, so the publish always observes the
// settled state; `visible` is window->isVisible() sampled at signal
// time.
//
// Seam discipline: setEventPublisher is a plain C++ setter (NOT
// Q_INVOKABLE / Q_PROPERTY WRITE — the api-boundary gate's pattern
// table stays untouched; the InstanceSession::setEventPublisher /
// SettingsApiImpl::setEventPublisher precedent). Null publisher =
// observe without publishing (pre-v1.9 behavior). Never throws; all
// work happens on the GUI thread (QWindow signals).

#include <QObject>
#include <QString>
#include <QWindow>

namespace pet { class IEventApi; }

namespace core {

class PanelWindowObserver : public QObject
{
    Q_OBJECT

public:
    explicit PanelWindowObserver(QObject* parent = nullptr);

    // v1.9 seam (see the class comment). Plain C++ setter, not owned.
    void setEventPublisher(pet::IEventApi* publisher)
    { m_eventPublisher = publisher; }

    // Start observing `window` (typ. the engine root, as its QWindow
    // base). Connects QWindow::visibilityChanged; idempotent —
    // re-observing another window first disconnects the previous one
    // (this object never owns the window). Null detaches. Safe on a
    // not-yet-shown window: the initial show() fires
    // visibilityChanged(Windowed) and publishes normally.
    void observe(QWindow* window);

    // QWindow::Visibility → state-string projection (the mapping table
    // in the class comment; public static so the test + docs assert ONE
    // truth).
    static QString stateForVisibility(QWindow::Visibility visibility);

private slots:
    void onVisibilityChanged(QWindow::Visibility visibility);

private:
    pet::IEventApi* m_eventPublisher = nullptr; // v1.9, not owned
    QWindow* m_window = nullptr;                // observed, not owned
};

} // namespace core
