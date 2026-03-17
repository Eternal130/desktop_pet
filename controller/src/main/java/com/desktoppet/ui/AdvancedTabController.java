package com.desktoppet.ui;

import com.desktoppet.core.AppOrchestrator;
import com.desktoppet.model.PetState;
import com.google.gson.JsonObject;
import javafx.application.Platform;
import javafx.collections.FXCollections;
import javafx.collections.ObservableList;
import javafx.fxml.FXML;
import javafx.scene.control.CheckBox;
import javafx.scene.control.Label;
import javafx.scene.control.ListView;
import javafx.scene.control.Spinner;
import javafx.scene.control.SpinnerValueFactory;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.awt.Desktop;
import java.nio.file.Path;
import java.time.LocalTime;
import java.time.format.DateTimeFormatter;

public class AdvancedTabController {
    private static final Logger log = LoggerFactory.getLogger(AdvancedTabController.class);
    private static final DateTimeFormatter TIME_FMT = DateTimeFormatter.ofPattern("HH:mm:ss");
    private static final int MAX_LOG_ENTRIES = 200;

    @FXML private Label rendererStatusLabel;
    @FXML private Label restartAttemptsLabel;
    @FXML private Spinner<Integer> positionXSpinner;
    @FXML private Spinner<Integer> positionYSpinner;
    @FXML private ListView<String> messageLogView;
    @FXML private CheckBox autoScrollCheckBox;
    @FXML private Label configPathLabel;

    private AppOrchestrator orchestrator;
    private final ObservableList<String> messageLogItems = FXCollections.observableArrayList();

    @FXML
    public void initialize() {
        positionXSpinner.setValueFactory(
                new SpinnerValueFactory.IntegerSpinnerValueFactory(-9999, 9999, 0));
        positionYSpinner.setValueFactory(
                new SpinnerValueFactory.IntegerSpinnerValueFactory(-9999, 9999, 0));
        positionXSpinner.setEditable(true);
        positionYSpinner.setEditable(true);
        messageLogView.setItems(messageLogItems);
    }

    public void init(AppOrchestrator orchestrator) {
        this.orchestrator = orchestrator;
        configPathLabel.setText("路径: " + orchestrator.getConfigPath());
        refreshRendererStatus();
        refreshPosition();
    }

    public void updateRendererStatus(boolean connected) {
        Platform.runLater(() ->
                rendererStatusLabel.setText("状态: " + (connected ? "运行中" : "已断开")));
    }

    public void refreshPosition() {
        if (orchestrator == null) return;
        PetState state = orchestrator.getStateManager().getState();
        Platform.runLater(() -> {
            positionXSpinner.getValueFactory().setValue(state.windowX());
            positionYSpinner.getValueFactory().setValue(state.windowY());
        });
    }

    public void addMessageLog(String direction, String type, String action, String summary) {
        Platform.runLater(() -> {
            String entry = LocalTime.now().format(TIME_FMT) + " " + direction
                    + " " + type + "/" + action + " " + summary;
            messageLogItems.addFirst(entry);
            if (messageLogItems.size() > MAX_LOG_ENTRIES) {
                messageLogItems.removeLast();
            }
            if (autoScrollCheckBox.isSelected()) {
                messageLogView.scrollTo(0);
            }
        });
    }

    private void refreshRendererStatus() {
        if (orchestrator == null) return;
        PetState state = orchestrator.getStateManager().getState();
        updateRendererStatus(state.connected());
    }

    @FXML
    private void onRestartRenderer() {
        if (orchestrator != null) {
            orchestrator.restartRenderer();
        }
    }

    @FXML
    private void onStopRenderer() {
        if (orchestrator != null) {
            orchestrator.stopRenderer();
        }
    }

    @FXML
    private void onApplyPosition() {
        if (orchestrator == null) return;
        int x = positionXSpinner.getValue();
        int y = positionYSpinner.getValue();
        JsonObject payload = new JsonObject();
        payload.addProperty("x", x);
        payload.addProperty("y", y);
        orchestrator.sendCommand("set_position", payload);
    }

    @FXML
    private void onClearLog() {
        messageLogItems.clear();
    }

    @FXML
    private void onOpenConfigDir() {
        try {
            Path configDir = Path.of(orchestrator.getConfigPath()).getParent();
            if (configDir != null && Desktop.isDesktopSupported()) {
                Desktop.getDesktop().open(configDir.toFile());
            }
        } catch (Exception e) {
            log.warn("Failed to open config directory: {}", e.getMessage());
        }
    }

    @FXML
    private void onReloadConfig() {
        if (orchestrator != null) {
            orchestrator.reloadConfig();
        }
    }
}
