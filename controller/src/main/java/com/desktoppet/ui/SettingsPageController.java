package com.desktoppet.ui;

import javafx.fxml.FXML;
import javafx.scene.control.Button;
import javafx.scene.control.CheckBox;
import javafx.scene.control.Label;
import javafx.scene.control.RadioButton;
import javafx.scene.control.Slider;
import javafx.scene.layout.GridPane;
import javafx.scene.layout.HBox;
import javafx.scene.layout.Region;
import javafx.scene.layout.VBox;
import javafx.scene.paint.Color;
import javafx.scene.shape.Rectangle;
import javafx.css.PseudoClass;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.LinkedHashMap;
import java.util.Map;

public class SettingsPageController {

    private static final Logger log = LoggerFactory.getLogger(SettingsPageController.class);

    private static final PseudoClass THEME_ACTIVE = PseudoClass.getPseudoClass("theme-active");

    private static final Map<String, String[]> THEME_COLORS = new LinkedHashMap<>();
    static {
        // Each entry: theme name -> [root bg, sidebar bg, accent color]
        THEME_COLORS.put("深紫梦幻", new String[]{"#1C1B2E", "#252440", "#FFB7C5"});
        THEME_COLORS.put("樱花浅粉", new String[]{"#FFF5F7", "#FFE8EE", "#E91E63"});
        THEME_COLORS.put("赛博霓虹", new String[]{"#0A0A0F", "#0F0F1A", "#FF0080"});
        THEME_COLORS.put("暖橘小窝", new String[]{"#1A150F", "#231C14", "#FFB347"});
        THEME_COLORS.put("深海蔚蓝", new String[]{"#0B1929", "#0F2136", "#00BCD4"});
        THEME_COLORS.put("终端黑客", new String[]{"#0A0A0A", "#0E0E0E", "#00FF41"});
        THEME_COLORS.put("云石浅灰", new String[]{"#F5F5F5", "#EBEBEB", "#455A64"});
    }

    @FXML private Button backBtn;
    @FXML private GridPane themeGrid;
    @FXML private Slider fontSizeSlider;
    @FXML private Label fontSizeValueLabel;
    @FXML private Slider panelOpacitySlider;
    @FXML private Label panelOpacityValueLabel;
    @FXML private CheckBox autoLaunchCheckBox;
    @FXML private CheckBox startMinimizedCheckBox;
    @FXML private RadioButton closeExitRadio;
    @FXML private RadioButton closeHideRadio;
    @FXML private CheckBox confirmOnExitCheckBox;

    private MainWindowController mainController;
    private String currentTheme;
    private boolean updatingUI = false;

    public void setMainWindowController(MainWindowController controller) {
        this.mainController = controller;
    }

    public void loadSettings(String currentTheme, int fontSize, double panelOpacity,
                             boolean autoLaunchSystem, boolean startMinimized,
                             String closeAction, boolean confirmOnExit) {
        updatingUI = true;
        try {
            this.currentTheme = currentTheme;
            buildThemeCards();
            fontSizeSlider.setValue(fontSize);
            fontSizeValueLabel.setText(fontSize + "px");
            panelOpacitySlider.setValue(panelOpacity);
            panelOpacityValueLabel.setText(Math.round(panelOpacity * 100) + "%");
            autoLaunchCheckBox.setSelected(autoLaunchSystem);
            startMinimizedCheckBox.setSelected(startMinimized);
            if ("hide_to_tray".equals(closeAction)) {
                closeHideRadio.setSelected(true);
            } else {
                closeExitRadio.setSelected(true);
            }
            confirmOnExitCheckBox.setSelected(confirmOnExit);
        } finally {
            updatingUI = false;
        }
    }

    public void updateThemeSelection(String themeName) {
        this.currentTheme = themeName;
        for (var node : themeGrid.getChildren()) {
            if (node instanceof VBox card) {
                boolean isActive = themeName.equals(card.getUserData());
                card.pseudoClassStateChanged(THEME_ACTIVE, isActive);
            }
        }
    }

    @FXML
    private void onBack() {
        if (mainController != null) {
            mainController.showInstanceDetail();
        }
    }

    @FXML
    private void initialize() {
        clipSliderToBounds(fontSizeSlider);
        clipSliderToBounds(panelOpacitySlider);
        MainWindowController.setupToggleSwitch(autoLaunchCheckBox);
        MainWindowController.setupToggleSwitch(startMinimizedCheckBox);
        MainWindowController.setupToggleSwitch(confirmOnExitCheckBox);

        fontSizeSlider.valueProperty().addListener((obs, oldValue, newValue) -> {
            if (updatingUI) return;
            int size = (int) Math.round(newValue.doubleValue());
            fontSizeValueLabel.setText(size + "px");
            if (mainController != null) {
                mainController.applyFontSize(size);
            }
        });

        panelOpacitySlider.valueProperty().addListener((obs, oldValue, newValue) -> {
            if (updatingUI) return;
            double opacity = newValue.doubleValue();
            panelOpacityValueLabel.setText(Math.round(opacity * 100) + "%");
            if (mainController != null) {
                mainController.applyPanelOpacity(opacity);
            }
        });

        autoLaunchCheckBox.selectedProperty().addListener((obs, old, val) -> {
            if (updatingUI) return;
            if (mainController != null) mainController.applyAutoLaunch(val);
        });

        startMinimizedCheckBox.selectedProperty().addListener((obs, old, val) -> {
            if (updatingUI) return;
            if (mainController != null) mainController.applyStartMinimized(val);
        });

        closeExitRadio.selectedProperty().addListener((obs, old, val) -> {
            if (updatingUI) return;
            if (val && mainController != null) mainController.applyCloseAction("exit");
        });

        closeHideRadio.selectedProperty().addListener((obs, old, val) -> {
            if (updatingUI) return;
            if (val && mainController != null) mainController.applyCloseAction("hide_to_tray");
        });

        confirmOnExitCheckBox.selectedProperty().addListener((obs, old, val) -> {
            if (updatingUI) return;
            if (mainController != null) mainController.applyConfirmOnExit(val);
        });
    }

    private void buildThemeCards() {
        themeGrid.getChildren().clear();
        int col = 0;
        int row = 0;
        int columns = 4;

        for (var entry : THEME_COLORS.entrySet()) {
            String themeName = entry.getKey();
            String[] colors = entry.getValue();

            VBox card = new VBox(8);
            card.getStyleClass().add("theme-card");
            card.setUserData(themeName);

            HBox swatches = new HBox(4);
            swatches.setAlignment(javafx.geometry.Pos.CENTER);
            for (String color : colors) {
                Region swatch = new Region();
                swatch.getStyleClass().add("theme-swatch");
                swatch.setStyle("-fx-background-color: " + color + ";");
                swatches.getChildren().add(swatch);
            }

            Label nameLabel = new Label(themeName);
            nameLabel.getStyleClass().add("theme-card-name");

            card.getChildren().addAll(swatches, nameLabel);

            boolean isActive = themeName.equals(currentTheme);
            card.pseudoClassStateChanged(THEME_ACTIVE, isActive);

            card.setOnMouseClicked(e -> {
                if (mainController != null) {
                    mainController.applyTheme(themeName);
                }
            });

            themeGrid.add(card, col, row);
            col++;
            if (col >= columns) {
                col = 0;
                row++;
            }
        }
    }

    private void clipSliderToBounds(Slider slider) {
        Rectangle clip = new Rectangle();
        clip.widthProperty().bind(slider.widthProperty());
        clip.heightProperty().bind(slider.heightProperty());
        slider.setClip(clip);
    }
}
