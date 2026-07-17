#include "system/TrayManager.hpp"

// spdlog MUST be included before logging/Logging.hpp — Logging.hpp references
// the SPDLOG_* macros but does NOT include spdlog headers itself (T5 finding
// in .omo/notepads/qt-controller-foundation/learnings.md). Same include
// order as AutoLaunchManager.cpp.
#include <spdlog/spdlog.h>
#include "logging/Logging.hpp"

#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QPainter>
#include <QPixmap>

// ── Icon resource path ───────────────────────────────────────────────────
// Matches the Java reference's getClass().getResource("/icons/tray-icon.png").
// Loaded via Qt's resource system if a .qrc is registered (none today —
// controller_qt has no resources/ dir), otherwise loadIcon falls through to
// the generated pixmap. Adding a real PNG asset later is a drop-in: place it
// at controller_qt/resources/icons/tray-icon.png, add a resources.qrc, and
// link via qt_add_resources — no TrayManager change required.
namespace {
const QString kIconResourcePath = QStringLiteral(":/icons/tray-icon.png");
const QString kTooltip = QStringLiteral("Desktop Pet");

// Fallback icon parameters — match the Java reference's createTrayImage():
// 16×16 ARGB buffer with a filled circle in the brand blue RGB(74,144,217).
// Scaled up to 32×32 here because the Qt tray renders larger on modern
// Windows DPI settings; Qt auto-scales via QSystemTrayIcon::setIcon size
// hints. The Java reference's smaller 16×16 looked pixelated on Win10+.
constexpr int kFallbackSize = 32;
constexpr int kBrandR = 74;
constexpr int kBrandG = 144;
constexpr int kBrandB = 217;
} // namespace

TrayManager::TrayManager(QObject* parent)
    : QObject(parent)
{
    // Blueprint §4.1.6: "需禁用框架的"关闭即退出"默认行为" — disable the
    // QGuiApplication default that quits when the last window closes. Called
    // UNCONDITIONALLY (even when no tray is available) so the close-to-tray
    // behavior works on every platform: with this set, Main.qml's onClosing
    // handler can `root.hide()` instead of `Qt.quit()`, and the app stays
    // alive. The only legit exit path from the tray is the menu's 退出 item
    // (emit quitRequested → Main.qml calls Qt.quit).
    //
    // QGuiApplication::instance() is non-null here because TrayManager is
    // constructed AFTER `QGuiApplication app(argc, argv)` in main.cpp.
    QGuiApplication::setQuitOnLastWindowClosed(false);

    // R5 risk — Linux GNOME ships no legacy SNI / XEmbed tray support by
    // default. isSystemTrayAvailable() returns false there unless the user
    // installs the AppIndicator shell extension. We LOG_WARN and continue;
    // the panel still runs, and the close button still works via closeAction
    // (todo 15) — tray features are a "nice to have", not a hard dependency.
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        LOG_WARN("TrayManager: QSystemTrayIcon::isSystemTrayAvailable() == "
                 "false — tray icon disabled. On Linux GNOME this typically "
                 "means the AppIndicator / KStatusNotifierItem shell "
                 "extension is not installed. The panel continues to run; "
                 "the close button (closeAction, todo 15) still works.");
        return;
    }

    // Construct + configure. QSystemTrayIcon is parented to `this` so Qt
    // deletes it during TrayManager destruction (no manual delete needed).
    m_tray = new QSystemTrayIcon(loadIcon(), this);
    m_tray->setToolTip(kTooltip);
    connect(m_tray, &QSystemTrayIcon::activated,
            this, &TrayManager::onActivated);

    // M3: deliberately NO setContextMenu(QMenu*) — QMenu is QtWidgets, and
    // the controller_qt app links only QtGui. The QML Menu (Main.qml) is the
    // popup; activated(Context) → requestContextMenu → trayMenu.popup().

    m_tray->show();
    m_available = true;
    LOG_INFO("TrayManager: system tray initialized (icon + tooltip \"{}\")",
             kTooltip.toStdString());
}

TrayManager::~TrayManager()
{
    // Explicit hide() before Qt tears down m_tray — avoids a brief "ghost
    // icon" lingering in the tray area during process shutdown on Windows.
    // Safe no-op when m_tray is null (no tray available).
    if (m_tray) {
        m_tray->hide();
    }
}

void TrayManager::onActivated(QSystemTrayIcon::ActivationReason reason)
{
    // M3 routing: tray gestures → our semantic signals. The QML Menu items
    // call the activate*() Q_INVOKABLEs and emit the same signals, so QML's
    // Connections block has ONE uniform handler per signal regardless of
    // whether the trigger was a tray gesture or a menu click.
    switch (reason) {
    case QSystemTrayIcon::Context:
        // Right-click — the QML Menu pops via onRequestContextMenu.
        emit requestContextMenu();
        break;
    case QSystemTrayIcon::DoubleClick:
        // Double-click — toggle the main window visibility.
        emit visibilityToggled();
        break;
    case QSystemTrayIcon::Trigger:
        // Single left-click — the Java reference ignores this (only
        // double-click toggles). Keeping the same semantics for parity: no
        // toggle on single click, which would be jarring on Windows where
        // single click is typically just "focus".
        break;
    case QSystemTrayIcon::MiddleClick:
        // Middle-click — Java reference has no handler; we leave it as a
        // no-op for now. Could be wired to a quick "show settings" later.
        break;
    case QSystemTrayIcon::Unknown:
    default:
        // Unknown reason (some platforms emit this on focus changes). Ignore.
        break;
    }
}

QIcon TrayManager::loadIcon()
{
    // Try the bundled PNG first (matches Java reference path
    // /icons/tray-icon.png). QFile::exists catches the no-.qrc case (current
    // state — no resources/ dir), and QIcon(path) returning isNull() catches
    // a corrupt / unreadable file. Either way → fallback pixmap.
    if (QFile::exists(kIconResourcePath)) {
        QIcon icon(kIconResourcePath);
        if (!icon.isNull()) {
            return icon;
        }
        LOG_WARN("TrayManager: tray-icon.png exists at {} but loaded null — "
                 "falling back to generated pixmap",
                 kIconResourcePath.toStdString());
    } else {
        LOG_INFO("TrayManager: no tray-icon.png at {} — using fallback "
                 "pixmap ({}x{} blue circle, RGB {},{},{})",
                 kIconResourcePath.toStdString(),
                 kFallbackSize, kFallbackSize, kBrandR, kBrandG, kBrandB);
    }

    // Fallback: transparent kFallbackSize × kFallbackSize pixmap with a
    // filled antialiased circle in the Java reference's brand blue. Matches
    // TrayManager.java's createTrayImage() default so the Qt port presents
    // the same default icon the JavaFX controller shipped.
    QPixmap pm(kFallbackSize, kFallbackSize);
    pm.fill(Qt::transparent);
    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(QColor(kBrandR, kBrandG, kBrandB));
    painter.setPen(Qt::NoPen);
    // drawEllipse with the full bounding rect — produces a circle since the
    // pixmap is square. (kFallbackSize-1) because QPainter's drawEllipse
    // boundary is inclusive: x+w on a 32-wide pixmap would draw one pixel
    // off the right edge.
    painter.drawEllipse(0, 0, kFallbackSize - 1, kFallbackSize - 1);
    painter.end();
    return QIcon(pm);
}
