#include <QJsonObject>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QSignalSpy>
#include <QString>
#include <QTest>
#include <QUrl>
#include <QWebSocket>

#include "network/Envelope.hpp"
#include "network/WsServer.hpp"

Q_DECLARE_METATYPE(Envelope)

// HandshakeTest (T8) — drives the full 3-gate token handshake + connection
// management with a real QWebSocket client against a real WsServer on
// 127.0.0.1 with an OS-assigned port (listen(0)). Each slot covers one gate
// or lifecycle rule from blueprint §3.3 + handshake.md §4. QTEST_MAIN (NOT
// APPLESS): WsServer + QWebSocket need a QCoreApplication event loop, pumped
// by QSignalSpy::wait.
class HandshakeTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void testGateAOriginRejected4001();
    void testGateBInstanceIdMissing4000();
    void testGateCTokenMismatch4002();
    void testGoodCredsAccepted();
    void testConnectionReplaceCloses1000();
    void testReadyTimeoutClosesConnection();

private:
    // Build a good-creds URL for the given instance_id/token + port.
    static QString goodUrl(quint16 port, int instanceId, const QString& token)
    {
        return QStringLiteral("ws://127.0.0.1:%1/?instance_id=%2&token=%3")
            .arg(port)
            .arg(instanceId)
            .arg(token);
    }
};

void HandshakeTest::initTestCase()
{
    qRegisterMetaType<Envelope>();
    qRegisterMetaType<WsConnectionState>();
    qRegisterMetaType<ConnectionInfo>();
}

// Gate a — a browser-style Origin is rejected with close code 4001 before any
// instance_id/token check runs.
void HandshakeTest::testGateAOriginRejected4001()
{
    WsServer server;
    QVERIFY(server.listen(0));
    const quint16 port = server.serverPort();

    QWebSocket client;
    QNetworkRequest req(QUrl(QStringLiteral("ws://127.0.0.1:%1/").arg(port)));
    req.setRawHeader("Origin", "http://evil.com");

    QSignalSpy rejectSpy(&server, &WsServer::connectionRejected);
    QSignalSpy disconnectedSpy(&client, &QWebSocket::disconnected);

    client.open(req);
    QVERIFY2(disconnectedSpy.wait(2000), "client not closed by server within 2s");

    QCOMPARE(rejectSpy.count(), 1);
    QCOMPARE(rejectSpy.takeFirst().at(0).toInt(), 4001);
    QCOMPARE(client.closeCode(), 4001);

    client.close();
    server.close();
}

// Gate b — a missing instance_id query param is rejected with close code 4000.
// A token is registered for instance 0, but the URL omits instance_id entirely.
void HandshakeTest::testGateBInstanceIdMissing4000()
{
    WsServer server;
    QVERIFY(server.listen(0));
    const quint16 port = server.serverPort();
    server.registerToken(0, QStringLiteral("goodtoken"));

    QWebSocket client;
    // instance_id intentionally absent.
    const QString url = QStringLiteral("ws://127.0.0.1:%1/?token=goodtoken").arg(port);

    QSignalSpy rejectSpy(&server, &WsServer::connectionRejected);
    QSignalSpy disconnectedSpy(&client, &QWebSocket::disconnected);

    client.open(QUrl(url));
    QVERIFY2(disconnectedSpy.wait(2000), "client not closed by server within 2s");

    QCOMPARE(rejectSpy.count(), 1);
    QCOMPARE(rejectSpy.takeFirst().at(0).toInt(), 4000);
    QCOMPARE(client.closeCode(), 4000);

    client.close();
    server.close();
}

// Gate c — a token that does not match the registered value for the
// instance_id is rejected with close code 4002.
void HandshakeTest::testGateCTokenMismatch4002()
{
    WsServer server;
    QVERIFY(server.listen(0));
    const quint16 port = server.serverPort();
    server.registerToken(0, QStringLiteral("goodtoken"));

    QWebSocket client;

    QSignalSpy rejectSpy(&server, &WsServer::connectionRejected);
    QSignalSpy disconnectedSpy(&client, &QWebSocket::disconnected);

    client.open(QUrl(goodUrl(port, 0, QStringLiteral("badtoken"))));
    QVERIFY2(disconnectedSpy.wait(2000), "client not closed by server within 2s");

    QCOMPARE(rejectSpy.count(), 1);
    QCOMPARE(rejectSpy.takeFirst().at(0).toInt(), 4002);
    QCOMPARE(client.closeCode(), 4002);

    client.close();
    server.close();
}

// Happy path — all 3 gates pass and the connection is accepted. Sending `ready`
// stops the ready-timeout so the connection stays open.
void HandshakeTest::testGoodCredsAccepted()
{
    WsServer server;
    QVERIFY(server.listen(0));
    const quint16 port = server.serverPort();
    server.registerToken(0, QStringLiteral("goodtoken"));

    QWebSocket client;
    QSignalSpy clientConnectedSpy(&client, &QWebSocket::connected);
    QSignalSpy serverConnectedSpy(&server, &WsServer::connectionStateChanged);

    client.open(QUrl(goodUrl(port, 0, QStringLiteral("goodtoken"))));
    QVERIFY2(serverConnectedSpy.wait(5000), "server Connected not received within 5s");
    if (clientConnectedSpy.isEmpty())
        QVERIFY2(clientConnectedSpy.wait(2000), "client connected not received");

    QCOMPARE(serverConnectedSpy.count(), 1);
    const auto state = serverConnectedSpy.takeFirst().at(0).value<WsConnectionState>();
    QCOMPARE(state, WsConnectionState::Connected);

    // Send `ready` to stop the ready-timeout (handshake.md §3.1).
    QSignalSpy msgSpy(&server, &WsServer::messageReceived);
    const QByteArray ready = serialize(createEvent(
        QStringLiteral("ready"),
        QJsonObject{{QStringLiteral("version"), QStringLiteral("1.0.0")}}))
            .toJson(QJsonDocument::Compact);
    QVERIFY(client.sendTextMessage(QString::fromUtf8(ready)) > 0);
    QVERIFY2(msgSpy.wait(5000), "server did not receive ready within 5s");

    // No close should follow — wait and confirm the client stays connected.
    QSignalSpy disconnectedSpy(&client, &QWebSocket::disconnected);
    QVERIFY2(!disconnectedSpy.wait(1000), "connection was closed after good creds + ready");

    client.close();
    server.close();
}

// A second validated connection for the same instance_id replaces the first,
// closing it with code 1000 (interface.md: 1000 = replaced by newer connection).
void HandshakeTest::testConnectionReplaceCloses1000()
{
    WsServer server;
    QVERIFY(server.listen(0));
    const quint16 port = server.serverPort();
    server.registerToken(0, QStringLiteral("goodtoken"));

    QWebSocket client1;
    QSignalSpy client1ConnectedSpy(&client1, &QWebSocket::connected);
    QSignalSpy serverConnectedSpy(&server, &WsServer::connectionStateChanged);
    client1.open(QUrl(goodUrl(port, 0, QStringLiteral("goodtoken"))));
    QVERIFY2(serverConnectedSpy.wait(5000), "first connection not accepted within 5s");
    // Wait for client1's own handshake to complete before client2 connects, so
    // the replace sequence is clean (learnings T5: server-Connected and
    // client-connected are two distinct events).
    if (client1ConnectedSpy.isEmpty())
        QVERIFY2(client1ConnectedSpy.wait(2000), "first client never connected");

    // Second good connection for the same instance → replaces client1 (1000).
    // client1 does not need to send `ready`; the replace happens immediately
    // and restarts the ready-timeout for client2.
    QWebSocket client2;
    QSignalSpy client1DisconnectedSpy(&client1, &QWebSocket::disconnected);
    QSignalSpy client2ConnectedSpy(&client2, &QWebSocket::connected);

    client2.open(QUrl(goodUrl(port, 0, QStringLiteral("goodtoken"))));
    QVERIFY2(client1DisconnectedSpy.wait(5000),
             "first client was not closed on replace within 5s");
    QCOMPARE(client1.closeCode(), 1000);

    // Second connection is accepted (server emits Connected again).
    QVERIFY2(serverConnectedSpy.count() >= 2,
             "server did not emit Connected for the second connection");
    if (client2ConnectedSpy.isEmpty())
        QVERIFY2(client2ConnectedSpy.wait(2000), "second client never connected");

    client1.close();
    client2.close();
    server.close();
}

// If `ready` is not received within the ready-timeout window, the connection is
// closed and Disconnected is emitted (handshake.md §4). Uses a shortened
// timeout via setReadyTimeoutMs() so the case runs fast.
void HandshakeTest::testReadyTimeoutClosesConnection()
{
    WsServer server;
    server.setReadyTimeoutMs(200); // test hook: real timeout is 10000ms
    QVERIFY(server.listen(0));
    const quint16 port = server.serverPort();
    server.registerToken(0, QStringLiteral("goodtoken"));

    QWebSocket client;
    QSignalSpy serverStateSpy(&server, &WsServer::connectionStateChanged);
    QSignalSpy clientDisconnectedSpy(&client, &QWebSocket::disconnected);

    client.open(QUrl(goodUrl(port, 0, QStringLiteral("goodtoken"))));
    QVERIFY2(serverStateSpy.wait(5000), "server Connected not received within 5s");

    // Do NOT send ready. The 200ms timer must fire and close the connection.
    QVERIFY2(clientDisconnectedSpy.wait(3000),
             "connection was not closed after ready-timeout within 3s");

    // Server must have emitted Disconnected as the final state.
    QVERIFY2(serverStateSpy.count() >= 2, "server did not emit Disconnected after timeout");
    const auto last = serverStateSpy.at(serverStateSpy.count() - 1).at(0).value<WsConnectionState>();
    QCOMPARE(last, WsConnectionState::Disconnected);

    client.close();
    server.close();
}

QTEST_MAIN(HandshakeTest)
#include "HandshakeTest.moc"
