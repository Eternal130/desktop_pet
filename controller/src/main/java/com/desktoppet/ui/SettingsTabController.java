package com.desktoppet.ui;

import com.desktoppet.core.AppOrchestrator;
import com.desktoppet.model.BehaviorConfig;
import com.desktoppet.model.HitAction;
import com.desktoppet.model.PetConfig;
import com.desktoppet.model.SystemConfig;
import com.desktoppet.model.WindowConfig;
import javafx.beans.property.SimpleIntegerProperty;
import javafx.beans.property.SimpleStringProperty;
import javafx.collections.FXCollections;
import javafx.collections.ObservableList;
import javafx.fxml.FXML;
import javafx.scene.control.CheckBox;
import javafx.scene.control.ComboBox;
import javafx.scene.control.Label;
import javafx.scene.control.RadioButton;
import javafx.scene.control.Slider;
import javafx.scene.control.Spinner;
import javafx.scene.control.SpinnerValueFactory;
import javafx.scene.control.TableColumn;
import javafx.scene.control.TableView;
import javafx.scene.control.ToggleGroup;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.List;
import java.util.Map;

public class SettingsTabController {
    private static final Logger log = LoggerFactory.getLogger(SettingsTabController.class);

    @FXML private Slider opacitySlider;
    @FXML private Label opacityValueLabel;
    @FXML private RadioButton directModeRadio;
    @FXML private RadioButton physicsModeRadio;
    @FXML private Spinner<Integer> idleIntervalSpinner;
    @FXML private RadioButton adaptiveFpsRadio;
    @FXML private RadioButton fixedFpsRadio;
    @FXML private Spinner<Integer> fpsSpinner;
    @FXML private TableView<HitMappingRow> hitMappingTable;
    @FXML private TableColumn<HitMappingRow, String> hitAreaColumn;
    @FXML private TableColumn<HitMappingRow, String> motionGroupColumn;
    @FXML private TableColumn<HitMappingRow, Number> priorityColumn;
    @FXML private CheckBox autoStartCheckBox;
    @FXML private ComboBox<String> voicePackComboBox;

    private ToggleGroup dragModeGroup;
    private ToggleGroup fpsModeGroup;
    private AppOrchestrator orchestrator;

    @FXML
    public void initialize() {
        dragModeGroup = new ToggleGroup();
        directModeRadio.setToggleGroup(dragModeGroup);
        physicsModeRadio.setToggleGroup(dragModeGroup);

        idleIntervalSpinner.setValueFactory(
                new SpinnerValueFactory.IntegerSpinnerValueFactory(1, 60, 10));

        fpsModeGroup = new ToggleGroup();
        adaptiveFpsRadio.setToggleGroup(fpsModeGroup);
        fixedFpsRadio.setToggleGroup(fpsModeGroup);

        fpsSpinner.setValueFactory(
                new SpinnerValueFactory.IntegerSpinnerValueFactory(15, 120, 30));
        fpsSpinner.setDisable(true);

        fpsModeGroup.selectedToggleProperty().addListener((obs, oldVal, newVal) -> {
            if (newVal != null) {
                boolean fixed = "fixed".equals(((RadioButton) newVal).getUserData());
                fpsSpinner.setDisable(!fixed);
            }
        });

        opacitySlider.valueProperty().addListener((obs, oldVal, newVal) ->
                opacityValueLabel.setText(String.format("%.2f", newVal.doubleValue())));

        hitAreaColumn.setCellValueFactory(cd -> cd.getValue().areaProperty());
        motionGroupColumn.setCellValueFactory(cd -> cd.getValue().motionGroupProperty());
        priorityColumn.setCellValueFactory(cd -> cd.getValue().priorityProperty());
    }

    public void init(AppOrchestrator orchestrator) {
        this.orchestrator = orchestrator;
        loadConfig();
        refreshVoicePacks();
    }

    private void refreshVoicePacks() {
        if (orchestrator == null || voicePackComboBox == null) return;
        String currentModel = orchestrator.getConfig() != null
            ? orchestrator.getConfig().model().currentModelName() : null;
        
        List<String> voicePacks = orchestrator.getAvailableVoicePacks();
        
        voicePackComboBox.getItems().clear();
        voicePackComboBox.getItems().add("(无)");
        voicePackComboBox.getItems().addAll(voicePacks);
        
        String current = currentModel != null ? orchestrator.getCurrentVoicePackForModel(currentModel) : null;
        voicePackComboBox.setValue(current != null ? current : "(无)");
    }

    @FXML
    private void onVoicePackChanged() {
        if (orchestrator == null || voicePackComboBox == null) return;
        String currentModel = orchestrator.getConfig() != null
            ? orchestrator.getConfig().model().currentModelName() : null;
        if (currentModel == null) return;
        
        String selected = voicePackComboBox.getValue();
        if (selected == null || selected.equals("(无)")) {
            orchestrator.unmountVoicePack(currentModel);
        } else {
            orchestrator.mountVoicePack(currentModel, selected);
        }
    }

    public void loadConfig() {
        if (orchestrator == null) return;
        PetConfig config = orchestrator.getConfig();
        if (config == null) return;

        if ("physics".equals(config.behavior().dragMode())) {
            physicsModeRadio.setSelected(true);
        } else {
            directModeRadio.setSelected(true);
        }

        idleIntervalSpinner.getValueFactory().setValue(config.behavior().idleIntervalSeconds());

        int targetFps = config.behavior().targetFps();
        if (targetFps > 0) {
            fixedFpsRadio.setSelected(true);
            fpsSpinner.getValueFactory().setValue(targetFps);
        } else {
            adaptiveFpsRadio.setSelected(true);
            fpsSpinner.getValueFactory().setValue(30);
        }

        opacitySlider.setValue(config.window().opacity());
        opacityValueLabel.setText(String.format("%.2f", config.window().opacity()));
        autoStartCheckBox.setSelected(config.system().autoStart());
    }

    public void loadHitMappings(Map<String, HitAction> hitActions) {
        ObservableList<HitMappingRow> rows = FXCollections.observableArrayList();
        if (hitActions != null) {
            for (Map.Entry<String, HitAction> entry : hitActions.entrySet()) {
                rows.add(new HitMappingRow(
                        entry.getKey(),
                        entry.getValue().motionGroup(),
                        entry.getValue().priority()));
            }
        }
        if (rows.stream().noneMatch(r -> "head".equals(r.getArea()))) {
            rows.add(new HitMappingRow("head", "TapHead", 2));
        }
        if (rows.stream().noneMatch(r -> "body".equals(r.getArea()))) {
            rows.add(new HitMappingRow("body", "TapBody", 2));
        }
        hitMappingTable.setItems(rows);
    }

    @FXML
    private void onSave() {
        if (orchestrator == null) return;

        RadioButton selected = (RadioButton) dragModeGroup.getSelectedToggle();
        String dragMode = selected != null ? (String) selected.getUserData() : "direct";
        int idleInterval = idleIntervalSpinner.getValue();
        double opacity = opacitySlider.getValue();
        boolean autoStart = autoStartCheckBox.isSelected();

        RadioButton fpsSelected = (RadioButton) fpsModeGroup.getSelectedToggle();
        boolean fixedFps = fpsSelected != null && "fixed".equals(fpsSelected.getUserData());
        int targetFps = fixedFps ? fpsSpinner.getValue() : 0;

        PetConfig current = orchestrator.getConfig();
        var newBehavior = new BehaviorConfig(dragMode, idleInterval, targetFps);
        var newWindow = new WindowConfig(
                current.window().positionX(),
                current.window().positionY(),
                opacity);
        var newSystem = new SystemConfig(autoStart);
        var newConfig = new PetConfig(newWindow, current.model(), newBehavior, newSystem);

        orchestrator.applyConfig(newConfig);
        log.info("Settings saved");
    }

    @FXML
    private void onResetMappings() {
        ObservableList<HitMappingRow> rows = FXCollections.observableArrayList();
        rows.add(new HitMappingRow("head", "TapHead", 2));
        rows.add(new HitMappingRow("body", "TapBody", 2));
        hitMappingTable.setItems(rows);
    }

    @FXML
    private void onResetAll() {
        PetConfig defaults = PetConfig.defaults();
        directModeRadio.setSelected(true);
        idleIntervalSpinner.getValueFactory().setValue(defaults.behavior().idleIntervalSeconds());
        adaptiveFpsRadio.setSelected(true);
        fpsSpinner.getValueFactory().setValue(30);
        opacitySlider.setValue(defaults.window().opacity());
        autoStartCheckBox.setSelected(defaults.system().autoStart());
        onResetMappings();
    }

    public static class HitMappingRow {
        private final SimpleStringProperty area;
        private final SimpleStringProperty motionGroup;
        private final SimpleIntegerProperty priority;

        public HitMappingRow(String area, String motionGroup, int priority) {
            this.area = new SimpleStringProperty(area);
            this.motionGroup = new SimpleStringProperty(motionGroup);
            this.priority = new SimpleIntegerProperty(priority);
        }

        public String getArea() { return area.get(); }
        public SimpleStringProperty areaProperty() { return area; }

        public String getMotionGroup() { return motionGroup.get(); }
        public SimpleStringProperty motionGroupProperty() { return motionGroup; }

        public int getPriority() { return priority.get(); }
        public SimpleIntegerProperty priorityProperty() { return priority; }
    }
}
