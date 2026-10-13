#include "core/PanelWindowObserver.hpp"

// spdlog MUST be included before logging/Logging.hpp — Logging.hpp
// references the SPDLOG_* macros but does NOT include spdlog headers
// itself (T5 finding in .omo/notepads/qt-controller-foundation/
// learnings.md).
#include <spdlog/spdlog.h>
#include "api/EventIds.hpp"
#include "api/IEventApi.hpp"
#include "core/EventCatalog.hpp"
#include "logging/Logging.hpp"

namespace core {

PanelWindowObserver::PanelWindowObserver(QObject* parent)
    : QObject(parent)
{
}

void PanelWindowObserver::observe(QWindow* window)
{
    if (m_window == window)
        return; // already attached (or both null)
    if (m_window != nullptr) {
        disconnect(m_window, &QWindow::visibilityChanged, this,
                   &PanelWindowObserver::onVisibilityChanged);
    }
    m_window = window;
    if (m_window == nullptr)
        return; // detach-only call
    // The post-commit single point: visibilityChanged fires AFTER the
    // window's visibility property flipped, so the publish below always
    // reads the settled state (isVisible() included).
    connect(m_window, &QWindow::visibilityChanged, this,
            &PanelWindowObserver::onVisibilityChanged);
    LOG_DEBUG("PanelWindowObserver: observing root window visibility");
}

QString PanelWindowObserver::stateForVisibility(QWindow::Visibility visibility)
{
    // The api/EventIds.hpp kEventPanelWindowState field table owns this
    // vocabulary; the two defensive rows cover enum values this UI can
    // never produce (no fullscreen path, no embedded AutomaticVisibility)
    // so a future QML change cannot smuggle an unmapped state onto the
    // bus.
    switch (visibility) {
    case QWindow::Windowed:
        return QStringLiteral("normal");
    case QWindow::Maximized:
    case QWindow::FullScreen:
        return QStringLiteral("maximized");
    case QWindow::Minimized:
        return QStringLiteral("minimized");
    case QWindow::Hidden:
        return QStringLiteral("hidden");
    case QWindow::AutomaticVisibility:
    default:
        return QStringLiteral("normal");
    }
}

void PanelWindowObserver::onVisibilityChanged(QWindow::Visibility visibility)
{
    // Pre-v1.9 wiring tolerance: attached but no publisher (degraded
    // wiring / tests) — nothing to publish, no crash.
    if (m_eventPublisher == nullptr)
        return;
    if (m_window == nullptr)
        return; // defensive: signal raced a detach
    pet::Event evt;
    evt.eventId = QString(pet::kEventPanelWindowState);
    evt.instanceUuid = QString(); // panel-level
    evt.publisherId = QString(pet::kEventPublisherHost);
    evt.cancellable = false;
    evt.payload = core::makeWindowStatePayload(
        stateForVisibility(visibility), m_window->isVisible());
    m_eventPublisher->publish(evt); // synchronous; errors are bus-level
}

} // namespace core
