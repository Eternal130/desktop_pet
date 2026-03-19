package com.desktoppet.integration;

import com.desktoppet.core.InteractionHandler;
import com.desktoppet.network.MessageDispatcher;
import com.desktoppet.network.PetWebSocketServer;
import com.desktoppet.network.Protocol;
import com.google.gson.JsonObject;
import org.java_websocket.client.WebSocketClient;
import org.java_websocket.handshake.ServerHandshake;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

import java.net.ServerSocket;
import java.net.URI;
import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.function.BiConsumer;
import java.util.function.Consumer;

import static org.junit.jupiter.api.Assertions.*;

/**
 * E2E smoke test that validates the full protocol flow using a real PetWebSocketServer
 * and a Java WebSocket client simulating the renderer. No real renderer process is launched.
 */
public class E2ESmokeTest {

    private PetWebSocketServer server;
    private MessageDispatcher dispatcher;
    private int port;
    private TestRendererClient rendererClient;

    @BeforeEach
    void setUp() throws Exception {
        // Find a free port
        try (ServerSocket ss = new ServerSocket(0)) {
            port = ss.getLocalPort();
        }

        // Start WebSocket server
        server = new PetWebSocketServer(port);
        dispatcher = new MessageDispatcher();
        server.setMessageCallback((instanceId, msg) -> {
            Protocol.deserialize(msg).ifPresent(dispatcher::dispatch);
        });
        server.setReuseAddr(true);
        server.start();
        Thread.sleep(300); // Wait for server to start
    }

    @AfterEach
    void tearDown() throws Exception {
        if (rendererClient != null && rendererClient.isOpen()) {
            rendererClient.close();
            Thread.sleep(100);
        }
        server.stop(1000);
    }

    /**
     * Test 1: ready event → load_model command
     * Simulates renderer sending "ready" event and verifies server responds with "load_model" command.
     */
    @Test
    void protocolFlow_readyToLoadModel() throws Exception {
        List<String> receivedCommands = new CopyOnWriteArrayList<>();
        CountDownLatch loadModelLatch = new CountDownLatch(1);

        // Connect test client (simulating renderer)
        rendererClient = new TestRendererClient(
            new URI("ws://localhost:" + port + "/?instance_id=1"),
            msg -> {
                receivedCommands.add(msg);
                Protocol.deserialize(msg).ifPresent(env -> {
                    if ("load_model".equals(env.action())) {
                        loadModelLatch.countDown();
                    }
                });
            }
        );
        rendererClient.connectBlocking(3, TimeUnit.SECONDS);
        assertTrue(rendererClient.isOpen(), "Client should be connected");

        // Register ready handler on server side (simulating AppOrchestrator behavior)
        CountDownLatch readyLatch = new CountDownLatch(1);
        dispatcher.registerEventHandler("ready", envelope -> {
            readyLatch.countDown();
            // Send load_model command (like AppOrchestrator does)
            JsonObject payload = new JsonObject();
            payload.addProperty("model_path", "Hiyori");
            var cmd = Protocol.createCommand("load_model", payload);
            server.sendToInstance(1, Protocol.serialize(cmd));
        });

        // Simulate renderer sending ready event
        JsonObject readyPayload = new JsonObject();
        readyPayload.addProperty("version", "1.0.0");
        rendererClient.send(Protocol.serialize(Protocol.createEvent("ready", readyPayload)));

        // Verify ready was received and load_model was sent
        assertTrue(readyLatch.await(3, TimeUnit.SECONDS), "ready event should be received by server");
        assertTrue(loadModelLatch.await(3, TimeUnit.SECONDS), "load_model command should be sent to renderer");

        // Verify load_model command content
        boolean foundLoadModel = receivedCommands.stream()
            .anyMatch(msg -> msg.contains("load_model") && msg.contains("Hiyori"));
        assertTrue(foundLoadModel, "load_model command with model_path=Hiyori should be received by renderer");
    }

    /**
     * Test 2: hit event → play_motion command
     * Simulates renderer sending "hit" event and verifies server responds with "play_motion" command.
     */
    @Test
    void protocolFlow_hitEventTriggersPlayMotion() throws Exception {
        List<String> receivedCommands = new CopyOnWriteArrayList<>();
        CountDownLatch playMotionLatch = new CountDownLatch(1);

        rendererClient = new TestRendererClient(
            new URI("ws://localhost:" + port + "/?instance_id=1"),
            msg -> {
                receivedCommands.add(msg);
                Protocol.deserialize(msg).ifPresent(env -> {
                    if ("play_motion".equals(env.action())) {
                        playMotionLatch.countDown();
                    }
                });
            }
        );
        rendererClient.connectBlocking(3, TimeUnit.SECONDS);
        assertTrue(rendererClient.isOpen(), "Client should be connected");

        // Set up InteractionHandler on server side (simulating AppOrchestrator)
        InteractionHandler handler = new InteractionHandler(msg -> server.sendToInstance(1, msg));
        dispatcher.registerEventHandler("hit", handler::handleHitEvent);

        // Simulate renderer sending hit event on "head" area
        JsonObject hitPayload = new JsonObject();
        hitPayload.addProperty("area_id", "head");
        hitPayload.addProperty("x", 100.0);
        hitPayload.addProperty("y", 200.0);
        hitPayload.addProperty("button", 0);
        rendererClient.send(Protocol.serialize(Protocol.createEvent("hit", hitPayload)));

        // Verify play_motion was sent
        assertTrue(playMotionLatch.await(3, TimeUnit.SECONDS), "play_motion should be sent after hit event");

        // Verify play_motion command content (head → TapHead)
        boolean foundPlayMotion = receivedCommands.stream()
            .anyMatch(msg -> msg.contains("play_motion") && msg.contains("TapHead"));
        assertTrue(foundPlayMotion, "play_motion with TapHead group should be received by renderer");
    }

    /**
     * Test 3: Full flow - ready → load_model → response → set_position
     * Verifies the complete startup sequence including response handling.
     */
    @Test
    void protocolFlow_readyLoadModelResponseSetPosition() throws Exception {
        List<String> receivedCommands = new CopyOnWriteArrayList<>();
        CountDownLatch loadModelLatch = new CountDownLatch(1);
        CountDownLatch setPositionLatch = new CountDownLatch(1);

        rendererClient = new TestRendererClient(
            new URI("ws://localhost:" + port + "/?instance_id=1"),
            msg -> {
                receivedCommands.add(msg);
                Protocol.deserialize(msg).ifPresent(env -> {
                    if ("load_model".equals(env.action())) {
                        loadModelLatch.countDown();
                    } else if ("set_position".equals(env.action())) {
                        setPositionLatch.countDown();
                    }
                });
            }
        );
        rendererClient.connectBlocking(3, TimeUnit.SECONDS);
        assertTrue(rendererClient.isOpen(), "Client should be connected");

        // Register ready handler that sends load_model and expects a response
        dispatcher.registerEventHandler("ready", envelope -> {
            JsonObject payload = new JsonObject();
            payload.addProperty("model_path", "Hiyori");
            var cmd = Protocol.createCommand("load_model", payload);

            // Register response handler: on success, send set_position
            dispatcher.expectResponse(cmd.id(), java.time.Duration.ofSeconds(5))
                .thenAccept(response -> {
                    if (Boolean.TRUE.equals(response.success())) {
                        JsonObject posPayload = new JsonObject();
                        posPayload.addProperty("x", 100);
                        posPayload.addProperty("y", 200);
                        server.sendToInstance(1, Protocol.serialize(Protocol.createCommand("set_position", posPayload)));
                    }
                });

            server.sendToInstance(1, Protocol.serialize(cmd));
        });

        // Simulate renderer sending ready event
        JsonObject readyPayload = new JsonObject();
        readyPayload.addProperty("version", "1.0.0");
        rendererClient.send(Protocol.serialize(Protocol.createEvent("ready", readyPayload)));

        // Wait for load_model to arrive at renderer
        assertTrue(loadModelLatch.await(3, TimeUnit.SECONDS), "load_model command should be received");

        // Find the load_model command ID so we can respond to it
        String loadModelMsg = receivedCommands.stream()
            .filter(msg -> msg.contains("load_model"))
            .findFirst()
            .orElseThrow(() -> new AssertionError("load_model message not found"));

        var loadModelEnv = Protocol.deserialize(loadModelMsg).orElseThrow();

        // Simulate renderer sending success response to load_model
        var response = Protocol.createResponse(loadModelEnv.id(), "load_model", true, 0, "");
        rendererClient.send(Protocol.serialize(response));

        // Verify set_position was sent after successful load_model response
        assertTrue(setPositionLatch.await(3, TimeUnit.SECONDS), "set_position should be sent after load_model success");

        boolean foundSetPosition = receivedCommands.stream()
            .anyMatch(msg -> msg.contains("set_position"));
        assertTrue(foundSetPosition, "set_position command should be received by renderer");
    }

    /**
     * Simple test WebSocket client simulating the renderer.
     */
    static class TestRendererClient extends WebSocketClient {
        private final Consumer<String> onMessage;

        TestRendererClient(URI uri, Consumer<String> onMessage) {
            super(uri);
            this.onMessage = onMessage;
        }

        @Override
        public void onOpen(ServerHandshake h) {}

        @Override
        public void onMessage(String msg) {
            onMessage.accept(msg);
        }

        @Override
        public void onClose(int code, String reason, boolean remote) {}

        @Override
        public void onError(Exception ex) {}
    }
}
