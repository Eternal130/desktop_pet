package com.desktoppet.ui;

import com.desktoppet.model.PetConfig;
import javafx.application.Platform;
import javafx.fxml.FXML;
import javafx.fxml.FXMLLoader;
import javafx.scene.Parent;
import javafx.scene.Scene;
import javafx.scene.control.Button;
import javafx.scene.control.Label;
import javafx.stage.Stage;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.util.function.Consumer;

public class MainWindowController {
    @FXML private Label statusLabel;
    @FXML private Label modelNameLabel;
    @FXML private Label idleIntervalLabel;
    @FXML private Button settingsButton;

    private Consumer<PetConfig> onSettingsSaveCallback;

    @FXML
    public void initialize() {
        updateConnectionStatus(false);
        updateModelName("None");
    }

    public void setOnSettingsSaveCallback(Consumer<PetConfig> callback) {
        this.onSettingsSaveCallback = callback;
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
        openSettingsPanel();
    }

    private void openSettingsPanel() {
        try {
            FXMLLoader loader = new FXMLLoader(getClass().getResource("/fxml/settings-panel.fxml"));
            Parent root = loader.load();
            
            SettingsPanelController controller = loader.getController();
            if (onSettingsSaveCallback != null) {
                controller.setOnSaveCallback(onSettingsSaveCallback);
            }

            Stage stage = new Stage();
            stage.setTitle("Settings");
            stage.setScene(new Scene(root));
            stage.show();
        } catch (IOException e) {
            log.error("Failed to open settings panel", e);
        }
    }

    private static final Logger log = LoggerFactory.getLogger(MainWindowController.class);
}