#include <QJsonObject>
#include <QSignalSpy>
#include <QString>
#include <QTest>
#include <QUrl>
#include <QWebSocket>
#include <QNetworkRequest>

#include "network/Envelope.hpp"
#include "network/WsServer.hpp"

// Metatype registration so QSignalSpy can carry Envelope (not registered in
// Envelope.hpp because that file is frozen from T4). WsConnectionState /
// ConnectionInfo are registered via Q_DECLARE_METATYPE in WsServer.hpp.
Q_DECLARE_METATYPE(Envelope)

// WsServerTest (T5) — exercises the real QWebSocketServer + QWebSocket client
// loop on 127.0.0.1 with an OS-assigned port (listen(0)). QTEST_MAIN (NOT
// APPLESS) because WsServer needs a QCoreApplication event loop for WebSocket
// I/O; QSignalSpy::wait pumps that loop.
class WsServerTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    // R1 de-risk (PRIMARY purpose of T5): assert QWebSocket::resourceName()
    // surfaces the query string the client sends, and that we parse it.
    void testResourceNameContainsQueryParams();
    void testOriginGuardClosesConnection();
    void testMessageRoundTrip();
    void testPortAutoAssign();
};

void WsServerTest::initTestCase()
{
    qRegisterMetaType<Envelope>();
    qRegisterMetaType<WsConnectionState>();
    qRegisterMetaType<ConnectionInfo>();
}

void WsServerTest::testResourceNameContainsQueryParams()
{
    WsServer server;
    QVERIFY2(server.listen(0), "server should listen on 127.0.0.1:0");
    const quint16 port = server.serverPort();
    QVERIFY2(port != 0, "OS should auto-assign a non-zero port");

    QWebSocket client;
    QSignalSpy connectedSpy(&server, &WsServer::connectionStateChanged);
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1/?instance_id=42&token=deadbeef")
                         .arg(port)));
    QVERIFY2(connectedSpy.wait(5000), "Connected signal not received within 5s");

    const QList<QVariant> args = connectedSpy.takeFirst();
    const auto state = args.at(0).value<WsConnectionState>();
    const auto info = args.at(1).value<ConnectionInfo>();
    QCOMPARE(state, WsConnectionState::Connected);

    // R1 PROOF — the raw resourceName() the server observed must literally
    // contain both query substrings. If this fails, resourceName() does not
    // surface the query and the QTcpServer fallback is required.
    QVERIFY2(info.rawResourceName.contains(QLatin1String("instance_id=")),
             qPrintable(QStringLiteral("resourceName() missing 'instance_id=': '%1'")
                            .arg(info.rawResourceName)));
    QVERIFY2(info.rawResourceName.contains(QLatin1String("token=")),
             qPrintable(QStringLiteral("resourceName() missing 'token=': '%1'")
                            .arg(info.rawResourceName)));

    // Parsed values derived from resourceName().
    QVERIFY2(info.hasQueryParams, "hasQueryParams should be true");
    QCOMPARE(info.instanceId, 42);
    QCOMPARE(info.token, QStringLiteral("deadbeef"));

    client.close();
    server.close();
}

void WsServerTest::testOriginGuardClosesConnection()
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
    // The server must reject within ~1s and emit connectionRejected(4001, ...).
    QVERIFY2(disconnectedSpy.wait(2000), "client was not closed by server within 2s");

    QCOMPARE(rejectSpy.count(), 1);
    const QList<QVariant> rArgs = rejectSpy.takeFirst();
    QCOMPARE(rArgs.at(0).toInt(), 4001);

    client.close();
    server.close();
}

void WsServerTest::testMessageRoundTrip()
{
    WsServer server;
    QVERIFY(server.listen(0));
    const quint16 port = server.serverPort();

    QWebSocket client;
    QSignalSpy serverConnectedSpy(&server, &WsServer::connectionStateChanged);
    QSignalSpy clientConnectedSpy(&client, &QWebSocket::connected);
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1/?instance_id=7&token=cafef00d")
                         .arg(port)));
    // Wait for BOTH sides — the server's Connected AND the client's connected,
    // so sendTextMessage below writes into a fully established socket (sending
    // before the client handshake resolves loses the bytes).
    QVERIFY2(serverConnectedSpy.wait(5000), "server Connected not received");
    if (clientConnectedSpy.isEmpty())
        QVERIFY2(clientConnectedSpy.wait(2000), "client connected not received");

    QSignalSpy msgSpy(&server, &WsServer::messageReceived);
    const Envelope cmd = createCommand(
        QStringLiteral("load_model"),
        QJsonObject{{QStringLiteral("model_path"), QStringLiteral("Hiyori")}});
    const QByteArray bytes = serialize(cmd).toJson(QJsonDocument::Compact);
    const qint64 written = client.sendTextMessage(QString::fromUtf8(bytes));
    QVERIFY2(written > 0, "sendTextMessage wrote 0 bytes");

    QVERIFY2(msgSpy.wait(5000), "messageReceived not emitted within 5s");
    QCOMPARE(msgSpy.count(), 1);

    const Envelope received = msgSpy.takeFirst().at(0).value<Envelope>();
    QCOMPARE(received.action, QStringLiteral("load_model"));
    QCOMPARE(received.type, QStringLiteral("command"));
    QCOMPARE(received.payload.value(QStringLiteral("model_path")).toString(),
             QStringLiteral("Hiyori"));

    client.close();
    server.close();
}

void WsServerTest::testPortAutoAssign()
{
    WsServer server;
    QCOMPARE(server.serverPort(), quint16(0)); // not listening yet
    QVERIFY2(server.listen(0), "listen(0) should succeed");
    QVERIFY2(server.serverPort() != 0, "serverPort() should be non-zero after listen(0)");
    QVERIFY2(server.serverPort() != 9001, "auto-assigned port should not be the default 9001");
    server.close();
    QCOMPARE(server.serverPort(), quint16(0)); // closed -> 0 again
}

QTEST_MAIN(WsServerTest)
#include "WsServerTest.moc"
