#pragma once

#include <QObject>
#include <QString>

#include "ui/NotificationStreamModel.hpp"

// NotificationStreamController (bubble stream) — QML bridge for the
// desktop-level notification bubble stream, exposed as the context property
// "notificationStream" (PanelConfigController bridge pattern). Owns the
// NotificationStreamModel — the shared bubble list every instance feeds.
// Bubble text comes from the voice-pack behavior engine via the dialogue
// sink seam (InstanceManager::setDialogueSink); there is no dialogue-pack
// system (design revision).
//
// Q_PROPERTYs: count (read), enabled (read/write — master switch; when false
// the BubbleStreamWindow hides and push() becomes a no-op).
// Q_INVOKABLEs: push / dismiss / testBubble.
class NotificationStreamController : public QObject {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged)
    // Must be a Q_PROPERTY: QML `notificationStream.model` on a plain getter
    // yields a function reference, not the model — the Repeater would stay empty.
    Q_PROPERTY(NotificationStreamModel* model READ model CONSTANT)

public:
    static constexpr int kDefaultDurationMs = 8000;

    explicit NotificationStreamController(QObject* parent = nullptr);

    int count() const { return m_model.count(); }
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    // Direct bubble push (durationMs <= 0 → kDefaultDurationMs). No-op when
    // the stream is disabled.
    Q_INVOKABLE void push(const QString& name, const QString& avatar,
                          const QString& text, int durationMs = -1);
    Q_INVOKABLE void dismiss(int row);

    // QA helper: pushes a canned bubble (verifies the whole chain from QML).
    Q_INVOKABLE void testBubble();

    // Exposed for BubbleStreamWindow's Repeater: the underlying model.
    NotificationStreamModel* model() { return &m_model; }

signals:
    void countChanged();
    void enabledChanged();

private:
    NotificationStreamModel m_model;
    bool m_enabled = true;
};
