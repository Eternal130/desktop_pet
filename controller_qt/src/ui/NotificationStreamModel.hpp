#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>

#include <functional>

// NotificationStreamModel (bubble-stream Phase 1) — QAbstractListModel of the
// ACTIVE notification bubbles. Multiple pet instances feed ONE shared stream
// (pattern references: MonitorDataModel's ring cap + InstanceManager's roster
// model).
//
// Row 0 is the NEWEST bubble: push() PREPENDS. A hard cap of 6 entries drops
// the OLDEST bubble on overflow (removeLast), mirroring competitor behavior
// (frameless top-right stack, newest on top).
//
// Expiry: each bubble carries createdAtMs/expiresAtMs; an internal 1s QTimer
// (parented to this model) drives expiry by calling expireNow() every tick.
// Tests call expireNow() DIRECTLY with an injectable now — no timing
// dependence.
//
// Roles: avatar, name, text, createdAtMs, expiresAtMs.
class NotificationStreamModel : public QAbstractListModel {
    Q_OBJECT
    // Roster size for QML (window visible-binding + Repeater bookkeeping) —
    // same pattern as InstanceManager::count.
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    static constexpr int kMaxEntries = 6;

    enum Roles {
        AvatarRole = Qt::UserRole + 1,
        NameRole,
        TextRole,
        CreatedAtMsRole,
        ExpiresAtMsRole,
    };
    Q_ENUM(Roles)

    explicit NotificationStreamModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_entries.size(); }

    // Prepend a bubble (newest first). durationMs <= 0 → no auto-expiry
    // (expiresAtMs == createdAtMs + default handled by the caller; here a
    // non-positive duration means "never expires"). Overlow beyond
    // kMaxEntries drops the OLDEST entry.
    void push(const QString& name, const QString& avatar, const QString& text,
              int durationMs);

    // Remove every entry whose expiresAtMs deadline has passed. Returns the
    // number removed. Uses the injectable clock (real wall clock by default).
    int expireNow();

    // Remove one bubble by row. Invalid row → WARN no-op.
    Q_INVOKABLE void dismiss(int row);

    // Test seam: fixed clock for expireNow(). Pass an empty function to
    // restore the real wall clock.
    void setClock(std::function<qint64()> clock) { m_clock = std::move(clock); }

    // Test seam: read one entry's field by row (QVariant for QCOMPARE ease).
    QString nameAt(int row) const;
    QString textAt(int row) const;
    qint64 expiresAtMsAt(int row) const;

signals:
    void countChanged();

private:
    struct Entry {
        QString name;
        QString avatar;
        QString text;
        qint64 createdAtMs = 0;
        qint64 expiresAtMs = 0;   // 0 = never expires
    };

    qint64 nowMs() const { return m_clock ? m_clock() : QDateTime::currentMSecsSinceEpoch(); }

    QVector<Entry> m_entries;
    QTimer* m_expireTimer = nullptr;   // 1s expiry sweep, parented to this
    std::function<qint64()> m_clock;   // injectable clock (tests)
};
