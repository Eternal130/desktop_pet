package com.desktoppet.ui;

import com.desktoppet.core.AppOrchestrator;
import com.desktoppet.model.ModelInfo;
import com.desktoppet.model.PetConfig;
import javafx.fxml.FXML;
import javafx.scene.control.TabPane;
import javafx.scene.layout.VBox;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.function.Consumer;

public class MainWindowController {
    private static final Logger log = LoggerFactory.getLogger(MainWindowController.class);

    @FXML private TabPane tabPane;

    @FXML private VBox dashboard;
    @FXML private DashboardTabController dashboardController;
    @FXML private VBox actions;
    @FXML private ActionsTabController actionsController;
    @FXML private VBox settings;
    @FXML private SettingsTabController settingsController;
    @FXML private VBox advanced;
    @FXML private AdvancedTabController advancedController;

    private AppOrchestrator orchestrator;
    private Consumer<PetConfig> onSettingsSaveCallback;

    @FXML
    public void initialize() {
    }

    public void setOrchestrator(AppOrchestrator orchestrator) {
        this.orchestrator = orchestrator;
        dashboardController.init(orchestrator);
        actionsController.init(orchestrator);
        settingsController.init(orchestrator);
        advancedController.init(orchestrator);
    }

    public void setOnSettingsSaveCallback(Consumer<PetConfig> callback) {
        this.onSettingsSaveCallback = callback;
    }

    public void updateConnectionStatus(boolean connected) {
        dashboardController.updateConnectionStatus(connected);
        advancedController.updateRendererStatus(connected);
    }

    public void updateModelName(String name) {
        dashboardController.updateModelName(name);
    }

    public void updateIdleInterval(int seconds) {
        dashboardController.refreshStatus();
        settingsController.loadConfig();
    }

    public void updateModelInfo(ModelInfo info) {
        dashboardController.updateModelInfo(info);
        actionsController.updateForModel(info);
    }

    public void addActivity(String message) {
        dashboardController.addActivity(message);
    }

    public void addMessageLog(String direction, String type, String action, String summary) {
        advancedController.addMessageLog(direction, type, action, summary);
    }
}
