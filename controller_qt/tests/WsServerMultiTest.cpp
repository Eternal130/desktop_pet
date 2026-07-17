// WsServerMultiTest (Phase 5, todo 11) — multi-instance routing.
//
// Asserts that WsServer routes N concurrent connections independently keyed
// by ?instance_id=N, that messageReceived fires with the right instanceId for
// each, that sendText(instanceId, ...) reaches only the right socket, that a
// disconnect on one instance leaves the others untouched, that a wrong-token
// probe for an existing instance_id does NOT evict the good connection, and
// that a reconnect with the correct token closes the old socket (1000) and
// replaces it.
//
// QTEST_MAIN (NOT APPLESS) — WsServer + QWebSocket need the QCoreApplication
// event loop that QSignalSpy::wait pumps.

#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QString>
#include <QTest>
#include <QUrl>
#include <QWebSocket>

#include "network/Envelope.hpp"
#include "network/WsServer.hpp"

Q_DECLARE_METATYPE(Envelope)

namespace {
constexpr int kReadyTimeoutMs = 30000; // generous — these slots never send `ready`

QString urlFor(quint16 port, int instanceId, const QString& token)
{
    return QStringLiteral("ws://127.0.0.1:%1/?instance_id=%2&token=%3")
        .arg(port).arg(instanceId).arg(token);
}

Envelope makeEvent(const char* action)
{
    return createEvent(QString::fromLatin1(action));
}

// Build an event JSON string from an action (no payload needed for routing tests).
QByteArray eventJson(const char* action)
{
    return serialize(makeEvent(action)).toJson(QJsonDocument::Compact);
}
} // namespace

class WsServerMultiTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void testTwoInstancesRouteCorrectly();
    void testSendTextRoutesToRightInstance();
    void testDisconnectLeavesOthersUntouched();
    void testWrongTokenDoesNotEvictGoodConnection();
    void testReconnectClosesOldAndReplaces();
};

void WsServerMultiTest::initTestCase()
{
    qRegisterMetaType<Envelope>();
    qRegisterMetaType<WsConnectionState>();
    qRegisterMetaType<ConnectionInfo>();
}

// Two concurrent instances (id=1 + id=2) both connect. Each fires a separate
// Connected emission, and messageReceived routes with the right instanceId.
void WsServerMultiTest::testTwoInstancesRouteCorrectly()
{
    WsServer server;
    server.setReadyTimeoutMs(kReadyTimeoutMs);
    QVERIFY2(server.listen(0), "listen(0) failed");
    const quint16 port = server.serverPort();
    server.registerToken(1, QStringLiteral("tok1"));
    server.registerToken(2, QStringLiteral("tok2"));

    QWebSocket c1;
    QWebSocket c2;
    QSignalSpy stateSpy(&server, &WsServer::connectionStateChanged);
    QSignalSpy msgSpy(&server, &WsServer::messageReceived);
    QSignalSpy c1ConnectedSpy(&c1, &QWebSocket::connected);
    QSignalSpy c2ConnectedSpy(&c2, &QWebSocket::connected);
    QVERIFY(stateSpy.isValid());
    QVERIFY(msgSpy.isValid());

    c1.open(QUrl(urlFor(port, 1, QStringLiteral("tok1"))));
    c2.open(QUrl(urlFor(port, 2, QStringLiteral("tok2"))));

    // Both Connected emissions must arrive (each carries its own instanceId).
    QVERIFY2(stateSpy.wait(5000), "no state change within 5s");
    // Wait for the second Connected if it has not arrived yet.
    qint64 elapsed = 0;
    while (stateSpy.count() < 2 && elapsed < 5000) {
        QTest::qWait(100);
        elapsed += 100;
    }
    QCOMPARE(stateSpy.count(), 2);

    // Wait for BOTH client-side handshakes to complete so sendTextMessage
    // below writes into fully established sockets (sending before the client
    // handshake resolves yields 0 bytes — same gotcha as WsServerTest).
    if (c1ConnectedSpy.isEmpty())
        QVERIFY2(c1ConnectedSpy.wait(2000), "c1 never connected");
    if (c2ConnectedSpy.isEmpty())
        QVERIFY2(c2ConnectedSpy.wait(2000), "c2 never connected");

    // Identify which Connected emission belongs to which instance_id via the
    // ConnectionInfo arg. Both must be present.
    QSet<int> connectedIds;
    for (int i = 0; i < stateSpy.count(); ++i) {
        const auto st = stateSpy.at(i).at(0).value<WsConnectionState>();
        const auto info = stateSpy.at(i).at(1).value<ConnectionInfo>();
        if (st == WsConnectionState::Connected)
            connectedIds.insert(info.instanceId);
    }
    QVERIFY2(connectedIds.contains(1), "instance_id=1 Connected not observed");
    QVERIFY2(connectedIds.contains(2), "instance_id=2 Connected not observed");

    // Each client sends an event; the server must emit messageReceived with
    // the correct instanceId for each.
    QVERIFY2(c1.sendTextMessage(QString::fromUtf8(eventJson("click_left"))) > 0,
             "c1 sendTextMessage returned 0");
    QVERIFY2(c2.sendTextMessage(QString::fromUtf8(eventJson("click_right"))) > 0,
             "c2 sendTextMessage returned 0");

    // Wait for both messageReceived emissions.
    elapsed = 0;
    while (msgSpy.count() < 2 && elapsed < 5000) {
        QTest::qWait(50);
        elapsed += 50;
    }
    QCOMPARE(msgSpy.count(), 2);

    // Build a {instanceId → action} map and verify both routed correctly.
    QMap<int, QString> routed;
    for (int i = 0; i < msgSpy.count(); ++i) {
        const int id = msgSpy.at(i).at(0).toInt();
        const Envelope env = msgSpy.at(i).at(1).value<Envelope>();
        routed.insert(id, env.action);
    }
    QCOMPARE(routed.value(1), QStringLiteral("click_left"));
    QCOMPARE(routed.value(2), QStringLiteral("click_right"));

    c1.close();
    c2.close();
    server.close();
}

// sendText(2, json) reaches instance 2's socket only — instance 1's client
// receives nothing. Both clients are connected; only c2 should see the message.
void WsServerMultiTest::testSendTextRoutesToRightInstance()
{
    WsServer server;
    server.setReadyTimeoutMs(kReadyTimeoutMs);
    QVERIFY2(server.listen(0), "listen(0) failed");
    const quint16 port = server.serverPort();
    server.registerToken(1, QStringLiteral("tok1"));
    server.registerToken(2, QStringLiteral("tok2"));

    QWebSocket c1;
    QWebSocket c2;
    QSignalSpy serverStateSpy(&server, &WsServer::connectionStateChanged);
    QSignalSpy c1MsgSpy(&c1, &QWebSocket::textMessageReceived);
    QSignalSpy c2MsgSpy(&c2, &QWebSocket::textMessageReceived);

    c1.open(QUrl(urlFor(port, 1, QStringLiteral("tok1"))));
    c2.open(QUrl(urlFor(port, 2, QStringLiteral("tok2"))));
    qint64 elapsed = 0;
    while (serverStateSpy.count() < 2 && elapsed < 5000) {
        QTest::qWait(100);
        elapsed += 100;
    }
    QCOMPARE(serverStateSpy.count(), 2);

    // Send ONLY to instance 2.
    const QString json = QString::fromUtf8(eventJson("set_opacity"));
    QVERIFY2(server.sendText(2, json),
             "sendText(2, ...) returned false despite an active connection");

    // c2 must receive it within 2s.
    QVERIFY2(c2MsgSpy.wait(2000), "c2 did not receive the routed message");
    QCOMPARE(c2MsgSpy.count(), 1);

    // c1 must remain silent — give the loop a beat to flush any spurious
    // delivery, then assert no message arrived.
    QTest::qWait(200);
    QCOMPARE(c1MsgSpy.count(), 0);

    // sendText to an UNKNOWN instance returns false (no fallback to "any").
    QVERIFY2(!server.sendText(999, json),
             "sendText to unknown instance_id should return false");

    c1.close();
    c2.close();
    server.close();
}

// Disconnect instance 1 → instance 2's connection is unaffected. After the
// disconnect, sendText(2, ...) still succeeds and c2 still receives.
void WsServerMultiTest::testDisconnectLeavesOthersUntouched()
{
    WsServer server;
    server.setReadyTimeoutMs(kReadyTimeoutMs);
    QVERIFY2(server.listen(0), "listen(0) failed");
    const quint16 port = server.serverPort();
    server.registerToken(1, QStringLiteral("tok1"));
    server.registerToken(2, QStringLiteral("tok2"));

    QWebSocket c1;
    QWebSocket c2;
    QSignalSpy stateSpy(&server, &WsServer::connectionStateChanged);
    QSignalSpy c1DiscSpy(&c1, &QWebSocket::disconnected);
    QSignalSpy c2MsgSpy(&c2, &QWebSocket::textMessageReceived);

    c1.open(QUrl(urlFor(port, 1, QStringLiteral("tok1"))));
    c2.open(QUrl(urlFor(port, 2, QStringLiteral("tok2"))));
    qint64 elapsed = 0;
    while (stateSpy.count() < 2 && elapsed < 5000) {
        QTest::qWait(100);
        elapsed += 100;
    }
    QCOMPARE(stateSpy.count(), 2);

    // Close c1 — server must emit Disconnected for instance 1 only.
    c1.close();
    QVERIFY2(c1DiscSpy.wait(2000), "c1 disconnected signal not received");
    QTest::qWait(100); // let the server-side onDisconnected settle

    // Verify the server emitted a Disconnected for instance 1 — and NOT for 2.
    bool sawOneDisconnected = false;
    bool sawTwoDisconnected = false;
    for (int i = 0; i < stateSpy.count(); ++i) {
        const auto st = stateSpy.at(i).at(0).value<WsConnectionState>();
        const auto info = stateSpy.at(i).at(1).value<ConnectionInfo>();
        if (st == WsConnectionState::Disconnected) {
            if (info.instanceId == 1) sawOneDisconnected = true;
            if (info.instanceId == 2) sawTwoDisconnected = true;
        }
    }
    QVERIFY2(sawOneDisconnected, "server did not emit Disconnected for instance_id=1");
    QVERIFY2(!sawTwoDisconnected,
             "server emitted spurious Disconnected for instance_id=2 (collateral)");

    // Instance 2 must still be routable.
    const QString json = QString::fromUtf8(eventJson("set_volume"));
    QVERIFY2(server.sendText(2, json),
             "sendText(2, ...) failed after instance 1 disconnected");
    QVERIFY2(c2MsgSpy.wait(2000), "c2 did not receive after instance 1 disconnected");

    c2.close();
    server.close();
}

// A new connection for instance_id=1 with a WRONG token is rejected (4002).
// The existing good connection for instance_id=1 must be UNAFFECTED — a bad
// probe cannot evict a good active connection (gate c runs before replace).
void WsServerMultiTest::testWrongTokenDoesNotEvictGoodConnection()
{
    WsServer server;
    server.setReadyTimeoutMs(kReadyTimeoutMs);
    QVERIFY2(server.listen(0), "listen(0) failed");
    const quint16 port = server.serverPort();
    server.registerToken(1, QStringLiteral("good"));

    QWebSocket good;
    QSignalSpy serverStateSpy(&server, &WsServer::connectionStateChanged);
    QSignalSpy rejectSpy(&server, &WsServer::connectionRejected);
    QSignalSpy goodDiscSpy(&good, &QWebSocket::disconnected);

    good.open(QUrl(urlFor(port, 1, QStringLiteral("good"))));
    QVERIFY2(serverStateSpy.wait(5000), "good connection not accepted");
    QCOMPARE(serverStateSpy.count(), 1);

    // Now probe with a bad token for the SAME instance_id.
    QWebSocket bad;
    QSignalSpy badDiscSpy(&bad, &QWebSocket::disconnected);
    bad.open(QUrl(urlFor(port, 1, QStringLiteral("bad"))));

    QVERIFY2(badDiscSpy.wait(3000), "bad-token client not closed within 3s");
    QCOMPARE(bad.closeCode(), 4002);
    QCOMPARE(rejectSpy.count(), 1);
    QCOMPARE(rejectSpy.takeFirst().at(0).toInt(), 4002);

    // The GOOD connection must NOT have been dropped.
    QTest::qWait(200);
    QVERIFY2(goodDiscSpy.isEmpty(),
             "good connection was dropped by a bad-token probe (gate c leak)");

    // And it must still be routable.
    QWebSocket localReceiver;
    // Re-use the good client: send via server → assert good client receives.
    QSignalSpy goodMsgSpy(&good, &QWebSocket::textMessageReceived);
    const QString json = QString::fromUtf8(eventJson("set_layout"));
    QVERIFY2(server.sendText(1, json),
             "sendText(1, ...) failed after a bad-token probe");
    QVERIFY2(goodMsgSpy.wait(2000),
             "good client did not receive after a bad-token probe");

    good.close();
    server.close();
}

// A new connection for an existing instance_id with the CORRECT token closes
// the old socket (1000) and replaces it. The new one routes; the old one does
// not. Other instances are untouched.
void WsServerMultiTest::testReconnectClosesOldAndReplaces()
{
    WsServer server;
    server.setReadyTimeoutMs(kReadyTimeoutMs);
    QVERIFY2(server.listen(0), "listen(0) failed");
    const quint16 port = server.serverPort();
    server.registerToken(1, QStringLiteral("tok1"));

    QWebSocket c1;
    QSignalSpy serverStateSpy(&server, &WsServer::connectionStateChanged);
    QSignalSpy c1DiscSpy(&c1, &QWebSocket::disconnected);
    QSignalSpy c1ConnectedSpy(&c1, &QWebSocket::connected);

    c1.open(QUrl(urlFor(port, 1, QStringLiteral("tok1"))));
    QVERIFY2(serverStateSpy.wait(5000), "first connection not accepted");
    if (c1ConnectedSpy.isEmpty())
        QVERIFY2(c1ConnectedSpy.wait(2000), "first client never connected");

    // A second connection for the SAME instance_id with the CORRECT token.
    QWebSocket c2;
    QSignalSpy c2ConnectedSpy(&c2, &QWebSocket::connected);
    QSignalSpy c2MsgSpy(&c2, &QWebSocket::textMessageReceived);

    c2.open(QUrl(urlFor(port, 1, QStringLiteral("tok1"))));

    // c1 must be closed with 1000 (replaced).
    QVERIFY2(c1DiscSpy.wait(5000), "old connection not closed on replace");
    QCOMPARE(c1.closeCode(), 1000);
    if (c2ConnectedSpy.isEmpty())
        QVERIFY2(c2ConnectedSpy.wait(2000), "new client never connected");

    // The new connection (c2) must be the routable one. c1 is dead.
    QSignalSpy c1MsgSpy(&c1, &QWebSocket::textMessageReceived);
    const QString json = QString::fromUtf8(eventJson("set_fps"));
    QVERIFY2(server.sendText(1, json),
             "sendText(1, ...) failed after reconnect-replace");
    QVERIFY2(c2MsgSpy.wait(2000), "new client did not receive after replace");
    QTest::qWait(200);
    QCOMPARE(c1MsgSpy.count(), 0); // old socket got nothing

    c2.close();
    server.close();
}

QTEST_MAIN(WsServerMultiTest)
#include "WsServerMultiTest.moc"
