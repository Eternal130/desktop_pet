package com.desktoppet;

import javafx.application.Application;
import javafx.scene.Scene;
import javafx.scene.layout.StackPane;
import javafx.stage.Stage;

public class App extends Application {
    @Override
    public void start(Stage primaryStage) {
        primaryStage.setTitle("Desktop Pet Controller");
        primaryStage.setScene(new Scene(new StackPane(), 400, 500));
        primaryStage.show();
    }

    public static void main(String[] args) {
        launch(args);
    }
}
