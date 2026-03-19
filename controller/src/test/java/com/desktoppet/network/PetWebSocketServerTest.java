package com.desktoppet.network;

import java.net.URI;
import java.net.URISyntaxException;
import java.net.ServerSocket;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.BiConsumer;
import org.java_websocket.client.WebSocketClient;
import org.java_websocket.handshake.ServerHandshake;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

class PetWebSocketServerTest {

    @Test
    void startAndStop_noCrash() {
        int port = findFreePort();
        PetWebSocketServer server = new PetWebSocketServer(port);

        assertDoesNotThrow(server::start);
        assertDoesNotThrow(() -> server.stop(1_000));
    }

    @Test
    void clientConnect_callsConnectionCallback() throws Exception {
        int port = findFreePort();
        PetWebSocketServer server = new PetWebSocketServer(port);
        CountDownLatch connectedLatch = new CountDownLatch(1);
        server.setConnectionCallback((instanceId, connected) -> {
            if (connected) {
                connectedLatch.countDown();
            }
        });

        TestWebSocketClient client = null;
        try {
            startServer(server);
            client = new TestWebSocketClient(port);
            assertTrue(client.connectBlocking(2, TimeUnit.SECONDS));
            assertTrue(connectedLatch.await(2, TimeUnit.SECONDS));
        } finally {
            closeClient(client);
            stopServer(server);
        }
    }

    @Test
    void clientDisconnect_callsConnectionCallback() throws Exception {
        int port = findFreePort();
        PetWebSocketServer server = new PetWebSocketServer(port);
        CountDownLatch disconnectedLatch = new CountDownLatch(1);
        server.setConnectionCallback((instanceId, connected) -> {
            if (!connected) {
                disconnectedLatch.countDown();
            }
        });

        TestWebSocketClient client = null;
        try {
            startServer(server);
            client = new TestWebSocketClient(port);
            assertTrue(client.connectBlocking(2, TimeUnit.SECONDS));
            client.closeBlocking();

            assertTrue(disconnectedLatch.await(2, TimeUnit.SECONDS));
        } finally {
            closeClient(client);
            stopServer(server);
        }
    }

    @Test
    void sendMessage_clientReceivesIt() throws Exception {
        int port = findFreePort();
        PetWebSocketServer server = new PetWebSocketServer(port);
        CountDownLatch connectedLatch = new CountDownLatch(1);
        server.setConnectionCallback((instanceId, connected) -> {
            if (connected) {
                connectedLatch.countDown();
            }
        });
        TestWebSocketClient client = null;

        try {
            startServer(server);
            client = new TestWebSocketClient(port);
            assertTrue(client.connectBlocking(2, TimeUnit.SECONDS));
            assertTrue(connectedLatch.await(2, TimeUnit.SECONDS));
            assertTrue(awaitActiveConnection(server, 2, TimeUnit.SECONDS));

            server.sendToInstance(1, "{\"type\":\"event\",\"action\":\"ready\"}");

            assertTrue(client.messageLatch.await(2, TimeUnit.SECONDS));
            assertEquals("{\"type\":\"event\",\"action\":\"ready\"}", client.lastMessage.get());
        } finally {
            closeClient(client);
            stopServer(server);
        }
    }

    @Test
    void clientSends_messageCallbackInvoked() throws Exception {
        int port = findFreePort();
        PetWebSocketServer server = new PetWebSocketServer(port);
        AtomicReference<String> captured = new AtomicReference<>();
        CountDownLatch messageLatch = new CountDownLatch(1);
        server.setMessageCallback((instanceId, message) -> {
            captured.set(message);
            messageLatch.countDown();
        });

        TestWebSocketClient client = null;
        String payload = "{\"type\":\"event\",\"action\":\"hit\",\"payload\":{\"area_id\":\"Head\"}}";

        try {
            startServer(server);
            client = new TestWebSocketClient(port);
            assertTrue(client.connectBlocking(2, TimeUnit.SECONDS));

            client.send(payload);

            assertTrue(messageLatch.await(2, TimeUnit.SECONDS));
            assertEquals(payload, captured.get());
        } finally {
            closeClient(client);
            stopServer(server);
        }
    }

    @Test
    void singleConnectionPolicy_newConnectionReplacesOld() throws Exception {
        int port = findFreePort();
        PetWebSocketServer server = new PetWebSocketServer(port);
        TestWebSocketClient client1 = null;
        TestWebSocketClient client2 = null;

        try {
            startServer(server);
            client1 = new TestWebSocketClient(port);
            assertTrue(client1.connectBlocking(2, TimeUnit.SECONDS));

            client2 = new TestWebSocketClient(port);
            assertTrue(client2.connectBlocking(2, TimeUnit.SECONDS));

            assertTrue(client1.closeLatch.await(2, TimeUnit.SECONDS));
            assertTrue(server.hasActiveConnection(1));
            assertTrue(client2.isOpen());

            server.sendToInstance(1, "second-client-only");
            assertTrue(client2.messageLatch.await(2, TimeUnit.SECONDS));
            assertEquals("second-client-only", client2.lastMessage.get());
        } finally {
            closeClient(client1);
            closeClient(client2);
            stopServer(server);
        }
    }

    private static int findFreePort() {
        try (ServerSocket socket = new ServerSocket(0)) {
            return socket.getLocalPort();
        } catch (Exception e) {
            throw new RuntimeException("Failed to find free port", e);
        }
    }

    private static void closeClient(TestWebSocketClient client) throws InterruptedException {
        if (client != null && client.isOpen()) {
            client.closeBlocking();
        }
    }

    private static void stopServer(PetWebSocketServer server) {
        if (server == null) {
            return;
        }
        try {
            server.stop(1_000);
        } catch (Exception ignored) {
        }
    }

    private static void startServer(PetWebSocketServer server) throws InterruptedException {
        server.start();
        Thread.sleep(50);
    }

    private static boolean awaitActiveConnection(PetWebSocketServer server, long timeout, TimeUnit unit)
        throws InterruptedException {
        long deadline = System.nanoTime() + unit.toNanos(timeout);
        while (System.nanoTime() < deadline) {
            if (server.hasActiveConnection(1)) {
                return true;
            }
            Thread.sleep(10);
        }
        return server.hasActiveConnection(1);
    }

    private static final class TestWebSocketClient extends WebSocketClient {
        private final CountDownLatch messageLatch = new CountDownLatch(1);
        private final CountDownLatch closeLatch = new CountDownLatch(1);
        private final AtomicReference<String> lastMessage = new AtomicReference<>();

        private TestWebSocketClient(int port) throws URISyntaxException {
            super(new URI("ws://127.0.0.1:" + port + "/?instance_id=1"));
        }

        @Override
        public void onOpen(ServerHandshake handshake) {
        }

        @Override
        public void onMessage(String message) {
            lastMessage.set(message);
            messageLatch.countDown();
        }

        @Override
        public void onClose(int code, String reason, boolean remote) {
            closeLatch.countDown();
        }

        @Override
        public void onError(Exception ex) {
        }
    }
}
