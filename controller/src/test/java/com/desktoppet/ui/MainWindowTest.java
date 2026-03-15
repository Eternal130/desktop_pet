package com.desktoppet.ui;

import javafx.scene.control.Label;
import org.junit.jupiter.api.Test;
import org.testfx.framework.junit5.ApplicationTest;
import javafx.stage.Stage;
import javafx.fxml.FXMLLoader;
import javafx.scene.Scene;
import javafx.scene.layout.VBox;
import static org.junit.jupiter.api.Assertions.*;

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
    void statusLabel_showsDisconnectedByDefault() {
        Label statusLabel = lookup("#statusLabel").queryAs(Label.class);
        assertNotNull(statusLabel);
        assertTrue(statusLabel.getText().contains("Disconnected") || statusLabel.getText().contains("○"));
    }

    @Test
    void updateConnectionStatus_changesLabel() {
        interact(() -> controller.updateConnectionStatus(true));
        Label statusLabel = lookup("#statusLabel").queryAs(Label.class);
        assertTrue(statusLabel.getText().contains("Connected") || statusLabel.getText().contains("●"));
    }

    @Test
    void settingsButton_exists() {
        assertNotNull(lookup("#settingsButton").tryQuery().orElse(null));
    }
}
