package com.desktoppet.ui;

import com.desktoppet.core.AppOrchestrator;
import com.desktoppet.model.ModelInfo;
import com.google.gson.JsonObject;
import javafx.application.Platform;
import javafx.fxml.FXML;
import javafx.scene.control.Button;
import javafx.scene.control.ComboBox;
import javafx.scene.control.Label;
import javafx.scene.layout.FlowPane;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class ActionsTabController {
    private static final Logger log = LoggerFactory.getLogger(ActionsTabController.class);

    @FXML private ComboBox<String> motionGroupCombo;
    @FXML private ComboBox<Integer> motionIndexCombo;
    @FXML private ComboBox<String> priorityCombo;
    @FXML private FlowPane quickActionsPane;
    @FXML private Label noMotionsLabel;
    @FXML private FlowPane expressionsPane;
    @FXML private Label noExpressionsLabel;
    @FXML private Label currentExpressionLabel;

    private AppOrchestrator orchestrator;
    private ModelInfo currentModelInfo;

    @FXML
    public void initialize() {
        priorityCombo.getItems().addAll("Idle (1)", "Normal (2)", "Force (3)");
        priorityCombo.setValue("Normal (2)");
    }

    public void init(AppOrchestrator orchestrator) {
        this.orchestrator = orchestrator;
    }

    public void updateForModel(ModelInfo info) {
        this.currentModelInfo = info;
        Platform.runLater(() -> {
            motionGroupCombo.getItems().clear();
            motionIndexCombo.getItems().clear();
            quickActionsPane.getChildren().clear();
            expressionsPane.getChildren().clear();

            if (info == null) {
                noMotionsLabel.setVisible(true);
                noMotionsLabel.setManaged(true);
                noExpressionsLabel.setVisible(true);
                noExpressionsLabel.setManaged(true);
                currentExpressionLabel.setText("当前表情: —");
                return;
            }

            if (!info.motionGroups().isEmpty()) {
                noMotionsLabel.setVisible(false);
                noMotionsLabel.setManaged(false);
                motionGroupCombo.getItems().addAll(info.motionGroups().keySet());
                motionGroupCombo.getSelectionModel().selectFirst();
                updateMotionIndices();

                for (String group : info.motionGroups().keySet()) {
                    Button btn = new Button(group);
                    btn.getStyleClass().add("quick-action-button");
                    btn.setOnAction(e -> playMotion(group, 0, 2));
                    quickActionsPane.getChildren().add(btn);
                }
            } else {
                noMotionsLabel.setVisible(true);
                noMotionsLabel.setManaged(true);
            }

            if (!info.expressions().isEmpty()) {
                noExpressionsLabel.setVisible(false);
                noExpressionsLabel.setManaged(false);
                for (String expr : info.expressions()) {
                    Button btn = new Button(expr);
                    btn.getStyleClass().add("expression-button");
                    btn.setOnAction(e -> setExpression(expr));
                    expressionsPane.getChildren().add(btn);
                }
            } else {
                noExpressionsLabel.setVisible(true);
                noExpressionsLabel.setManaged(true);
            }

            currentExpressionLabel.setText("当前表情: —");
        });
    }

    @FXML
    private void onMotionGroupChanged() {
        updateMotionIndices();
    }

    @FXML
    private void onPlayMotion() {
        String group = motionGroupCombo.getValue();
        Integer index = motionIndexCombo.getValue();
        if (group == null || index == null) return;
        int priority = parsePriority(priorityCombo.getValue());
        playMotion(group, index, priority);
    }

    @FXML
    private void onStopMotion() {
        if (orchestrator != null) {
            orchestrator.sendCommand("stop_motion", new JsonObject());
        }
    }

    private void playMotion(String group, int index, int priority) {
        if (orchestrator == null) return;
        JsonObject payload = new JsonObject();
        payload.addProperty("group", group);
        payload.addProperty("index", index);
        payload.addProperty("priority", priority);
        orchestrator.sendCommand("play_motion", payload);
    }

    private void setExpression(String expressionId) {
        if (orchestrator == null) return;
        JsonObject payload = new JsonObject();
        payload.addProperty("expression_id", expressionId);
        orchestrator.sendCommand("set_expression", payload);
        currentExpressionLabel.setText("当前表情: " + expressionId);
    }

    private void updateMotionIndices() {
        motionIndexCombo.getItems().clear();
        if (currentModelInfo == null) return;
        String group = motionGroupCombo.getValue();
        if (group == null) return;
        Integer count = currentModelInfo.motionGroups().get(group);
        if (count != null) {
            for (int i = 0; i < count; i++) {
                motionIndexCombo.getItems().add(i);
            }
            motionIndexCombo.getSelectionModel().selectFirst();
        }
    }

    private int parsePriority(String priorityStr) {
        if (priorityStr == null) return 2;
        if (priorityStr.startsWith("Idle")) return 1;
        if (priorityStr.startsWith("Force")) return 3;
        return 2;
    }
}
