// TrayManagerTest — Wave 7 todo 13.
//
// Locks the QSystemTrayIcon wrapper's signal routing + the Q_INVOKABLE
// menu-action activators. The QML Menu items (in production) and this test
// (in the test harness) call the same activate*() methods — uniform coverage.
//
// 5 slots:
//   - testQuitOnLastWindowClosedDisabled: constructor side-effect —
//       QGuiApplication::quitOnLastWindowClosed() is false after construct,
//       even when no system tray is available.
//   - testMenuActivators: activateToggleVisibility / activateSettings /
//       activateQuit each emit their corresponding signal exactly once
//       (QSignalSpy count + signal-name match). The tray's actual double-
//       click path is exercised by the next slot.
//   - testActivatedDoubleClickEmitsVisibilityToggled: directly emit the
//       QSystemTrayIcon::activated signal with DoubleClick → assert
//       visibilityToggled fires. Mirrors the production routing in
//       onActivated() without depending on a real mouse interaction.
//   - testActivatedContextEmitsRequestContextMenu: same, Context reason →
//       requestContextMenu.
//   - testActivatedTriggerIgnored: Trigger / MiddleClick / Unknown do NOT
//       emit any signal (parity with Java reference, which only handles
//       double-click).
//
// testNoCrashWhenNoTray is implicit — every slot constructs a TrayManager,
// and on CI/headless/GNOME the constructor's isSystemTrayAvailable() check
// early-returns. The Q_INVOKABLE activators still work (they're pure
// signal-emitters independent of m_tray), so the menu logic is fully testable
// without a real tray.
//
// Custom main() instantiates QGuiApplication (NOT QCoreApplication via
// QTEST_MAIN) because QSystemTrayIcon + setQuitOnLastWindowClosed require
// it. The test still runs under ctest on Windows / X11 / macOS; on truly
// headless CI the tray path is skipped (isAvailable() returns false) but
// the signal-routing slots still pass.

#include "system/TrayManager.hpp"

#include <QGuiApplication>
#include <QObject>
#include <QSignalSpy>
#include <QSystemTrayIcon>
#include <QTest>

class TrayManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void testQuitOnLastWindowClosedDisabled();
    void testMenuActivators();
    void testActivatedDoubleClickEmitsVisibilityToggled();
    void testActivatedContextEmitsRequestContextMenu();
    void testActivatedTriggerIgnored();
};

// ===========================================================================
// Constructor side-effect — quitOnLastWindowClosed == false.
// ===========================================================================

void TrayManagerTest::testQuitOnLastWindowClosedDisabled()
{
    // Given: a fresh TrayManager. Blueprint §4.1.6 mandates this be false so
    // the close-to-tray flow (todo 15) can hide the window without quitting.
    TrayManager tm;

    // Then: the QGuiApplication flag is false regardless of tray availability.
    QVERIFY2(!QGuiApplication::quitOnLastWindowClosed(),
             "TrayManager ctor must disable QGuiApplication::"
             "quitOnLastWindowClosed so hiding the window to tray does not "
             "quit the app");
}

// ===========================================================================
// Q_INVOKABLE menu activators — each emits its signal exactly once.
// ===========================================================================

void TrayManagerTest::testMenuActivators()
{
    TrayManager tm;
    QSignalSpy toggleSpy(&tm, &TrayManager::visibilityToggled);
    QSignalSpy settingsSpy(&tm, &TrayManager::showSettings);
    QSignalSpy quitSpy(&tm, &TrayManager::quitRequested);

    // When: each menu activator is invoked (mirrors Main.qml's Action
    // onTriggered handlers).
    tm.activateToggleVisibility();
    tm.activateSettings();
    tm.activateQuit();

    // Then: each signal fired exactly once. The activators are pure emit()
    // wrappers — no async, no event loop pumping needed.
    QCOMPARE_EQ(toggleSpy.count(), 1);
    QCOMPARE_EQ(settingsSpy.count(), 1);
    QCOMPARE_EQ(quitSpy.count(), 1);
}

// ===========================================================================
// activated(DoubleClick) → visibilityToggled.
// ===========================================================================

void TrayManagerTest::testActivatedDoubleClickEmitsVisibilityToggled()
{
    TrayManager tm;

    // If no tray is available on this platform, m_tray is null — we cannot
    // emit QSystemTrayIcon::activated. QSKIP gracefully; the menu-activator
    // slot above still covers the visibilityToggled signal.
    if (!tm.isAvailable()) {
        QSKIP("QSystemTrayIcon not available on this platform — "
              "activated(DoubleClick) routing test skipped. "
              "testMenuActivators covers visibilityToggled via the "
              "Q_INVOKABLE activator path.");
    }

    QSignalSpy spy(&tm, &TrayManager::visibilityToggled);
    QVERIFY(spy.isValid());

    // Invoke onActivated directly with DoubleClick — bypasses the platform's
    // actual mouse interaction (which we can't simulate in a unit test) but
    // exercises TrayManager's full routing logic. The slot is public so the
    // test can reach it without friend declarations.
    tm.onActivated(QSystemTrayIcon::DoubleClick);

    QCOMPARE_EQ(spy.count(), 1);
}

// ===========================================================================
// activated(Context) → requestContextMenu.
// ===========================================================================

void TrayManagerTest::testActivatedContextEmitsRequestContextMenu()
{
    TrayManager tm;
    if (!tm.isAvailable()) {
        QSKIP("QSystemTrayIcon not available on this platform — "
              "activated(Context) routing test skipped.");
    }

    QSignalSpy spy(&tm, &TrayManager::requestContextMenu);
    QVERIFY(spy.isValid());

    tm.onActivated(QSystemTrayIcon::Context);

    QCOMPARE_EQ(spy.count(), 1);
}

// ===========================================================================
// activated(Trigger / MiddleClick / Unknown) → NO signal.
// Parity with Java reference, which only handles double-click.
// ===========================================================================

void TrayManagerTest::testActivatedTriggerIgnored()
{
    TrayManager tm;
    if (!tm.isAvailable()) {
        QSKIP("QSystemTrayIcon not available on this platform — "
              "activated(ignored reasons) routing test skipped.");
    }

    QSignalSpy ctxSpy(&tm, &TrayManager::requestContextMenu);
    QSignalSpy visSpy(&tm, &TrayManager::visibilityToggled);
    QSignalSpy setSpy(&tm, &TrayManager::showSettings);
    QSignalSpy quitSpy(&tm, &TrayManager::quitRequested);

    // Single left-click, middle-click, and unknown reasons all map to no-op
    // in onActivated() — no signal should fire for any of them.
    tm.onActivated(QSystemTrayIcon::Trigger);
    tm.onActivated(QSystemTrayIcon::MiddleClick);
    tm.onActivated(QSystemTrayIcon::Unknown);

    QCOMPARE_EQ(ctxSpy.count(), 0);
    QCOMPARE_EQ(visSpy.count(), 0);
    QCOMPARE_EQ(setSpy.count(), 0);
    QCOMPARE_EQ(quitSpy.count(), 0);
}

// Custom main — QGuiApplication is required by QSystemTrayIcon +
// QGuiApplication::setQuitOnLastWindowClosed. QTEST_MAIN would create only
// QCoreApplication, which is insufficient.
int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    TrayManagerTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "TrayManagerTest.moc"
