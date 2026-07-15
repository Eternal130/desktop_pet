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
//   1000 = normal close (incl. replaced by a newer connection)
//   4001 = Origin rejected (browser connection)

WsServer::WsServer(QObject* parent)
    : QObject(parent)
    , m_server(new QWebSocketServer(QStringLiteral("desktop-pet-controller"),
                                    QWebSocketServer::NonSecureMode, this))
{
    connect(m_server, &QWebSocketServer::newConnection, this, &WsServer::onNewConnection);
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
    if (m_activeConnection) {
        m_activeConnection->close(QWebSocketProtocol::CloseCodeNormal,
                                  QStringLiteral("server shutdown"));
        m_activeConnection->deleteLater();
        m_activeConnection = nullptr;
    }
    if (m_server && m_server->isListening()) {
        m_server->close();
        LOG_INFO("WsServer: stopped listening");
    }
}

quint16 WsServer::serverPort() const
{
    return m_server ? m_server->serverPort() : 0;
}

bool WsServer::sendText(const QString& text)
{
    if (!m_activeConnection) {
        LOG_WARN("WsServer: sendText with no active connection ({} bytes dropped)",
                 text.size());
        return false;
    }
    const qint64 written = m_activeConnection->sendTextMessage(text);
    LOG_DEBUG("WsServer: sent {} bytes (queued {})", text.size(), written);
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

void WsServer::onNewConnection()
{
    QWebSocket* socket = m_server->nextPendingConnection();
    if (!socket)
        return;

    // Origin guard first (blueprint §3.3 gate a): reject browser origins.
    const QString origin = socket->origin();
    if (!isOriginAcceptable(origin)) {
        LOG_WARN("WsServer: rejecting connection, origin='{}'", origin.toStdString());
        // 4001 is an application-defined close code (not in the standard enum),
        // so it must be cast into QWebSocketProtocol::CloseCode.
        socket->close(static_cast<QWebSocketProtocol::CloseCode>(4001),
                      QStringLiteral("origin rejected"));
        emit connectionRejected(4001, QStringLiteral("origin rejected"));
        // Schedule deletion once the close handshake completes the disconnect,
        // so the 4001 Close frame is actually sent before teardown.
        connect(socket, &QWebSocket::disconnected, socket, &QWebSocket::deleteLater);
        return;
    }

    // Single-connection management: replace any existing connection (close 1000).
    if (m_activeConnection) {
        LOG_INFO("WsServer: replacing existing connection (close 1000)");
        m_activeConnection->close(QWebSocketProtocol::CloseCodeNormal,
                                  QStringLiteral("replaced"));
        m_activeConnection->deleteLater();
        m_activeConnection = nullptr;
    }

    // Query-param extraction (instance_id existence + token match is T8).
    ConnectionInfo info;
    parseQueryParams(socket, info);
    m_lastConnectionInfo = info;

    connect(socket, &QWebSocket::textMessageReceived, this, &WsServer::onTextMessageReceived);
    connect(socket, &QWebSocket::disconnected, this, &WsServer::onDisconnected);
    connect(socket, &QWebSocket::disconnected, socket, &QWebSocket::deleteLater);

    m_activeConnection = socket;
    LOG_INFO("WsServer: connection accepted (instance_id={}, token_len={})",
             info.instanceId, info.token.size());
    emit connectionStateChanged(WsConnectionState::Connected, info);
}

void WsServer::onTextMessageReceived(const QString& text)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        // §2.4: invalid JSON is silently dropped (logged at WARN, no exception).
        LOG_WARN("WsServer: dropped non-JSON text frame: {}",
                 parseError.errorString().toStdString());
        return;
    }
    const auto opt = deserialize(doc);
    if (!opt.has_value()) {
        LOG_WARN("WsServer: dropped invalid envelope");
        return;
    }
    emit messageReceived(*opt);
}

void WsServer::onDisconnected()
{
    auto* socket = qobject_cast<QWebSocket*>(sender());
    if (socket && socket == m_activeConnection)
        m_activeConnection = nullptr;
    LOG_INFO("WsServer: connection disconnected");
    emit connectionStateChanged(WsConnectionState::Disconnected, m_lastConnectionInfo);
}
