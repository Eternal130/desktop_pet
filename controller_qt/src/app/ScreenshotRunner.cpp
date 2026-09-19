#include "app/ScreenshotRunner.hpp"

#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTimer>
#include <QDir>
#include <QGuiApplication>
#include <QScreen>
#include <QPixmap>
#include <QImage>
#include <QVariant>
#include <QStringList>

#include <cstdio>
#include <cstdlib>

#include "logging/Logging.hpp"
#include "ui/NotificationStreamController.hpp"

// Moved verbatim from main.cpp (P3/M1). Behavior contract, incl. the two
// P1b fixes (out-dir mkpath + per-save verification), is byte-equivalent:
// same context property access, same connect order, same exit codes.

void runScreenshotQa(QApplication& app, QQmlApplicationEngine& engine,
                     NotificationStreamController& notificationStream,
                     int argc, char* argv[])
{
    // ── Screenshot mode (visual QA, 方案3) ─────────────────────────────────
    // Usage: controller --screenshot [--out DIR] [--pages welcome,monitor,settings]
    // [--delay MS] [--settings-anchor SECTION]. Waits for the window to render,
    // then for each requested
    // page: calls root.switchPage(name) via QMetaObject::invokeMethod, waits
    // --delay ms (default 800) for bindings/animations to settle, grabs the
    // window via QQuickWindow::grabWindow() and saves
    // "<out>/<page>.png" (default out: "screenshots"). Quits with exit code 0.
    // Purely additive: without --screenshot the run loop below is unchanged.
    bool screenshotMode = false;
    bool bubbleShot = false;
    int bubbleWaitMs = 0;
    QString outDir = QStringLiteral("screenshots");
    QStringList pages = {QStringLiteral("welcome"), QStringLiteral("monitor"),
                         QStringLiteral("settings")};
    int delayMs = 800;
    QString settingsAnchor;   // dev/QA: scroll target for the settings page
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromUtf8(argv[i]);
        if (arg == QStringLiteral("--screenshot")) screenshotMode = true;
        else if (arg == QStringLiteral("--bubble")) bubbleShot = true;
        else if (arg == QStringLiteral("--bubble-wait") && i + 1 < argc)
            bubbleWaitMs = QString::fromUtf8(argv[++i]).toInt();
        else if (arg == QStringLiteral("--out") && i + 1 < argc)
            outDir = QString::fromUtf8(argv[++i]);
        else if (arg == QStringLiteral("--pages") && i + 1 < argc)
            pages = QString::fromUtf8(argv[++i]).split(QLatin1Char(','),
                                                       Qt::SkipEmptyParts);
        else if (arg == QStringLiteral("--delay") && i + 1 < argc)
            delayMs = QString::fromUtf8(argv[++i]).toInt();
        else if (arg == QStringLiteral("--settings-anchor") && i + 1 < argc)
            settingsAnchor = QString::fromUtf8(argv[++i]);
    }
    // Bubble visual-QA mode: push a bubble, grab the FULL SCREEN (the
    // BubbleStreamWindow is a separate native window — the main window's
    // grabWindow cannot capture it; QScreen::grabWindow can).
    if (bubbleShot && !engine.rootObjects().isEmpty()) {
        auto* runner = new QObject(&app);
        auto* ticker = new QTimer(runner);
        ticker->setInterval(1500);
        const int n = 2;
        QObject::connect(ticker, &QTimer::timeout, runner,
                         [&notificationStream, runner, ticker, n,
                          bubbleWaitMs]() {
            static int pushed = 0;
            static int settledTicks = 0;
            static bool grabbed = false;
            if (pushed < n) {
                notificationStream.testBubble();
                ++pushed;
            } else if (settledTicks * 1500 < bubbleWaitMs) {
                ++settledTicks;
            } else if (!grabbed) {
                grabbed = true;
                QScreen* screen = QGuiApplication::primaryScreen();
                const QPixmap img = screen->grabWindow(0);
                const QString path = QDir(QStringLiteral("."))
                    .filePath(QStringLiteral("bubble-screen.png"));
                img.save(path);
                printf("BUBBLE_SHOT_SAVED=%s\n", path.toStdString().c_str());
                fflush(stdout);
                ticker->stop();
                QApplication::quit();
            }
        });
        QTimer::singleShot(1500, runner, [ticker]() { ticker->start(); });
        const int bubbleExit = app.exec();
        Logging::shutdown();
        std::exit(bubbleExit == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
    }
    if (screenshotMode && !engine.rootObjects().isEmpty()) {
        // P1b fix (docs/refactor/baselines/p1a-verification §3): the out
        // directory used to never be created — SCREENSHOT_SAVED was
        // printed even though nothing hit the disk. Create it up front
        // and hard-fail when that is not possible.
        if (!QDir().mkpath(outDir)) {
            fprintf(stderr, "SCREENSHOT_OUTDIR_FAILED=%s\n",
                    outDir.toStdString().c_str());
            fflush(stderr);
            Logging::shutdown();
            std::exit(EXIT_FAILURE);
        }
        // Two-phase state machine on one repeating QTimer: odd ticks switch
        // the page, even ticks grab it. All state lives as properties on
        // the heap `runner` (parented to app) so teardown after exec()
        // cannot touch dangling stack references — the earlier
        // by-reference recursive std::function crashed (0xC0000005) at exit.
        auto* runner = new QObject(&app);
        runner->setProperty("root", QVariant::fromValue<QObject*>(
            engine.rootObjects().first()));
        runner->setProperty("outDir", outDir);
        runner->setProperty("pages", pages);
        runner->setProperty("idx", 0);
        runner->setProperty("awaitingGrab", false);
        runner->setProperty("failed", false);
        runner->setProperty("settingsAnchor", settingsAnchor);

        auto* ticker = new QTimer(runner);
        ticker->setInterval(delayMs);
        QObject::connect(ticker, &QTimer::timeout, runner,
                         [runner, ticker]() {
            auto* window = qobject_cast<QQuickWindow*>(
                runner->property("root").value<QObject*>());
            const QString dir = runner->property("outDir").toString();
            const QStringList pages =
                runner->property("pages").toStringList();
            const int idx = runner->property("idx").toInt();
            const bool awaiting = runner->property("awaitingGrab").toBool();

            if (awaiting) {
                runner->setProperty("awaitingGrab", false);
                if (window) {
                    const QImage img = window->grabWindow();
                    const QString path = QDir(dir).filePath(
                        pages.at(idx) + QStringLiteral(".png"));
                    // P1b fix: verify the save actually landed (full disk,
                    // permissions, unwritable path) — a silent failure
                    // must surface as SCREENSHOT_FAILED and a non-zero
                    // exit, not a bogus SCREENSHOT_SAVED.
                    if (img.save(path)) {
                        printf("SCREENSHOT_SAVED=%s\n",
                               path.toStdString().c_str());
                    } else {
                        runner->setProperty("failed", true);
                        printf("SCREENSHOT_FAILED=%s\n",
                               path.toStdString().c_str());
                    }
                    fflush(stdout);
                }
                runner->setProperty("idx", idx + 1);
                if (idx + 1 >= pages.size()) {
                    ticker->stop();
                    QApplication::quit();
                }
            } else if (idx < pages.size()) {
                // Dev/QA seam: anchor-scroll the settings page right before
                // it loads (Main.qml devSetSettingsAnchor) so below-the-fold
                // cards (plugin management) can be captured.
                const QString anchor =
                    runner->property("settingsAnchor").toString();
                if (pages.at(idx) == QStringLiteral("settings")
                        && !anchor.isEmpty()) {
                    QMetaObject::invokeMethod(window, "devSetSettingsAnchor",
                                              Q_ARG(QVariant, anchor));
                }
                QMetaObject::invokeMethod(window, "switchPage",
                                          Q_ARG(QVariant, pages.at(idx)));
                runner->setProperty("awaitingGrab", true);
            }
        });
        QTimer::singleShot(1200, runner, [ticker]() { ticker->start(); });
        const int shotExit = app.exec();
        Logging::shutdown();
        // Test-only path: exit directly. Destroying the QML engine
        // after an in-exec quit still corrupts the heap (0xC0000374,
        // reproducible WITHOUT FluentUI — so not a plugin artifact).
        // Screenshots are already on disk; the production close path
        // (normal window close) is unaffected. P1b: any failed save
        // flips the exit code so CI/QA scripts notice missing PNGs.
        std::exit(runner->property("failed").toBool() ? EXIT_FAILURE
                                                      : shotExit);
    }
}
