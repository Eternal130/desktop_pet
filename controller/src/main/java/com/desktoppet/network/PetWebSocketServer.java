package com.desktoppet.network;

import java.net.InetSocketAddress;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.function.BiConsumer;
import org.java_websocket.WebSocket;
import org.java_websocket.handshake.ClientHandshake;
import org.java_websocket.server.WebSocketServer;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Shared WebSocket server for multiple renderer instances.
 * Renderers connect via: {@code ws://host:port/?instance_id=N}
 */
public class PetWebSocketServer extends WebSocketServer {

    private static final Logger log = LoggerFactory.getLogger(PetWebSocketServer.class);

    private final Map<Integer, WebSocket> instanceConnections = new ConcurrentHashMap<>();
    private final Map<WebSocket, Integer> connectionInstances = new ConcurrentHashMap<>();
    private final Map<Integer, String> expectedTokens = new ConcurrentHashMap<>();

    private volatile BiConsumer<Integer, Boolean> connectionCallback;
    private volatile BiConsumer<Integer, String> messageCallback;

    public PetWebSocketServer(int port) {
        super(new InetSocketAddress("127.0.0.1", port));
    }

    @Override
    public void onOpen(WebSocket conn, ClientHandshake handshake) {
        // Reject browser connections — DNS rebinding defense.
        // Browsers send Origin like "https://evil.com"; IXWebSocket sends "ws://host:port".
        String origin = handshake.getFieldValue("Origin");
        if (origin != null && (origin.startsWith("http://") || origin.startsWith("https://"))) {
            log.warn("Connection with browser Origin header rejected (possible DNS rebinding): {}", origin);
            conn.close(4001, "browser connections not allowed");
            return;
        }

        Integer instanceId = parseInstanceId(handshake.getResourceDescriptor());
        if (instanceId == null) {
            log.warn("Renderer connected without instance_id, closing: {}", conn.getRemoteSocketAddress());
            conn.close(4000, "missing instance_id query parameter");
            return;
        }

        String token = parseQueryParam(handshake.getResourceDescriptor(), "token");
        String expectedToken = expectedTokens.get(instanceId);
        if (expectedToken == null || !expectedToken.equals(token)) {
            log.warn("Renderer connected with invalid token for instance {}, closing", instanceId);
            conn.close(4002, "invalid or missing auth token");
            return;
        }

            WebSocket previous = instanceConnections.get(instanceId);
        if (previous != null && previous.isOpen() && previous != conn) {
            connectionInstances.remove(previous);
            previous.close(1000, "replaced by new connection");
        }

        instanceConnections.put(instanceId, conn);
        connectionInstances.put(conn, instanceId);
        log.info("Renderer connected: instance={}, remote={}", instanceId, conn.getRemoteSocketAddress());

        BiConsumer<Integer, Boolean> cb = connectionCallback;
        if (cb != null) {
            cb.accept(instanceId, true);
        }
    }

    @Override
    public void onMessage(WebSocket conn, String message) {
        Integer instanceId = connectionInstances.get(conn);
        BiConsumer<Integer, String> cb = messageCallback;
        if (instanceId != null && cb != null) {
            cb.accept(instanceId, message);
        }
    }

    @Override
    public void onClose(WebSocket conn, int code, String reason, boolean remote) {
        Integer instanceId = connectionInstances.remove(conn);
        if (instanceId != null) {
            instanceConnections.remove(instanceId, conn);
            BiConsumer<Integer, Boolean> cb = connectionCallback;
            if (cb != null) {
                cb.accept(instanceId, false);
            }
        }
        log.info("Renderer disconnected: instance={}, code={}, reason={}, remote={}",
                instanceId, code, reason, remote);
    }

    @Override
    public void onError(WebSocket conn, Exception ex) {
        Integer instanceId = conn != null ? connectionInstances.get(conn) : null;
        log.error("WebSocket error (instance={}): {}", instanceId, ex.getMessage(), ex);
    }

    @Override
    public void onStart() {
        log.info("WebSocket server started on port {}", getPort());
    }

    public void sendToInstance(int instanceId, String message) {
        WebSocket conn = instanceConnections.get(instanceId);
        if (conn != null && conn.isOpen()) {
            conn.send(message);
        }
    }

    public boolean hasActiveConnection(int instanceId) {
        WebSocket conn = instanceConnections.get(instanceId);
        return conn != null && conn.isOpen();
    }

    public void closeInstance(int instanceId) {
        WebSocket conn = instanceConnections.remove(instanceId);
        if (conn != null) {
            connectionInstances.remove(conn);
            if (conn.isOpen()) {
                conn.close(1000, "instance stopped");
            }
        }
    }

    public void setMessageCallback(BiConsumer<Integer, String> callback) {
        this.messageCallback = callback;
    }

    public void setConnectionCallback(BiConsumer<Integer, Boolean> callback) {
        this.connectionCallback = callback;
    }

    public void registerToken(int instanceId, String token) {
        expectedTokens.put(instanceId, token);
    }

    public void removeToken(int instanceId) {
        expectedTokens.remove(instanceId);
    }

    static Integer parseInstanceId(String resourceDescriptor) {
        String value = parseQueryParam(resourceDescriptor, "instance_id");
        if (value == null) return null;
        try {
            return Integer.parseInt(value);
        } catch (NumberFormatException e) {
            return null;
        }
    }

    static String parseQueryParam(String resourceDescriptor, String key) {
        if (resourceDescriptor == null) return null;
        int queryStart = resourceDescriptor.indexOf('?');
        if (queryStart < 0) return null;
        String query = resourceDescriptor.substring(queryStart + 1);
        for (String param : query.split("&")) {
            String[] kv = param.split("=", 2);
            if (kv.length == 2 && key.equals(kv[0])) {
                return kv[1];
            }
        }
        return null;
    }
}
