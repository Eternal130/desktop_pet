#pragma once

#include <QMap>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QWebSocket>
#include <QWebSocketServer>
#include "network/Envelope.hpp"

// Connection states emitted by connectionStateChanged.
enum class WsConnectionState {
    Disconnected,
    Connected,
};

// Connection info extracted from the WS upgrade request query string.
//
// rawResourceName is the verbatim value of QWebSocket::resourceName() at the
// time the connection was accepted. It is exposed so the T5 R1 de-risk test can
// assert directly that Qt surfaces the query string the renderer sends (the
// whole point of this task). parsed instance_id/token are derived from it.
struct ConnectionInfo {
    int instanceId = -1;         // parsed from ?instance_id=N
    QString token;               // parsed from &token=<hex>
    bool hasQueryParams = false; // true if resourceName() contained instance_id= AND token=
    QString rawResourceName;     // verbatim QWebSocket::resourceName() — R1 observability
};

// WebSocket Server (T5 + T8). Binds 127.0.0.1 only (NEVER 0.0.0.0), parses the
// connection query string for instance_id/token via QWebSocket::resourceName(),
// and enforces the full 3-gate token handshake (blueprint §3.3):
//   gate a — Origin guard  : browser-style http(s):// origins → close 4001
//   gate b — instance_id   : missing / non-integer instance_id → close 4000
//   gate c — token         : token != registered token for instance_id → close 4002
// A connection is accepted only after all three gates pass. Tokens are
// pre-registered via registerToken() BEFORE the renderer process starts (avoids
// a race where the renderer connects before the token is in the map).
//
// After acceptance a 10s ready-timeout (handshake.md §4) starts; if the `ready`
// event is not received the connection is closed. A single active connection is
// maintained — a new validated connection replaces the old (close 1000).
// Close codes (interface.md appendix):
//   1000 = normal close (incl. replaced by a newer connection)
//   4000 = missing / non-integer instance_id query param
//   4001 = Origin rejected (browser connection)
//   4002 = token does not match the registered token
class WsServer : public QObject {
    Q_OBJECT
public:
    explicit WsServer(QObject* parent = nullptr);
    ~WsServer() override;

    // Start listening on 127.0.0.1:<port>. Returns true on success. Pass 0 to
    // let the OS auto-assign a free port (used by tests); read serverPort()
    // afterwards for the actual port.
    bool listen(quint16 port = 9001);

    // Stop listening and close any active connection (close code 1000).
    void close();

    // The port currently being listened on, or 0 when not listening.
    quint16 serverPort() const;

    // Send a text frame to the active connection. Returns true if a connection
    // is active and the bytes were queued for send, false if no connection is
    // active. The PoC (T6) and later command dispatchers use this to push
    // command envelopes to the renderer. Forwarded to
    // QWebSocket::sendTextMessage on m_activeConnection.
    bool sendText(const QString& text);

    // Token registry (T8). Register a token for an instance id BEFORE the
    // renderer process starts — gate c rejects any connection whose token does
    // not match the registered value for its instance_id. removeToken clears an
    // entry (e.g. on instance teardown).
    void registerToken(int instanceId, const QString& token);
    void removeToken(int instanceId);

    // Test hook: override the ready-timeout duration (default 10000ms per
    // handshake.md §4). Tests pass a short value (e.g. 200) so the
    // ready-timeout case runs fast instead of blocking 10s. Production code
    // never calls this.
    void setReadyTimeoutMs(int ms);

signals:
    // Emitted when a text frame is received and successfully parsed into an
    // Envelope. Invalid JSON / invalid envelopes are silently dropped (§2.4).
    void messageReceived(const Envelope& env);

    // Emitted when the connection state changes (connected/disconnected).
    void connectionStateChanged(WsConnectionState state, const ConnectionInfo& info);

    // Emitted when a connection is rejected (Origin guard, etc.) with the close
    // code and human-readable reason.
    void connectionRejected(int closeCode, const QString& reason);

private slots:
    void onNewConnection();
    void onTextMessageReceived(const QString& text);
    void onDisconnected();

private:
    QWebSocketServer* m_server;
    QWebSocket* m_activeConnection = nullptr; // single active connection
    ConnectionInfo m_lastConnectionInfo;

    // Token registry (T8 gate c): instance_id → expected token. Populated via
    // registerToken() before the renderer starts.
    QMap<int, QString> m_tokens;

    // Ready-timeout (handshake.md §4). Single-shot; (re)started whenever a
    // connection is accepted, stopped on `ready` / disconnect / server close.
    // Fires → close the connection (renderer never said ready).
    QTimer* m_readyTimer;
    int m_readyTimeoutMs = 10000;

    // Parse the query string from QWebSocket::resourceName(). Returns true if
    // instance_id and token were both found. The raw resource name is copied to
    // outInfo.rawResourceName regardless, for R1 observability.
    bool parseQueryParams(QWebSocket* socket, ConnectionInfo& outInfo);

    // Origin guard: returns true if the origin is acceptable (empty or
    // non-http). Returns false for any origin starting with http:// or https://
    // (per blueprint §3.3 — blocks DNS-rebinding / browser injection).
    static bool isOriginAcceptable(const QString& origin);
};

// Metatype registration so QSignalSpy / queued connections can carry these.
Q_DECLARE_METATYPE(WsConnectionState)
Q_DECLARE_METATYPE(ConnectionInfo)
