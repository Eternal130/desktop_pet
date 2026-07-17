#include <QJsonObject>
#include <QSignalSpy>
#include <QString>
#include <QTest>
#include <QUrl>
#include <QWebSocket>

#include "network/Envelope.hpp"
#include "network/PendingRequests.hpp"
#include "network/WsServer.hpp"

Q_DECLARE_METATYPE(Envelope)

class NetworkWiringTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void testListenOnEphemeralPort();
    void testMessageRoutesThroughConnection();
};

void NetworkWiringTest::initTestCase()
{
    qRegisterMetaType<Envelope>();
    qRegisterMetaType<WsConnectionState>();
    qRegisterMetaType<ConnectionInfo>();
}

void NetworkWiringTest::testListenOnEphemeralPort()
{
    WsServer server;
    PendingRequests pending;
    QCOMPARE(server.serverPort(), quint16(0));
    QVERIFY2(server.listen(0), "listen(0) should succeed");
    QVERIFY2(server.serverPort() > 0, "serverPort() must be > 0 after listen(0)");
    server.close();
    QCOMPARE(server.serverPort(), quint16(0));
}

void NetworkWiringTest::testMessageRoutesThroughConnection()
{
    WsServer server;
    PendingRequests pending;
    server.setReadyTimeoutMs(5000);
    QVERIFY2(server.listen(0), "listen(0) failed");
    const quint16 port = server.serverPort();
    QVERIFY2(port > 0, "serverPort() must be > 0");

    server.registerToken(0, QStringLiteral("wiringtoken"));

    QSignalSpy serverConnectedSpy(&server, &WsServer::connectionStateChanged);
    QVERIFY2(serverConnectedSpy.isValid(), "connectionStateChanged spy invalid");

    QWebSocket client;
    QSignalSpy clientConnectedSpy(&client, &QWebSocket::connected);
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1/?instance_id=0&token=wiringtoken")
                         .arg(port)));
    QVERIFY2(serverConnectedSpy.wait(5000), "server Connected not received within 5s");
    if (clientConnectedSpy.isEmpty())
        QVERIFY2(clientConnectedSpy.wait(2000), "client connected not received within 2s");

    QSignalSpy msgSpy(&server, &WsServer::messageReceived);
    QVERIFY2(msgSpy.isValid(), "messageReceived spy invalid");

    const Envelope cmd = createCommand(
        QStringLiteral("load_model"),
        QJsonObject{{QStringLiteral("model_path"), QStringLiteral("Hiyori")}});
    const QByteArray bytes = serialize(cmd).toJson(QJsonDocument::Compact);
    const qint64 written = client.sendTextMessage(QString::fromUtf8(bytes));
    QVERIFY2(written > 0, "sendTextMessage wrote 0 bytes");

    QVERIFY2(msgSpy.wait(5000), "messageReceived not emitted within 5s");
    QCOMPARE(msgSpy.count(), 1);

    // Phase 5 todo 11: signal carries (int instanceId, const Envelope&).
    const QList<QVariant> captured = msgSpy.takeFirst();
    QCOMPARE(captured.at(0).toInt(), 0); // instance_id=0 was in the connect URL
    const Envelope received = captured.at(1).value<Envelope>();
    QCOMPARE(received.type, QStringLiteral("command"));
    QCOMPARE(received.action, QStringLiteral("load_model"));
    QCOMPARE(received.payload.value(QStringLiteral("model_path")).toString(),
             QStringLiteral("Hiyori"));

    client.close();
    server.close();
}

QTEST_MAIN(NetworkWiringTest)
#include "NetworkWiringTest.moc"
