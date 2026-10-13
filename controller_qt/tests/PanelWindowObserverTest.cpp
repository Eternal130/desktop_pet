// PanelWindowObserverTest (v1.9) — the pet.panel.window_state emission
// point over a REAL QWindow driven through the offscreen QPA platform.
//
// Slots:
//   - testStateForVisibilityMapping: the QWindow::Visibility → state
//     string projection for EVERY enum value (the api/EventIds.hpp
//     field table — including the two defensive fallbacks for values
//     this UI can never produce).
//   - testWindowStateEmission: an observed QWindow's show()/hide()
//     transitions publish panel-level events {state, visible} with the
//     host header; the hidden transition carries the hide-to-tray
//     semantics (Main.qml's closeAction "minimize" branch and
//     toggleVisibility() both land on root.hide() → Hidden).
//   - testDegradedWiring: null publisher + observe (pre-v1.9 behavior)
//     and null detach never crash; a re-observe switches windows
//     (idempotent disconnect).
//
// Custom main() (the TrayManagerTest precedent): QWindow::show() needs
// QGuiApplication, and QT_QPA_PLATFORM=offscreen is forced BEFORE the
// app object so the window transitions are deterministic on headless
// CI (no real window is ever mapped to a display).
// Links pet_panel_core.

#include "core/PanelWindowObserver.hpp"

#include "api/EventIds.hpp"
#include "api/IEventApi.hpp"

#include <QGuiApplication>
#include <QObject>
#include <QString>
#include <QTest>
#include <QWindow>

namespace {

// Recording publisher (the EventBusEmissionTest fake shape): captures
// every publish; the window emission site is publish-only so
// subscribe/unsubscribe stay unused.
class RecordingPublisher : public pet::IEventApi
{
public:
    QList<pet::Event> events;

    pet::PluginError subscribe(const QString&, pet::IEventListener*,
                               pet::EventPriority) override
    { return pet::PluginError::Ok; }
    void unsubscribe(const QString&, pet::IEventListener*) override {}
    pet::PluginError publish(pet::Event& event) override
    {
        events.append(event);
        return pet::PluginError::Ok;
    }
};

} // namespace

class PanelWindowObserverTest : public QObject
{
    Q_OBJECT

private slots:
    void testStateForVisibilityMapping();
    void testWindowStateEmission();
    void testDegradedWiring();
};

// ===========================================================================
// The visibility → state projection (the whole mapping table).
// ===========================================================================

void PanelWindowObserverTest::testStateForVisibilityMapping()
{
    using core::PanelWindowObserver;
    QCOMPARE(PanelWindowObserver::stateForVisibility(QWindow::Windowed),
             QStringLiteral("normal"));
    QCOMPARE(PanelWindowObserver::stateForVisibility(QWindow::Maximized),
             QStringLiteral("maximized"));
    QCOMPARE(PanelWindowObserver::stateForVisibility(QWindow::Minimized),
             QStringLiteral("minimized"));
    QCOMPARE(PanelWindowObserver::stateForVisibility(QWindow::Hidden),
             QStringLiteral("hidden"));
    // Defensive fallbacks — enum values this UI can never produce (no
    // fullscreen path, no embedded AutomaticVisibility); the mapping
    // must still return a documented state, never crash.
    QCOMPARE(PanelWindowObserver::stateForVisibility(QWindow::FullScreen),
             QStringLiteral("maximized"));
    QCOMPARE(PanelWindowObserver::stateForVisibility(
                  QWindow::AutomaticVisibility),
             QStringLiteral("normal"));
}

// ===========================================================================
// show()/hide() transitions → panel-level pet.panel.window_state.
// ===========================================================================

void PanelWindowObserverTest::testWindowStateEmission()
{
    RecordingPublisher publisher;
    core::PanelWindowObserver observer;
    observer.setEventPublisher(&publisher);

    QWindow window;
    window.setTitle(QStringLiteral("PanelWindowObserverTest"));
    observer.observe(&window);

    const QString stateId = pet::kEventPanelWindowState.toString();

    // show() → Windowed → {state:"normal", visible:true}. (QTRY: the
    // property change is delivered synchronously today, but the QPA
    // platform window creation is allowed to settle first.)
    window.show();
    QTRY_COMPARE_WITH_TIMEOUT(publisher.events.size(), 1, 2000);
    {
        const pet::Event& e = publisher.events.at(0);
        QCOMPARE(e.eventId, stateId);
        QCOMPARE(e.payload.value(QStringLiteral("state")).toString(),
                 QStringLiteral("normal"));
        QCOMPARE(e.payload.value(QStringLiteral("visible")).toBool(), true);
        QCOMPARE(e.instanceUuid, QString()); // PANEL-LEVEL
        QCOMPARE(e.publisherId, QString(pet::kEventPublisherHost));
        QVERIFY(!e.cancellable);
        QCOMPARE(e.payload.size(), 2); // exactly the two catalog keys
    }

    // hide() → Hidden → {state:"hidden", visible:false} — the
    // hide-to-tray semantics (closeAction "minimize" / tray toggle).
    window.hide();
    QTRY_COMPARE_WITH_TIMEOUT(publisher.events.size(), 2, 2000);
    {
        const pet::Event& e = publisher.events.at(1);
        QCOMPARE(e.eventId, stateId);
        QCOMPARE(e.payload.value(QStringLiteral("state")).toString(),
                 QStringLiteral("hidden"));
        QCOMPARE(e.payload.value(QStringLiteral("visible")).toBool(), false);
        QCOMPARE(e.instanceUuid, QString());
        QVERIFY(!e.cancellable);
    }
}

// ===========================================================================
// Degraded wiring — null publisher, null detach, re-observe switching.
// ===========================================================================

void PanelWindowObserverTest::testDegradedWiring()
{
    // Null publisher (pre-v1.9 behavior): transitions observed, nothing
    // published, never a crash.
    core::PanelWindowObserver silent;
    QWindow window;
    silent.observe(&window);
    window.show();
    window.hide();

    // Detach (null) + re-observe another window: idempotent disconnect,
    // the new window's transitions publish, the old one's do not.
    RecordingPublisher publisher;
    core::PanelWindowObserver observer;
    observer.observe(&window); // null publisher at attach time — fine
    observer.setEventPublisher(&publisher);
    observer.observe(nullptr);
    observer.observe(&window);
    window.show(); // re-attached → publishes again
    QTRY_COMPARE_WITH_TIMEOUT(publisher.events.size(), 1, 2000);

    QWindow other;
    observer.observe(&other);
    window.hide(); // detached — must NOT publish
    other.show();  // the new observation point
    QTRY_COMPARE_WITH_TIMEOUT(publisher.events.size(), 2, 2000);
    QCOMPARE(publisher.events.at(1).payload
                 .value(QStringLiteral("state")).toString(),
             QStringLiteral("normal"));
    // Give the detached hide a beat to prove it stays quiet.
    QTest::qWait(50);
    QCOMPARE(publisher.events.size(), 2);
}

// Custom main — QWindow::show() needs QGuiApplication; the offscreen QPA
// platform is forced BEFORE the app object so the transitions are
// deterministic and nothing maps a real window on a display machine.
int main(int argc, char* argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    PanelWindowObserverTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "PanelWindowObserverTest.moc"
