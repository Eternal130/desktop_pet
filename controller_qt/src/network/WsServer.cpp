#include "network/WsServer.hpp"

#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLatin1String>
#include <QUrl>
#include <QUrlQuery>
#include <QWebSocketProtocol>

// Logging.hpp defines LOG_* macros that expand to SPDLOG_* macros, but does
// not itself include spdlog headers — every TU that uses LOG_* must pull in
// spdlog/spdlog.h (same as Logging.cpp does).
#include <spdlog/spdlog.h>

#include "logging/Logging.hpp"

// Close codes (interface.md appendix, blueprint §3.3):
//   1000 = normal close (incl. replaced by a newer connection for the same
//          instance_id, and server shutdown)
//   4001 = Origin rejected (browser connection)

WsServer::WsServer(QObject* parent)
    : QObject(parent)
    , m_server(new QWebSocketServer(QStringLiteral("desktop-pet-controller"),
                                     QWebSocketServer::NonSecureMode, this))
{
    connect(m_server, &QWebSocketServer::newConnection, this, &WsServer::onNewConnection);
    // Per-instance ready-timers are constructed lazily in onNewConnection
    // (handshake.md §4). Single-shot; (re)armed on accept, stopped on `ready`
    // (onTextMessageReceived) / disconnect / server close.
}

WsServer::~WsServer()
{
    close();
}

bool WsServer::listen(quint16 port)
{
    // LocalHost ONLY — never QHostAddress::Any / 0.0.0.0 (blueprint §3.1).
    if (!m_server->listen(QHostAddress::LocalHost, port)) {
        LOG_ERROR("WsServer: failed to listen on 127.0.0.1:{} — {}",
                  port, m_server->errorString().toStdString());
        return false;
    }
    LOG_INFO("WsServer: listening on 127.0.0.1:{}", m_server->serverPort());
    return true;
}

void WsServer::close()
{
    // Stop + free every per-instance ready-timer first so no timeout can fire
    // while we are tearing down sockets below.
    for (QTimer* t : std::as_const(m_readyTimers)) {
        t->stop();
        t->deleteLater();
    }
    m_readyTimers.clear();

    // Close every active connection with 1000. The acceptance-time
    // `disconnected → deleteLater` connection cleans each socket up after the
    // close handshake completes. We do NOT emit Disconnected here: a server
    // shutdown is not an instance-specific state change and InstanceManager
    // tears its sessions down explicitly via InstanceSession::stop().
    for (QWebSocket* socket : std::as_const(m_connections)) {
        if (socket) {
            socket->close(QWebSocketProtocol::CloseCodeNormal,
                          QStringLiteral("server shutdown"));
        }
    }
    m_connections.clear();
    m_lastConnectionInfos.clear();

    if (m_server && m_server->isListening()) {
        m_server->close();
        LOG_INFO("WsServer: stopped listening");
    }
}

quint16 WsServer::serverPort() const
{
    return m_server ? m_server->serverPort() : 0;
}

void WsServer::registerToken(int instanceId, const QString& token)
{
    m_tokens.insert(instanceId, token);
    LOG_INFO("WsServer: registered token for instance_id={} ({} hex chars)",
             instanceId, token.size());
}

void WsServer::removeToken(int instanceId)
{
    m_tokens.remove(instanceId);
}

void WsServer::setReadyTimeoutMs(int ms)
{
    m_readyTimeoutMs = ms;
}

bool WsServer::sendText(int instanceId, const QString& text)
{
    const auto it = m_connections.constFind(instanceId);
    if (it == m_connections.constEnd() || !*it) {
        LOG_WARN("WsServer: sendText with no active connection for instance_id={} "
                 "({} bytes dropped)", instanceId, text.size());
        return false;
    }
    const qint64 written = (*it)->sendTextMessage(text);
    LOG_DEBUG("WsServer: sent {} bytes to instance_id={} (queued {})",
              text.size(), instanceId, written);
    return written > 0;
}

bool WsServer::isOriginAcceptable(const QString& origin)
{
    // Empty origin = non-browser client (the renderer) — acceptable. Only
    // browser-style http(s):// origins are rejected (DNS-rebinding guard).
    if (origin.isEmpty())
        return true;
    if (origin.startsWith(QLatin1String("http://"), Qt::CaseInsensitive))
        return false;
    if (origin.startsWith(QLatin1String("https://"), Qt::CaseInsensitive))
        return false;
    return true;
}

bool WsServer::parseQueryParams(QWebSocket* socket, ConnectionInfo& outInfo)
{
    // R1 de-risk: resourceName() returns the HTTP upgrade request target. On Qt
    // 6.10 it is the FULL request URL ("ws://127.0.0.1:port/?instance_id=0&token=…")
    // rather than just the relative path ("/?…"); either form is handled by
    // splitting on '?' so the scheme/host shape never matters. The raw value is
    // stored for test observability regardless of parse success.
    const QString resource = socket->resourceName();
    outInfo.rawResourceName = resource;
    LOG_INFO("WsServer: resourceName() = '{}'", resource.toStdString());

    if (!resource.contains(QLatin1String("instance_id="))
        || !resource.contains(QLatin1String("token="))) {
        outInfo.hasQueryParams = false;
        LOG_WARN("WsServer: resourceName() missing instance_id=/token= query params");
        return false;
    }

    // Extract the substring after the first '?', then let QUrlQuery split it
    // into key=value pairs (handles '+' / '%xx' / '&' decoding).
    const int qMark = resource.indexOf(QLatin1Char('?'));
    const QString queryString = (qMark >= 0) ? resource.mid(qMark + 1) : QString{};
    const QUrlQuery query(queryString);
    bool foundInstance = false;
    bool foundToken = false;
    for (const auto& item : query.queryItems()) {
        if (item.first == QLatin1String("instance_id")) {
            bool ok = false;
            outInfo.instanceId = item.second.toInt(&ok);
            foundInstance = ok;
        } else if (item.first == QLatin1String("token")) {
            outInfo.token = item.second;
            foundToken = true;
        }
    }
    outInfo.hasQueryParams = foundInstance && foundToken;
    if (!outInfo.hasQueryParams) {
        LOG_WARN("WsServer: query params present but instance_id/token unparseable");
    }
    return outInfo.hasQueryParams;
}

int WsServer::findInstanceIdForSocket(QWebSocket* socket) const
{
    for (auto it = m_connections.constBegin(); it != m_connections.constEnd(); ++it) {
        if (it.value() == socket)
            return it.key();
    }
    return -1;
}

void WsServer::onNewConnection()
{
    QWebSocket* socket = m_server->nextPendingConnection();
    if (!socket)
        return;

    // Gate a — Origin guard (blueprint §3.3): reject browser origins first so a
    // browser probe can never reach the instance_id/token checks.
    const QString origin = socket->origin();
    if (!isOriginAcceptable(origin)) {
        LOG_WARN("WsServer: rejecting connection (gate a), origin='{}'",
                 origin.toStdString());
        socket->close(static_cast<QWebSocketProtocol::CloseCode>(4001),
                      QStringLiteral("origin rejected"));
        emit connectionRejected(4001, QStringLiteral("origin rejected"));
        // Delete once the close handshake completes so the 4001 frame is sent.
        connect(socket, &QWebSocket::disconnected, socket, &QWebSocket::deleteLater);
        return;
    }

    // Parse query params for gates b + c.
    ConnectionInfo info;
    parseQueryParams(socket, info);

    // Gate b — instance_id must be present and parse as an integer.
    if (!info.hasQueryParams || info.instanceId < 0) {
        LOG_WARN("WsServer: rejecting connection (gate b): invalid instance_id "
                 "(hasQueryParams={}, instanceId={})",
                 info.hasQueryParams, info.instanceId);
        socket->close(static_cast<QWebSocketProtocol::CloseCode>(4000),
                      QStringLiteral("invalid instance_id"));
        emit connectionRejected(4000, QStringLiteral("invalid instance_id"));
        connect(socket, &QWebSocket::disconnected, socket, &QWebSocket::deleteLater);
        return;
    }

    // Gate c — token must match the registered token for this instance_id.
    const auto it = m_tokens.constFind(info.instanceId);
    const bool registered = (it != m_tokens.constEnd());
    const bool tokenOk = registered && (*it == info.token);
    if (!tokenOk) {
        LOG_WARN("WsServer: rejecting connection (gate c): token mismatch "
                 "(instance_id={}, registered={})", info.instanceId, registered);
        socket->close(static_cast<QWebSocketProtocol::CloseCode>(4002),
                      QStringLiteral("token mismatch"));
        emit connectionRejected(4002, QStringLiteral("token mismatch"));
        connect(socket, &QWebSocket::disconnected, socket, &QWebSocket::deleteLater);
        return;
    }

    // All 3 gates passed. Phase 5 todo 11: replace any existing connection FOR
    // THIS instance_id (close 1000). A rejected connection above never reaches
    // here, so a bad probe cannot evict a good active connection.
    //
    // We close() but do NOT deleteLater() here: the old socket's acceptance-time
    // `disconnected → deleteLater` connection cleans it up AFTER the close
    // handshake completes, so the peer actually receives the 1000 Close frame
    // (destroying the socket too early yields an abnormal 1005 close).
    //
    // CRITICAL: we overwrite m_connections[instanceId] = socket BEFORE the old
    // socket's disconnected signal fires. So onDisconnected's reverse lookup
    // (findInstanceIdForSocket) returns -1 for the old socket → it is treated
    // as a replaced/rejected socket, NOT an active disconnect → no spurious
    // Disconnected emission that would clobber the just-emitted Connected.
    if (const auto existingIt = m_connections.find(info.instanceId);
        existingIt != m_connections.end() && existingIt.value()) {
        LOG_INFO("WsServer: replacing existing connection for instance_id={} (close 1000)",
                 info.instanceId);
        existingIt.value()->close(QWebSocketProtocol::CloseCodeNormal,
                                  QStringLiteral("replaced"));
        // Stop the old ready-timer — a new one is created below for the new
        // socket. The QTimer object is reused (one per instance_id at a time).
        if (const auto ti = m_readyTimers.find(info.instanceId);
            ti != m_readyTimers.end() && ti.value()) {
            ti.value()->stop();
        }
    }

    connect(socket, &QWebSocket::textMessageReceived, this, &WsServer::onTextMessageReceived);
    connect(socket, &QWebSocket::disconnected, this, &WsServer::onDisconnected);
    connect(socket, &QWebSocket::disconnected, socket, &QWebSocket::deleteLater);

    m_connections.insert(info.instanceId, socket);
    m_lastConnectionInfos.insert(info.instanceId, info);

    // Per-instance ready-timer (handshake.md §4). Reuse the existing QTimer if
    // one is already mapped for this instance_id (replace case); otherwise
    // create a fresh one parented to this WsServer.
    QTimer* readyTimer = m_readyTimers.value(info.instanceId, nullptr);
    if (!readyTimer) {
        readyTimer = new QTimer(this);
        readyTimer->setSingleShot(true);
        const int instanceId = info.instanceId;
        connect(readyTimer, &QTimer::timeout, this, [this, instanceId]() {
            LOG_ERROR("WsServer: ready timeout for instance_id={} (no ready event within {}ms)",
                      instanceId, m_readyTimeoutMs);
            // Close only THIS instance's connection. Other instances stay live.
            if (const auto cit = m_connections.find(instanceId);
                cit != m_connections.end() && cit.value()) {
                cit.value()->close(QWebSocketProtocol::CloseCodeNormal,
                                   QStringLiteral("ready timeout"));
                // deleteLater is wired via disconnected → deleteLater (installed at accept).
                m_connections.erase(cit);
            }
            if (const auto ti = m_readyTimers.find(instanceId); ti != m_readyTimers.end()) {
                ti.value()->stop();
                ti.value()->deleteLater();
                m_readyTimers.erase(ti);
            }
            if (const auto infoIt = m_lastConnectionInfos.find(instanceId);
                infoIt != m_lastConnectionInfos.end()) {
                emit connectionStateChanged(WsConnectionState::Disconnected, infoIt.value());
                m_lastConnectionInfos.erase(infoIt);
            }
        });
        m_readyTimers.insert(info.instanceId, readyTimer);
    }

    LOG_INFO("WsServer: connection accepted (instance_id={}, token_len={}, total={})",
             info.instanceId, info.token.size(), m_connections.size());
    emit connectionStateChanged(WsConnectionState::Connected, info);

    // Start the ready-timeout (handshake.md §4): the renderer must send `ready`
    // within m_readyTimeoutMs or this connection is closed by the timer.
    readyTimer->start(m_readyTimeoutMs);
}

void WsServer::onTextMessageReceived(const QString& text)
{
    auto* socket = qobject_cast<QWebSocket*>(sender());
    if (!socket)
        return;
    const int instanceId = findInstanceIdForSocket(socket);
    if (instanceId < 0) {
        // Replaced or already-removed socket whose close handshake is just now
        // completing — drop the frame (no routing key, no emission).
        LOG_DEBUG("WsServer: text from non-active socket — dropped ({} bytes)", text.size());
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        // §2.4: invalid JSON is silently dropped (logged at WARN, no exception).
        LOG_WARN("WsServer: dropped non-JSON text frame from instance_id={}: {}",
                 instanceId, parseError.errorString().toStdString());
        return;
    }
    const auto opt = deserialize(doc);
    if (!opt.has_value()) {
        LOG_WARN("WsServer: dropped invalid envelope from instance_id={}", instanceId);
        return;
    }
    // handshake.md §3.1: the renderer sends `ready` immediately after the WS
    // upgrade. Receiving it stops the per-instance ready-timeout.
    if (opt->type == QLatin1String("event")
        && opt->action == QLatin1String("ready")) {
        if (const auto ti = m_readyTimers.find(instanceId);
            ti != m_readyTimers.end() && ti.value()->isActive()) {
            ti.value()->stop();
            LOG_INFO("WsServer: ready received for instance_id={}, ready-timeout stopped",
                     instanceId);
        }
    }
    emit messageReceived(instanceId, *opt);
}

void WsServer::onDisconnected()
{
    auto* socket = qobject_cast<QWebSocket*>(sender());
    if (!socket)
        return;
    const int instanceId = findInstanceIdForSocket(socket);
    if (instanceId < 0) {
        // Replaced or rejected socket — already absent from m_connections.
        // The replacement already emitted Connected (or the rejection emitted
        // connectionRejected) at accept time, so emitting Disconnected here
        // would clobber that signal. HandshakeTest's replace case locks this.
        LOG_DEBUG("WsServer: non-active socket disconnected (replaced or rejected)");
        return;
    }
    // Active connection for THIS instance_id disconnected — remove from all
    // per-instance maps, stop + free its ready-timer, emit Disconnected.
    m_connections.remove(instanceId);
    if (const auto ti = m_readyTimers.find(instanceId); ti != m_readyTimers.end()) {
        ti.value()->stop();
        ti.value()->deleteLater();
        m_readyTimers.erase(ti);
    }
    if (const auto infoIt = m_lastConnectionInfos.find(instanceId);
        infoIt != m_lastConnectionInfos.end()) {
        LOG_INFO("WsServer: connection disconnected for instance_id={} (remaining={})",
                 instanceId, m_connections.size());
        emit connectionStateChanged(WsConnectionState::Disconnected, infoIt.value());
        m_lastConnectionInfos.erase(infoIt);
    } else {
        // No ConnectionInfo recorded (should not happen — onNewConnection
        // always inserts before emitting Connected). Emit a minimal info so
        // InstanceSession's filter on info.instanceId still works.
        ConnectionInfo fallback;
        fallback.instanceId = instanceId;
        LOG_INFO("WsServer: connection disconnected for instance_id={} (no info, remaining={})",
                 instanceId, m_connections.size());
        emit connectionStateChanged(WsConnectionState::Disconnected, fallback);
    }
}
