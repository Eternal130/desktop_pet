package com.desktoppet.ui;

import com.desktoppet.core.AppOrchestrator;
import com.desktoppet.model.ModelInfo;
import com.desktoppet.model.PetConfig;
import com.desktoppet.model.PetState;
import javafx.application.Platform;
import javafx.collections.FXCollections;
import javafx.collections.ObservableList;
import javafx.fxml.FXML;
import javafx.scene.control.Button;
import javafx.scene.control.ComboBox;
import javafx.scene.control.Label;
import javafx.scene.control.ListView;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.time.LocalTime;
import java.time.format.DateTimeFormatter;
import java.util.List;

public class DashboardTabController {
    private static final Logger log = LoggerFactory.getLogger(DashboardTabController.class);
    private static final DateTimeFormatter TIME_FMT = DateTimeFormatter.ofPattern("HH:mm:ss");
    private static final int MAX_LOG_ENTRIES = 50;

    @FXML private Label statusLabel;
    @FXML private Button restartButton;
    @FXML private ComboBox<String> modelComboBox;
    @FXML private Label motionGroupsLabel;
    @FXML private Label expressionsLabel;
    @FXML private Label hitAreasLabel;
    @FXML private Label windowPositionLabel;
    @FXML private Label opacityLabel;
    @FXML private Label idleIntervalLabel;
    @FXML private Label schedulerStatusLabel;
    @FXML private ListView<String> activityLog;

    private AppOrchestrator orchestrator;
    private final ObservableList<String> activityItems = FXCollections.observableArrayList();
    private boolean suppressModelSelection;

    @FXML
    public void initialize() {
        activityLog.setItems(activityItems);
    }

    public void init(AppOrchestrator orchestrator) {
        this.orchestrator = orchestrator;
        refreshModels();
        refreshStatus();
    }

    public void updateConnectionStatus(boolean connected) {
        Platform.runLater(() -> {
            statusLabel.setText(connected ? "● 已连接" : "○ 未连接");
            statusLabel.getStyleClass().removeAll("status-connected", "status-disconnected");
            statusLabel.getStyleClass().add(connected ? "status-connected" : "status-disconnected");
            schedulerStatusLabel.setText("调度器: " + (connected ? "▶ 运行中" : "⏸ 已暂停"));
        });
    }

    public void updateModelName(String name) {
        Platform.runLater(() -> {
            suppressModelSelection = true;
            modelComboBox.setValue(name);
            suppressModelSelection = false;
            refreshModelInfo(name);
        });
    }

    public void updateModelInfo(ModelInfo info) {
        Platform.runLater(() -> {
            if (info == null) {
                motionGroupsLabel.setText("动作组: —");
                expressionsLabel.setText("表情: —");
                hitAreasLabel.setText("Hit区域: —");
                return;
            }

            String groups = String.join(", ", info.motionGroups().keySet());
            motionGroupsLabel.setText("动作组: " + groups + " (" + info.motionGroups().size() + "组)");

            String exprs = String.join(", ", info.expressions());
            expressionsLabel.setText("表情: " + (exprs.isEmpty() ? "—" : exprs + " (" + info.expressions().size() + "个)"));

            String areas = String.join(", ", info.hitAreas());
            hitAreasLabel.setText("Hit区域: " + (areas.isEmpty() ? "—" : areas));
        });
    }

    public void refreshStatus() {
        if (orchestrator == null) return;
        Platform.runLater(() -> {
            PetConfig config = orchestrator.getConfig();
            if (config != null) {
                PetState state = orchestrator.getStateManager().getState();
                windowPositionLabel.setText("窗口位置: (" + state.windowX() + ", " + state.windowY() + ")");
                opacityLabel.setText("透明度: " + String.format("%.2f", config.window().opacity()));
                idleIntervalLabel.setText("闲时动作: 每 " + config.behavior().idleIntervalSeconds() + "s 触发");
            }
        });
    }

    public void addActivity(String message) {
        Platform.runLater(() -> {
            String entry = LocalTime.now().format(TIME_FMT) + "  " + message;
            activityItems.addFirst(entry);
            if (activityItems.size() > MAX_LOG_ENTRIES) {
                activityItems.removeLast();
            }
        });
    }

    @FXML
    private void onRestartRenderer() {
        if (orchestrator != null) {
            orchestrator.restartRenderer();
            addActivity("手动重启渲染器");
        }
    }

    @FXML
    private void onModelSelected() {
        if (suppressModelSelection) return;
        String selected = modelComboBox.getValue();
        if (orchestrator != null && selected != null && !selected.isEmpty()) {
            orchestrator.loadModel(selected);
            addActivity("切换模型: " + selected);
        }
    }

    @FXML
    private void onRefreshModels() {
        refreshModels();
    }

    private void refreshModels() {
        if (orchestrator == null) return;
        List<String> models = orchestrator.getAvailableModels();
        suppressModelSelection = true;
        modelComboBox.getItems().setAll(models);
        PetConfig config = orchestrator.getConfig();
        if (config != null) {
            modelComboBox.setValue(config.model().currentModelName());
        }
        suppressModelSelection = false;
    }

    private void refreshModelInfo(String modelName) {
        if (orchestrator == null || modelName == null || modelName.isEmpty()) return;
        orchestrator.getModelInfo(modelName).ifPresent(this::updateModelInfo);
    }
}
