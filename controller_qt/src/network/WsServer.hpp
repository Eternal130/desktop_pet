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

// WebSocket Server (T5 + T8 + Phase 5 todo 11). Binds 127.0.0.1 only (NEVER
// 0.0.0.0), parses the connection query string for instance_id/token via
// QWebSocket::resourceName(), and enforces the full 3-gate token handshake
// (blueprint §3.3):
//   gate a — Origin guard  : browser-style http(s):// origins → close 4001
//   gate b — instance_id   : missing / non-integer instance_id → close 4000
//   gate c — token         : token != registered token for instance_id → close 4002
// A connection is accepted only after all three gates pass. Tokens are
// pre-registered via registerToken() BEFORE the renderer process starts (avoids
// a race where the renderer connects before the token is in the map).
//
// Multi-instance routing (Phase 5, todo 11): N concurrent connections are
// supported, one per instance_id. Each connection is keyed by its parsed
// ?instance_id=N query param in m_connections. A new validated connection for
// an already-connected instance_id replaces the old (close 1000) — instance_id
// is the routing identity, not the TCP socket. Each connection has its own
// ready-timer (handshake.md §4) — a slow/stuck instance never blocks another.
//
// After acceptance a 10s ready-timeout starts; if the `ready` event is not
// received that connection is closed (only the offending instance_id is
// affected; other connections stay live).
//
// Close codes (interface.md appendix):
//   1000 = normal close (incl. replaced by a newer connection for the same
//          instance_id, and server shutdown)
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

    // Stop listening and close every active connection (close code 1000).
    // All per-instance ready-timers are stopped + freed.
    void close();

    // The port currently being listened on, or 0 when not listening.
    quint16 serverPort() const;

    // Send a text frame to the connection registered for instanceId. Returns
    // true if a connection is active for that instance_id and the bytes were
    // queued for send, false if no connection is registered for instance_id
    // (logged at WARN — callers should not routinely send to dead instances).
    // Per-instance routing (Phase 5, todo 11): each instance's renderer
    // connects with its own ?instance_id=N query param and gets its own slot
    // in the m_connections map; this routes the outbound frame to the right
    // socket. There is NO fallback "any connection" send — callers MUST pass
    // the explicit instanceId (InstanceSession knows its m_instanceId).
    bool sendText(int instanceId, const QString& text);

    // Token registry (T8 gate c). Register a token for an instance id BEFORE the
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
    // Envelope. The instanceId is the parsed ?instance_id=N of the connection
    // that delivered the frame (Phase 5, todo 11: multi-instance routing).
    // Invalid JSON / invalid envelopes are silently dropped (§2.4).
    void messageReceived(int instanceId, const Envelope& env);

    // Emitted when a connection's state changes. The ConnectionInfo carries the
    // instanceId that the state change applies to (Phase 5, todo 11: per-
    // instance demux; InstanceSession filters on info.instanceId == m_instanceId).
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

    // Per-instance connection table (Phase 5, todo 11: multi-instance routing).
    // Keyed by the parsed ?instance_id=N. A new validated connection for an
    // already-connected instance_id closes the old socket (close 1000) and
    // overwrites the entry — instance_id is the routing identity.
    QMap<int, QWebSocket*> m_connections;

    // Per-instance last-known ConnectionInfo — emitted with Disconnected so
    // InstanceSession can identify WHICH instance dropped (it cannot derive
    // instanceId from the bare signal arg).
    QMap<int, ConnectionInfo> m_lastConnectionInfos;

    // Per-instance ready-timers (handshake.md §4). Each connection has its own
    // single-shot QTimer; a slow/stuck instance never blocks another's ready-
    // timeout. Stopped on `ready`, cleared on disconnect/replace/server-close.
    QMap<int, QTimer*> m_readyTimers;

    // Token registry (T8 gate c): instance_id → expected token. Populated via
    // registerToken() before the renderer starts.
    QMap<int, QString> m_tokens;

    int m_readyTimeoutMs = 10000;

    // Parse the query string from QWebSocket::resourceName(). Returns true if
    // instance_id and token were both found. The raw resource name is copied to
    // outInfo.rawResourceName regardless, for R1 observability.
    bool parseQueryParams(QWebSocket* socket, ConnectionInfo& outInfo);

    // Origin guard: returns true if the origin is acceptable (empty or
    // non-http). Returns false for any origin starting with http:// or https://
    // (per blueprint §3.3 — blocks DNS-rebinding / browser injection).
    static bool isOriginAcceptable(const QString& origin);

    // Reverse lookup: find the instanceId registered for a given socket. Used
    // in onTextMessageReceived + onDisconnected where Qt's sender() yields the
    // QWebSocket* but we need the routing key. Returns -1 when the socket is
    // no longer in m_connections (e.g. already-removed replaced socket whose
    // close handshake is just now completing). O(N) but N is sidebar-sized
    // (typically <5 instances), so a linear scan is cheaper than maintaining a
    // second QMap<QWebSocket*, int> in lock-step with m_connections.
    int findInstanceIdForSocket(QWebSocket* socket) const;
};

// Metatype registration so QSignalSpy / queued connections can carry these.
Q_DECLARE_METATYPE(WsConnectionState)
Q_DECLARE_METATYPE(ConnectionInfo)
