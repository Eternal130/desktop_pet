package com.desktoppet;

import com.desktoppet.ui.MainWindowController;
import javafx.application.Application;
import javafx.fxml.FXMLLoader;
import javafx.scene.Scene;
import javafx.scene.layout.VBox;
import javafx.stage.Stage;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class App extends Application {
    private static final Logger log = LoggerFactory.getLogger(App.class);
    
    @Override
    public void start(Stage primaryStage) throws Exception {
        FXMLLoader loader = new FXMLLoader(getClass().getResource("/fxml/main-window.fxml"));
        VBox root = loader.load();
        MainWindowController controller = loader.getController();
        
        Scene scene = new Scene(root, 400, 500);
        scene.getStylesheets().add(getClass().getResource("/css/style.css").toExternalForm());
        
        primaryStage.setTitle("Desktop Pet Controller");
        primaryStage.setScene(scene);
        primaryStage.setResizable(false);
        primaryStage.show();
        log.info("Desktop Pet Controller started");
    }
    
    public static void main(String[] args) {
        launch(args);
    }
}