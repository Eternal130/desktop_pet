package com.desktoppet.ui;

import javafx.application.Platform;
import javafx.fxml.FXML;
import javafx.scene.control.Button;
import javafx.scene.control.Label;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class MainWindowController {
    @FXML private Label statusLabel;
    @FXML private Label modelNameLabel;
    @FXML private Label idleIntervalLabel;
    @FXML private Button settingsButton;
    
    @FXML
    public void initialize() {
        updateConnectionStatus(false);
        updateModelName("None");
    }
    
    public void updateConnectionStatus(boolean connected) {
        Platform.runLater(() -> {
            statusLabel.setText(connected ? "● Connected" : "○ Disconnected");
            statusLabel.setStyle(connected ? "-fx-text-fill: green;" : "-fx-text-fill: red;");
        });
    }
    
    public void updateModelName(String name) {
        Platform.runLater(() -> modelNameLabel.setText("Model: " + name));
    }
    
    public void updateIdleInterval(int seconds) {
        Platform.runLater(() -> idleIntervalLabel.setText("Idle: " + seconds + "s"));
    }
    
    @FXML
    private void onSettingsButtonClicked() {
        log.info("Settings button clicked (not yet implemented)");
    }
    
    private static final Logger log = LoggerFactory.getLogger(MainWindowController.class);
}