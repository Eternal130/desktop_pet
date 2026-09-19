// PluginPageModelTest (P4, §B.5/§B.6): nav-row model — qrc-only qmlUrl
// enforcement, (order, title) sorting, duplicate rejection, boot-window
// closure ("logged and ignored"), bridge exposure.
#include <QTest>

#include "core/PluginBridge.hpp"
#include "core/PluginPageModel.hpp"

using core::PluginPageModel;

namespace {
pet::PageDescriptor page(const QString& title, const QString& qmlUrl, int order)
{
    pet::PageDescriptor d;
    d.title = title;
    d.qmlUrl = qmlUrl;
    d.order = order;
    return d;
}
} // namespace

class PluginPageModelTest : public QObject
{
    Q_OBJECT

private slots:
    void testAddAndRoles();
    void testSortByOrderThenTitle();
    void testRejectsNonQrcUrl();
    void testRejectsDuplicatePlugin();
    void testFinalizeClosesRegistrationWindow();
    void testQmlUrlForAndIsPluginPage();
    void testBridgePerRow();
    void testEmptyTitleFallsBackToPluginId();
};

void PluginPageModelTest::testAddAndRoles()
{
    PluginPageModel model;
    QVERIFY(model.addPage(QStringLiteral("org.test.a"),
                          page(QStringLiteral("A"), QStringLiteral("qrc:/x/A.qml"), 100)));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), PluginPageModel::PageKeyRole).toString(),
             QStringLiteral("org.test.a"));
    QCOMPARE(model.data(model.index(0, 0), PluginPageModel::TitleRole).toString(),
             QStringLiteral("A"));
    QCOMPARE(model.data(model.index(0, 0), PluginPageModel::QmlUrlRole).toString(),
             QStringLiteral("qrc:/x/A.qml"));
    QCOMPARE(model.data(model.index(0, 0), PluginPageModel::OrderRole).toInt(), 100);
    QCOMPARE(model.count(), 1);
}

void PluginPageModelTest::testSortByOrderThenTitle()
{
    PluginPageModel model;
    QVERIFY(model.addPage("org.test.c", page("C", "qrc:/c.qml", 200)));
    QVERIFY(model.addPage("org.test.b", page("Beta",  "qrc:/b.qml", 100)));
    QVERIFY(model.addPage("org.test.a", page("Alpha", "qrc:/a.qml", 100))); // tie → title
    QCOMPARE(model.data(model.index(0, 0), PluginPageModel::PageKeyRole).toString(),
             QStringLiteral("org.test.a"));
    QCOMPARE(model.data(model.index(1, 0), PluginPageModel::PageKeyRole).toString(),
             QStringLiteral("org.test.b"));
    QCOMPARE(model.data(model.index(2, 0), PluginPageModel::PageKeyRole).toString(),
             QStringLiteral("org.test.c"));
}

void PluginPageModelTest::testRejectsNonQrcUrl()
{
    PluginPageModel model;
    QVERIFY(!model.addPage("org.test.fs", page("FS", "file:///etc/passwd", 100)));
    QVERIFY(!model.addPage("org.test.http", page("HTTP", "https://evil/x.qml", 100)));
    QVERIFY(!model.addPage("org.test.empty", page("E", "", 100)));
    QCOMPARE(model.rowCount(), 0); // nothing registered
}

void PluginPageModelTest::testRejectsDuplicatePlugin()
{
    PluginPageModel model;
    QVERIFY(model.addPage("org.test.a", page("A", "qrc:/a.qml", 100)));
    QVERIFY(!model.addPage("org.test.a", page("A2", "qrc:/a2.qml", 50)));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), PluginPageModel::TitleRole).toString(),
             QStringLiteral("A")); // original row untouched
}

void PluginPageModelTest::testFinalizeClosesRegistrationWindow()
{
    PluginPageModel model;
    QVERIFY(model.addPage("org.test.a", page("A", "qrc:/a.qml", 100)));
    model.finalizeRegistrations(); // host calls after initializeAll
    QVERIFY(!model.addPage("org.test.b", page("B", "qrc:/b.qml", 100))); // logged+ignored
    QCOMPARE(model.rowCount(), 1);
}

void PluginPageModelTest::testQmlUrlForAndIsPluginPage()
{
    PluginPageModel model;
    model.addPage("org.test.a", page("A", "qrc:/a.qml", 100));
    QCOMPARE(model.qmlUrlFor(QStringLiteral("org.test.a")),
             QStringLiteral("qrc:/a.qml"));
    QCOMPARE(model.qmlUrlFor(QStringLiteral("no.such.plugin")), QString());
    QVERIFY(model.isPluginPage(QStringLiteral("org.test.a")));
    QVERIFY(!model.isPluginPage(QStringLiteral("welcome")));
    QVERIFY(!model.isPluginPage(QStringLiteral("no.such.plugin")));
}

void PluginPageModelTest::testBridgePerRow()
{
    PluginPageModel model;
    model.addPage("org.test.a", page("A", "qrc:/a.qml", 100));
    model.addPage("org.test.b", page("B", "qrc:/b.qml", 200));
    auto* bridgeA = model.data(model.index(0, 0), PluginPageModel::BridgeRole)
                        .value<core::PluginBridge*>();
    auto* bridgeB = model.data(model.index(1, 0), PluginPageModel::BridgeRole)
                        .value<core::PluginBridge*>();
    QVERIFY(bridgeA != nullptr);
    QVERIFY(bridgeB != nullptr);
    QVERIFY(bridgeA != bridgeB);
    QCOMPARE(bridgeA->pluginId(), QStringLiteral("org.test.a"));
    QCOMPARE(bridgeB->title(), QStringLiteral("B"));
}

void PluginPageModelTest::testEmptyTitleFallsBackToPluginId()
{
    PluginPageModel model;
    QVERIFY(model.addPage("org.test.a", page("", "qrc:/a.qml", 100)));
    QCOMPARE(model.data(model.index(0, 0), PluginPageModel::TitleRole).toString(),
             QStringLiteral("org.test.a"));
}

QTEST_APPLESS_MAIN(PluginPageModelTest)
#include "PluginPageModelTest.moc"
