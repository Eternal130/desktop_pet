#include <QSignalSpy>
#include <QTest>

#include "ui/NotificationStreamController.hpp"

// NotificationStreamControllerTest (bubble stream) — locks the QML bridge
// contract:
//   - push → count changes; disabled stream suppresses pushes
//   - testBubble pushes a canned bubble
// QTEST_MAIN — QSignalSpy on countChanged + the controller's internal
// QTimer-based model need a QCoreApplication.
class NotificationStreamControllerTest : public QObject
{
    Q_OBJECT

private slots:
    void testPushChangesCount();
    void testDisabledSuppressesPush();
    void testTestBubble();
};

void NotificationStreamControllerTest::testPushChangesCount()
{
    NotificationStreamController controller;
    QCOMPARE(controller.count(), 0);

    QSignalSpy countSpy(&controller,
                        &NotificationStreamController::countChanged);
    controller.push(QStringLiteral("uuid-a"), QStringLiteral("A"),
                    QStringLiteral("🐱"), QStringLiteral("hello"));
    QCOMPARE(controller.count(), 1);
    QCOMPARE(countSpy.count(), 1);

    controller.push(QStringLiteral("uuid-b"), QStringLiteral("B"), QString(),
                    QStringLiteral("again"), 5000);
    QCOMPARE(controller.count(), 2);
    QCOMPARE(controller.model()->instanceIdAt(0), QStringLiteral("uuid-b"));
    QCOMPARE(controller.model()->nameAt(0), QStringLiteral("B"));

    controller.dismiss(0);
    QCOMPARE(controller.count(), 1);
    QCOMPARE(controller.model()->nameAt(0), QStringLiteral("A"));
}

void NotificationStreamControllerTest::testDisabledSuppressesPush()
{
    NotificationStreamController controller;
    controller.setEnabled(false);
    QVERIFY(!controller.isEnabled());

    controller.push(QString(), QStringLiteral("A"), QString(),
                    QStringLiteral("nope"));
    QCOMPARE(controller.count(), 0);

    controller.setEnabled(true);
    controller.push(QString(), QStringLiteral("A"), QString(),
                    QStringLiteral("yes"));
    QCOMPARE(controller.count(), 1);
}

void NotificationStreamControllerTest::testTestBubble()
{
    NotificationStreamController controller;
    controller.testBubble();
    QCOMPARE(controller.count(), 1);
    // No pet identity on the QA bubble → instanceId "" (BubbleCard glyph
    // fallback), canned name stays (bridge has no roster access).
    QCOMPARE(controller.model()->instanceIdAt(0), QString());
    QVERIFY(controller.model()->textAt(0).contains(
        QStringLiteral("气泡信息流测试")));
}

QTEST_MAIN(NotificationStreamControllerTest)
#include "NotificationStreamControllerTest.moc"
