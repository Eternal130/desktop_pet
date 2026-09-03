#include <QDateTime>
#include <QSignalSpy>
#include <QTest>

#include "ui/NotificationStreamModel.hpp"

// NotificationStreamModelTest (bubble-stream Phase 1) — locks the bubble
// list-model contract BubbleStreamWindow.qml relies on:
//   - push PREPENDS (row 0 = newest)
//   - hard cap 6 entries, overflow drops the OLDEST
//   - expireNow() removes overdue entries (injectable clock — no timing
//     dependence; entries with expiresAtMs == 0 never expire)
//   - dismiss(row) removes exactly that row; invalid row no-ops
//   - roleNames expose avatar/name/text/createdAtMs/expiresAtMs
//
// Recompiles NotificationStreamModel.cpp directly (same pattern as
// MonitorDataModelTest — no shared lib yet). QTEST_MAIN (NOT APPLESS): the
// model's internal 1s QTimer needs a QCoreApplication (it never fires in
// these slots — every expiry assertion calls expireNow() directly).
class NotificationStreamModelTest : public QObject
{
    Q_OBJECT

private slots:
    void testPushPrepends();
    void testCapSixDropsOldest();
    void testExpireNowRemovesOverdue();
    void testDismiss();
    void testRoleNames();
};

void NotificationStreamModelTest::testPushPrepends()
{
    NotificationStreamModel m;
    QCOMPARE(m.count(), 0);
    QCOMPARE(m.rowCount(), 0);

    m.push(QStringLiteral("A"), QStringLiteral("🐱"), QStringLiteral("first"), 0);
    m.push(QStringLiteral("B"), QStringLiteral("🐶"), QStringLiteral("second"), 0);
    m.push(QStringLiteral("C"), QStringLiteral("🐾"), QStringLiteral("third"), 0);

    QCOMPARE(m.count(), 3);
    QCOMPARE(m.nameAt(0), QStringLiteral("C"));
    QCOMPARE(m.nameAt(1), QStringLiteral("B"));
    QCOMPARE(m.nameAt(2), QStringLiteral("A"));
    QCOMPARE(m.textAt(0), QStringLiteral("third"));

    // durationMs 0 → never expires (expiresAtMs sentinel 0).
    QCOMPARE(m.expiresAtMsAt(0), qint64(0));
}

void NotificationStreamModelTest::testCapSixDropsOldest()
{
    NotificationStreamModel m;
    for (int i = 0; i < 8; ++i) {
        m.push(QStringLiteral("n%1").arg(i), QString(), QStringLiteral("t"), 0);
    }

    // 8 pushes → capped at 6; the two OLDEST (n0, n1) are dropped.
    QCOMPARE(m.count(), NotificationStreamModel::kMaxEntries);
    QCOMPARE(m.count(), 6);
    QCOMPARE(m.nameAt(0), QStringLiteral("n7"));   // newest survived
    QCOMPARE(m.nameAt(5), QStringLiteral("n2"));   // oldest survivor
}

void NotificationStreamModelTest::testExpireNowRemovesOverdue()
{
    NotificationStreamModel m;

    qint64 now = 1000000;
    m.setClock([&now] { return now; });

    m.push(QStringLiteral("old"), QString(), QStringLiteral("expired"), 5000);
    m.push(QStringLiteral("keep"), QString(), QStringLiteral("fresh"), 60000);
    m.push(QStringLiteral("forever"), QString(), QStringLiteral("no expiry"), 0);
    QCOMPARE(m.count(), 3);

    // Advance past the 5s deadline of "old" but not the 60s of "keep".
    now = 1000000 + 6000;
    const int removed = m.expireNow();
    QCOMPARE(removed, 1);
    QCOMPARE(m.count(), 2);
    QCOMPARE(m.nameAt(0), QStringLiteral("forever"));  // newest (pushed last)
    QCOMPARE(m.nameAt(1), QStringLiteral("keep"));

    // Expire-now boundary: exactly at the deadline counts as expired (>=).
    now = 1000000 + 60000;
    QCOMPARE(m.expireNow(), 1);
    QCOMPARE(m.count(), 1);
    QCOMPARE(m.nameAt(0), QStringLiteral("forever"));

    // The never-expire entry survives any clock.
    now += 10 * 365 * 24 * 3600 * 1000LL;
    QCOMPARE(m.expireNow(), 0);
    QCOMPARE(m.count(), 1);
}

void NotificationStreamModelTest::testDismiss()
{
    NotificationStreamModel m;
    m.push(QStringLiteral("A"), QString(), QStringLiteral("1"), 0);
    m.push(QStringLiteral("B"), QString(), QStringLiteral("2"), 0);
    m.push(QStringLiteral("C"), QString(), QStringLiteral("3"), 0);

    QSignalSpy countSpy(&m, &NotificationStreamModel::countChanged);
    m.dismiss(1);
    QCOMPARE(m.count(), 2);
    QCOMPARE(countSpy.count(), 1);
    QCOMPARE(m.nameAt(0), QStringLiteral("C"));
    QCOMPARE(m.nameAt(1), QStringLiteral("A"));

    // Invalid rows are WARN no-ops (no crash, no signal).
    const int signalsBefore = countSpy.count();
    m.dismiss(-1);
    m.dismiss(99);
    QCOMPARE(m.count(), 2);
    QCOMPARE(countSpy.count(), signalsBefore);
}

void NotificationStreamModelTest::testRoleNames()
{
    NotificationStreamModel m;
    const QHash<int, QByteArray> roles = m.roleNames();
    QCOMPARE(roles.size(), 5);
    QVERIFY(roles.key("avatar", 0) != 0);
    QVERIFY(roles.key("name", 0) != 0);
    QVERIFY(roles.key("text", 0) != 0);
    QVERIFY(roles.key("createdAtMs", 0) != 0);
    QVERIFY(roles.key("expiresAtMs", 0) != 0);

    // data() serves every role for a valid index.
    m.push(QStringLiteral("Hiyori"), QStringLiteral("🐾"),
           QStringLiteral("hello"), 10000);
    const QModelIndex idx = m.index(0, 0);
    QVERIFY(idx.isValid());
    QCOMPARE(idx.data(NotificationStreamModel::AvatarRole).toString(),
             QStringLiteral("🐾"));
    QCOMPARE(idx.data(NotificationStreamModel::NameRole).toString(),
             QStringLiteral("Hiyori"));
    QCOMPARE(idx.data(NotificationStreamModel::TextRole).toString(),
             QStringLiteral("hello"));
    QVERIFY(idx.data(NotificationStreamModel::CreatedAtMsRole).toLongLong() > 0);
    QVERIFY(idx.data(NotificationStreamModel::ExpiresAtMsRole).toLongLong()
             > idx.data(NotificationStreamModel::CreatedAtMsRole).toLongLong());
}

QTEST_MAIN(NotificationStreamModelTest)
#include "NotificationStreamModelTest.moc"
