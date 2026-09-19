#pragma once

// ScreenshotRunner — the --screenshot / --bubble visual-QA modes, extracted
// verbatim from main.cpp (P3/M1, docs/refactor §B.2). App layer: compiled
// into the exe target only, never into pet_panel_core.
//
// Contract: returns ONLY when neither QA flag is present on the command
// line. An active QA mode ends the process via std::exit() after running
// its own event loop — see the teardown-history notes in the .cpp for why
// the engine is never destroyed on these paths.

class QApplication;
class QQmlApplicationEngine;
class NotificationStreamController;

// Parses the QA flags from argv ( --screenshot | --bubble | --bubble-wait MS
// | --out DIR | --pages a,b,c | --delay MS ), then:
//
//   --bubble      pushes 2 test bubbles, waits --bubble-wait ms, grabs the
//                 FULL screen and saves ./bubble-screen.png (the
//                 BubbleStreamWindow is a separate native window, so the
//                 main window's grabWindow cannot capture it —
//                 QScreen::grabWindow can).
//   --screenshot  for each requested page (default welcome,monitor,settings):
//                 root.switchPage(name) via QMetaObject::invokeMethod, wait
//                 --delay ms (default 800) for bindings/animations to settle,
//                 grab via QQuickWindow::grabWindow(), save "<out>/<page>.png"
//                 (default out: "screenshots"). The out directory is created
//                 up front; a failed mkpath or any failed save flips the
//                 process exit code to failure (P1b fix).
void runScreenshotQa(QApplication& app, QQmlApplicationEngine& engine,
                     NotificationStreamController& notificationStream,
                     int argc, char* argv[]);
