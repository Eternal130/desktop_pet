package com.desktoppet.ui;

import javafx.scene.control.Button;
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
}
