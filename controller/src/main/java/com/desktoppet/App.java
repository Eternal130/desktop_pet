package com.desktoppet;

import com.desktoppet.core.PanelStateManager;
import com.desktoppet.model.PanelConfig;
import com.desktoppet.ui.MainWindowController;
import com.desktoppet.ui.TrayManager;
import javafx.application.Application;
import javafx.application.Platform;
import javafx.fxml.FXMLLoader;
import javafx.scene.Scene;
import javafx.scene.layout.VBox;
import javafx.stage.Stage;
import javafx.stage.StageStyle;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class App extends Application {
    private static final Logger log = LoggerFactory.getLogger(App.class);
    private PanelStateManager panelStateManager;
    private TrayManager trayManager;

    @Override
    public void start(Stage primaryStage) throws Exception {
        panelStateManager = new PanelStateManager();
        PanelConfig savedState = panelStateManager.load();

        FXMLLoader loader = new FXMLLoader(getClass().getResource("/fxml/main-window.fxml"));
        VBox root = loader.load();
        MainWindowController controller = loader.getController();
        controller.setPanelStateManager(panelStateManager);

        double width = Math.max(900, savedState.panelWidth());
        double height = Math.max(600, savedState.panelHeight());

        Scene scene = new Scene(root, width, height);
        scene.getStylesheets().add(getClass().getResource("/css/style.css").toExternalForm());

        primaryStage.initStyle(StageStyle.UNDECORATED);
        primaryStage.setTitle("Live2D 桌面宠物控制面板");
        primaryStage.setScene(scene);
        primaryStage.setMinWidth(900);
        primaryStage.setMinHeight(600);

        if (savedState.panelX() >= 0 && savedState.panelY() >= 0) {
            primaryStage.setX(savedState.panelX());
            primaryStage.setY(savedState.panelY());
        }

        controller.enableWindowResize(primaryStage);
        controller.restoreState();

        trayManager = new TrayManager(primaryStage);
        trayManager.setOnExitCallback(() -> Platform.runLater(controller::performFullShutdown));
        trayManager.setOnCloseRequestCallback(controller::handleCloseRequest);
        trayManager.initialize();

        if (!controller.isStartMinimized() || !java.awt.SystemTray.isSupported()) {
            primaryStage.show();
        }

        log.info("Desktop Pet Controller started");
    }

    @Override
    public void stop() {
        if (trayManager != null) {
            trayManager.shutdown();
        }
    }

    public static void main(String[] args) {
        launch(args);
    }
}
