package com.desktoppet;

import com.desktoppet.core.AppOrchestrator;
import com.desktoppet.ui.MainWindowController;
import com.desktoppet.ui.TrayManager;
import javafx.application.Application;
import javafx.application.Platform;
import javafx.fxml.FXMLLoader;
import javafx.scene.Scene;
import javafx.scene.layout.VBox;
import javafx.stage.Stage;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class App extends Application {
    private static final Logger log = LoggerFactory.getLogger(App.class);
    private AppOrchestrator orchestrator;
    private TrayManager trayManager;
    
    @Override
    public void start(Stage primaryStage) throws Exception {
        orchestrator = new AppOrchestrator();

        FXMLLoader loader = new FXMLLoader(getClass().getResource("/fxml/main-window.fxml"));
        VBox root = loader.load();
        MainWindowController controller = loader.getController();

        orchestrator.setUiController(controller);
        orchestrator.startup();
        
        Scene scene = new Scene(root, 400, 500);
        scene.getStylesheets().add(getClass().getResource("/css/style.css").toExternalForm());
        
        primaryStage.setTitle("Desktop Pet Controller");
        primaryStage.setScene(scene);
        primaryStage.setResizable(false);
        primaryStage.show();

        trayManager = new TrayManager(primaryStage);
        trayManager.setOnExitCallback(() -> {
            orchestrator.shutdown();
            Platform.exit();
        });
        trayManager.initialize();

        log.info("Desktop Pet Controller started");
    }

    @Override
    public void stop() {
        if (trayManager != null) {
            trayManager.shutdown();
        }
        if (orchestrator != null) {
            orchestrator.shutdown();
        }
    }
    
    public static void main(String[] args) {
        launch(args);
    }
}
