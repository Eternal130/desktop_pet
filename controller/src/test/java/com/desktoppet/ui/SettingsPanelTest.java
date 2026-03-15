package com.desktoppet.ui;

import com.desktoppet.model.PetConfig;
import javafx.scene.control.*;
import org.junit.jupiter.api.Test;
import org.testfx.framework.junit5.ApplicationTest;
import javafx.stage.Stage;
import javafx.fxml.FXMLLoader;
import javafx.scene.Scene;
import javafx.scene.layout.VBox;
import static org.junit.jupiter.api.Assertions.*;

public class SettingsPanelTest extends ApplicationTest {
    private SettingsPanelController controller;

    @Override
    public void start(Stage stage) throws Exception {
        FXMLLoader loader = new FXMLLoader(getClass().getResource("/fxml/settings-panel.fxml"));
        VBox root = loader.load();
        controller = loader.getController();
        stage.setScene(new Scene(root, 400, 500));
        stage.show();
    }

    @Test
    void settingsPanel_loadsSuccessfully() {
        assertNotNull(controller);
    }

    @Test
    void loadConfig_populatesFields() {
        PetConfig config = PetConfig.defaults();
        interact(() -> controller.loadConfig(config));
        Slider opacitySlider = lookup("#opacitySlider").queryAs(Slider.class);
        assertNotNull(opacitySlider);
        assertEquals(1.0, opacitySlider.getValue(), 0.01);
    }

    @Test
    void idleIntervalSpinner_exists() {
        assertNotNull(lookup("#idleIntervalSpinner").tryQuery().orElse(null));
    }

    @Test
    void directModeRadio_selectedAfterLoadConfig() {
        PetConfig config = PetConfig.defaults();
        interact(() -> controller.loadConfig(config));
        RadioButton directModeRadio = lookup("#directModeRadio").queryAs(RadioButton.class);
        assertNotNull(directModeRadio);
        assertTrue(directModeRadio.isSelected());
    }
}
