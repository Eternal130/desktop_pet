package com.desktoppet.ui;

import com.desktoppet.model.PetInstance;
import com.desktoppet.network.PetWebSocketServer;
import javafx.scene.control.Button;
import javafx.scene.control.CheckBox;
import javafx.scene.control.Label;
import org.junit.jupiter.api.Test;
import org.testfx.framework.junit5.ApplicationTest;
import javafx.stage.Stage;
import javafx.fxml.FXMLLoader;
import javafx.scene.Scene;
import javafx.scene.layout.VBox;
import static org.junit.jupiter.api.Assertions.*;

import java.lang.reflect.Field;
import java.net.URI;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import org.java_websocket.client.WebSocketClient;
import org.java_websocket.handshake.ServerHandshake;

public class MainWindowTest extends ApplicationTest {
    private MainWindowController controller;

    @Override
    public void start(Stage stage) throws Exception {
        FXMLLoader loader = new FXMLLoader(getClass().getResource("/fxml/main-window.fxml"));
        VBox root = loader.load();
        controller = loader.getController();
        stage.setScene(new Scene(root, 400, 500));
        stage.show();
    }

    @Test
    void mainWindow_loadsSuccessfully() {
        assertNotNull(controller);
    }

    @Test
    void connectionBadge_showsDisconnectedByDefault() {
        Label badge = lookup("#connectionBadge").queryAs(Label.class);
        assertNotNull(badge);
        assertTrue(badge.getText().contains("已断开"));
    }



    @Test
    void updateConnectionStatus_changesLabel() {
        interact(() -> controller.updateConnectionStatus(true));
        Label badge = lookup("#connectionBadge").queryAs(Label.class);
        assertTrue(badge.getText().contains("已连接") || badge.getText().contains("●"));
    }

    @Test
    void updateConnectionStatus_changesBadge() {
        interact(() -> controller.updateConnectionStatus(true));
        Label badge = lookup("#connectionBadge").queryAs(Label.class);
        assertTrue(badge.getText().contains("已连接") || badge.getText().contains("●"));
    }

    @Test
    void settingsButton_exists() {
        Button settingsBtn = lookup("#settingsButton").queryAs(Button.class);
        assertNotNull(settingsBtn);
    }

    @Test
    void muteCheckbox_sendsSetVolumeMutedTrue() throws Exception {
        CheckBox muteBox = lookup("#muteCheckBox").queryAs(CheckBox.class);
        assertNotNull(muteBox);
        assertFalse(muteBox.isSelected());

        int port = findFreePort();
        PetWebSocketServer oldServer = (PetWebSocketServer) getPrivateField(controller, "wsServer");
        if (oldServer != null) {
            try { oldServer.stop(200); } catch (Exception ignored) {}
        }
        PetWebSocketServer testServer = new PetWebSocketServer(port);
        testServer.setReuseAddr(true);
        testServer.start();
        Thread.sleep(50);
        setPrivateField(controller, "wsServer", testServer);

        PetInstance instance = new PetInstance("test-mute", "TestMute", "Haru", "stopped", false, "");
        int instanceId = instance.getId();
        setPrivateField(controller, "currentInstance", instance);

        CapturingClient client = new CapturingClient(port, instanceId);
        try {
            assertTrue(client.connectBlocking(3, TimeUnit.SECONDS),
                    "Test WS client should connect to test server on port " + port);
            assertTrue(awaitActiveConnection(testServer, instanceId, 3, TimeUnit.SECONDS),
                    "WS server should register the test client connection");

            interact(() -> muteBox.setSelected(true));

            assertTrue(client.messageLatch.await(3, TimeUnit.SECONDS),
                    "Should receive set_volume message after clicking mute");
            String msg = client.lastMessage.get();
            assertNotNull(msg);
            assertTrue(msg.contains("\"set_volume\""), "Message action should be set_volume: " + msg);
            assertTrue(msg.contains("\"muted\":true"), "Message payload should contain muted:true: " + msg);
        } finally {
            if (client.isOpen()) {
                client.closeBlocking();
            }
            try { testServer.stop(500); } catch (Exception ignored) {}
        }
    }

    private static int findFreePort() {
        try (java.net.ServerSocket socket = new java.net.ServerSocket(0)) {
            return socket.getLocalPort();
        } catch (Exception e) {
            throw new RuntimeException("Failed to find free port", e);
        }
    }

    private static void setPrivateField(Object target, String name, Object value) throws Exception {
        Field f = MainWindowController.class.getDeclaredField(name);
        f.setAccessible(true);
        f.set(target, value);
    }

    private static Object getPrivateField(Object target, String name) throws Exception {
        Field f = MainWindowController.class.getDeclaredField(name);
        f.setAccessible(true);
        return f.get(target);
    }

    private static boolean awaitActiveConnection(PetWebSocketServer server, int instanceId,
                                                  long timeout, TimeUnit unit) throws InterruptedException {
        long deadline = System.nanoTime() + unit.toNanos(timeout);
        while (System.nanoTime() < deadline) {
            if (server.hasActiveConnection(instanceId)) {
                return true;
            }
            Thread.sleep(10);
        }
        return server.hasActiveConnection(instanceId);
    }

    private static final class CapturingClient extends WebSocketClient {
        final CountDownLatch messageLatch = new CountDownLatch(1);
        final AtomicReference<String> lastMessage = new AtomicReference<>();

        CapturingClient(int port, int instanceId) throws java.net.URISyntaxException {
            super(new URI("ws://127.0.0.1:" + port + "/?instance_id=" + instanceId));
        }

        @Override
        public void onOpen(ServerHandshake handshake) {}

        @Override
        public void onMessage(String message) {
            lastMessage.set(message);
            messageLatch.countDown();
        }

        @Override
        public void onClose(int code, String reason, boolean remote) {}

        @Override
        public void onError(Exception ex) {}
    }
}
