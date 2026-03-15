package com.desktoppet.network;

import java.net.InetSocketAddress;
import java.util.function.Consumer;
import org.java_websocket.WebSocket;
import org.java_websocket.handshake.ClientHandshake;
import org.java_websocket.server.WebSocketServer;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class PetWebSocketServer extends WebSocketServer {

    private static final Logger log = LoggerFactory.getLogger(PetWebSocketServer.class);

    private volatile WebSocket activeConnection;
    private volatile Consumer<String> messageCallback;
    private volatile Consumer<Boolean> connectionCallback;

    public PetWebSocketServer(int port) {
        super(new InetSocketAddress(port));
    }

    @Override
    public void onOpen(WebSocket conn, ClientHandshake handshake) {
        WebSocket previous = activeConnection;
        if (previous != null && previous.isOpen() && previous != conn) {
            previous.close(1000, "replaced by new connection");
        }

        activeConnection = conn;
        log.info("Renderer connected: {}", conn.getRemoteSocketAddress());

        Consumer<Boolean> callback = connectionCallback;
        if (callback != null) {
            callback.accept(true);
        }
    }

    @Override
    public void onMessage(WebSocket conn, String message) {
        Consumer<String> callback = messageCallback;
        if (callback != null) {
            callback.accept(message);
        }
    }

    @Override
    public void onClose(WebSocket conn, int code, String reason, boolean remote) {
        if (conn == activeConnection) {
            activeConnection = null;
            Consumer<Boolean> callback = connectionCallback;
            if (callback != null) {
                callback.accept(false);
            }
        }
        log.info("Renderer disconnected: code={}, reason={}, remote={}", code, reason, remote);
    }

    @Override
    public void onError(WebSocket conn, Exception ex) {
        log.error("WebSocket error: {}", ex.getMessage(), ex);
    }

    @Override
    public void onStart() {
        log.info("WebSocket server started on port {}", getPort());
    }

    public void sendMessage(String message) {
        WebSocket conn = activeConnection;
        if (conn != null && conn.isOpen()) {
            conn.send(message);
        }
    }

    public boolean hasActiveConnection() {
        WebSocket conn = activeConnection;
        return conn != null && conn.isOpen();
    }

    public void setMessageCallback(Consumer<String> callback) {
        this.messageCallback = callback;
    }

    public void setConnectionCallback(Consumer<Boolean> callback) {
        this.connectionCallback = callback;
    }
}
