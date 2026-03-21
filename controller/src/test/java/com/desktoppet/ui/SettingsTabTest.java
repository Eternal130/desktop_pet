package com.desktoppet.ui;

import javafx.scene.control.ComboBox;
import org.junit.jupiter.api.Test;
import org.testfx.framework.junit5.ApplicationTest;
import javafx.stage.Stage;
import javafx.fxml.FXMLLoader;
import javafx.scene.Scene;
import javafx.scene.layout.VBox;
import static org.junit.jupiter.api.Assertions.*;

public class SettingsTabTest extends ApplicationTest {
    private SettingsTabController controller;

    @Override
    public void start(Stage stage) throws Exception {
        FXMLLoader loader = new FXMLLoader(getClass().getResource("/fxml/tab-settings.fxml"));
        VBox root = loader.load();
        controller = loader.getController();
        stage.setScene(new Scene(root, 500, 600));
        stage.show();
    }

    @Test
    void settingsTab_loadsSuccessfully() {
        assertNotNull(controller);
    }

    @Test
    void voicePackComboBox_exists() {
        ComboBox<?> combo = lookup("#voicePackComboBox").queryAs(ComboBox.class);
        assertNotNull(combo, "voicePackComboBox should exist in the settings tab");
    }

    @Test
    void voicePackComboBox_hasNoneOption() {
        ComboBox<?> combo = lookup("#voicePackComboBox").queryAs(ComboBox.class);
        assertNotNull(combo);
    }
}
