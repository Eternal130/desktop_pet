#include "ui/NotificationStreamController.hpp"

// spdlog MUST be included before logging/Logging.hpp — Logging.hpp references
// the SPDLOG_* macros but does NOT include spdlog headers itself (T5 finding).
#include <spdlog/spdlog.h>
#include "logging/Logging.hpp"

NotificationStreamController::NotificationStreamController(QObject* parent)
    : QObject(parent)
{
    connect(&m_model, &NotificationStreamModel::countChanged,
            this, &NotificationStreamController::countChanged);
}

void NotificationStreamController::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    emit enabledChanged();
    LOG_INFO("NotificationStream: enabled -> {}", enabled);
}

void NotificationStreamController::push(const QString& name,
                                        const QString& avatar,
                                        const QString& text,
                                        int durationMs)
{
    if (!m_enabled) {
        LOG_DEBUG("NotificationStream: push suppressed (disabled)");
        return;
    }
    const int effective = durationMs > 0 ? durationMs : kDefaultDurationMs;
    m_model.push(name, avatar, text, effective);
}

void NotificationStreamController::dismiss(int row)
{
    m_model.dismiss(row);
}

void NotificationStreamController::testBubble()
{
    push(QStringLiteral("Hiyori"), QStringLiteral("🐾"),
         QStringLiteral("气泡信息流测试 — 如果你看到这条气泡，说明通知流工作正常。"),
         kDefaultDurationMs);
}
