package com.desktoppet.ui;

import com.desktoppet.model.PetConfig;
import javafx.fxml.FXML;
import javafx.scene.control.*;
import javafx.stage.Stage;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.function.Consumer;

public class SettingsPanelController {
    private static final Logger log = LoggerFactory.getLogger(SettingsPanelController.class);

    @FXML private RadioButton directModeRadio;
    @FXML private RadioButton physicsModeRadio;
    @FXML private Spinner<Integer> idleIntervalSpinner;
    @FXML private Slider opacitySlider;
    @FXML private Label opacityValueLabel;
    @FXML private ComboBox<String> modelComboBox;

    private ToggleGroup dragModeGroup;
    private Consumer<PetConfig> onSaveCallback;
    private Runnable onOpacityChangeCallback;
    private PetConfig currentConfig;

    @FXML
    public void initialize() {
        dragModeGroup = new ToggleGroup();
        directModeRadio.setToggleGroup(dragModeGroup);
        physicsModeRadio.setToggleGroup(dragModeGroup);

        if (idleIntervalSpinner.getValueFactory() == null) {
            idleIntervalSpinner.setValueFactory(new SpinnerValueFactory.IntegerSpinnerValueFactory(1, 60, 10));
        }

        opacitySlider.valueProperty().addListener((obs, oldVal, newVal) -> {
            opacityValueLabel.setText(String.format("%.2f", newVal.doubleValue()));
        });
    }

    public void loadConfig(PetConfig config) {
        this.currentConfig = config;
        if ("physics".equals(config.behavior().dragMode())) {
            physicsModeRadio.setSelected(true);
        } else {
            directModeRadio.setSelected(true);
        }
        
        if (idleIntervalSpinner.getValueFactory() != null) {
            idleIntervalSpinner.getValueFactory().setValue(config.behavior().idleIntervalSeconds());
        }
        
        opacitySlider.setValue(config.window().opacity());
        opacityValueLabel.setText(String.format("%.2f", config.window().opacity()));
    }

    public void setAvailableModels(List<String> models, String currentModel) {
        modelComboBox.getItems().setAll(models);
        if (currentModel != null && !currentModel.isEmpty()) {
            modelComboBox.setValue(currentModel);
        }
    }

    public void setOnSaveCallback(Consumer<PetConfig> callback) {
        this.onSaveCallback = callback;
    }

    @FXML
    private void onRefreshModels() {
        Path resourcesDir = Path.of("../renderer/build/bin/desktop-pet-renderer/Resources");
        List<String> models = new ArrayList<>();

        if (Files.exists(resourcesDir)) {
            try (var stream = Files.list(resourcesDir)) {
                stream.filter(Files::isDirectory)
                      .map(p -> p.getFileName().toString())
                      .filter(name -> !name.startsWith("."))
                      .sorted()
                      .forEach(models::add);
            } catch (IOException e) {
                log.warn("Failed to scan models directory: {}", e.getMessage());
            }
        } else {
            log.warn("Resources directory not found: {}", resourcesDir);
        }

        if (!models.isEmpty()) {
            modelComboBox.getItems().setAll(models);
            log.info("Found {} models: {}", models.size(), models);
        } else {
            log.info("No models found in Resources directory");
        }
    }

    @FXML
    private void onSave() {
        if (currentConfig == null || onSaveCallback == null) {
            closeWindow();
            return;
        }
        RadioButton selected = (RadioButton) dragModeGroup.getSelectedToggle();
        String dragMode = selected != null ? (String) selected.getUserData() : "direct";
        int idleInterval = idleIntervalSpinner.getValue() != null ? idleIntervalSpinner.getValue() : 10;
        double opacity = opacitySlider.getValue();
        String selectedModel = modelComboBox.getValue();

        int currentFps = currentConfig != null ? currentConfig.behavior().targetFps() : 0;
        var newBehavior = new com.desktoppet.model.BehaviorConfig(dragMode, idleInterval, currentFps);
        var newWindow = new com.desktoppet.model.WindowConfig(
            currentConfig.window().positionX(),
            currentConfig.window().positionY(),
            currentConfig.window().width(),
            currentConfig.window().height(),
            opacity
        );
        var newModel = selectedModel != null && !selectedModel.isEmpty()
            ? new com.desktoppet.model.ModelSettingsConfig(selectedModel, currentConfig.model().scale())
            : currentConfig.model();
        var newConfig = new PetConfig(newWindow, newModel, newBehavior, currentConfig.system());
        onSaveCallback.accept(newConfig);
        closeWindow();
    }

    @FXML
    private void onCancel() {
        closeWindow();
    }

    private void closeWindow() {
        Stage stage = (Stage) directModeRadio.getScene().getWindow();
        stage.close();
    }
}