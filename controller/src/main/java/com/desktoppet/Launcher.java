package com.desktoppet;

/**
 * Non-Application entry point for fat JAR execution.
 *
 * JavaFX requires the main class in an executable JAR to NOT extend
 * {@link javafx.application.Application} — otherwise the runtime
 * module check fails when launched via {@code java -jar}.
 * This class delegates to {@link App#main(String[])} to work around that.
 */
public class Launcher {
    public static void main(String[] args) {
        App.main(args);
    }
}
