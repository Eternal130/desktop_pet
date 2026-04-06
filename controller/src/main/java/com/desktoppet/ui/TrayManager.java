package com.desktoppet.ui;

import javafx.application.Platform;
import javafx.stage.Stage;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import javax.swing.*;
import java.awt.*;
import java.awt.event.MouseAdapter;
import java.awt.event.MouseEvent;

public class TrayManager {
    private static final Logger log = LoggerFactory.getLogger(TrayManager.class);

    private SystemTray tray;
    private TrayIcon trayIcon;
    private final Stage primaryStage;
    private Runnable onExitCallback;
    private Runnable onSettingsCallback;
    private Runnable onRandomMotionCallback;
    private Runnable onTogglePauseCallback;
    private Runnable onCloseRequestCallback;

    public TrayManager(Stage primaryStage) {
        this.primaryStage = primaryStage;
    }

    public void setOnExitCallback(Runnable callback) { this.onExitCallback = callback; }
    public void setOnSettingsCallback(Runnable callback) { this.onSettingsCallback = callback; }
    public void setOnRandomMotionCallback(Runnable callback) { this.onRandomMotionCallback = callback; }
    public void setOnTogglePauseCallback(Runnable callback) { this.onTogglePauseCallback = callback; }
    public void setOnCloseRequestCallback(Runnable callback) { this.onCloseRequestCallback = callback; }

    public boolean initialize() {
        if (!SystemTray.isSupported()) {
            log.warn("SystemTray is not supported on this platform");
            return false;
        }

        SwingUtilities.invokeLater(() -> {
            try {
                tray = SystemTray.getSystemTray();

                Image image = createTrayImage();
                PopupMenu popup = createPopupMenu();

                trayIcon = new TrayIcon(image, "Desktop Pet Controller", popup);
                trayIcon.setImageAutoSize(true);

                trayIcon.addMouseListener(new MouseAdapter() {
                    @Override
                    public void mouseClicked(MouseEvent e) {
                        if (e.getClickCount() == 2) {
                            Platform.runLater(() -> toggleWindowVisibility());
                        }
                    }
                });

                tray.add(trayIcon);
                log.info("System tray initialized");
            } catch (AWTException e) {
                log.error("Failed to add tray icon: {}", e.getMessage());
            }
        });

        Platform.setImplicitExit(false);

        primaryStage.setOnCloseRequest(event -> {
            event.consume();
            if (onCloseRequestCallback != null) {
                Platform.runLater(onCloseRequestCallback);
            } else {
                Platform.runLater(() -> primaryStage.hide());
            }
        });

        return true;
    }

    private PopupMenu createPopupMenu() {
        PopupMenu popup = new PopupMenu();

        MenuItem showHideItem = new MenuItem("Show/Hide");
        showHideItem.addActionListener(e ->
            Platform.runLater(() -> toggleWindowVisibility()));

        MenuItem settingsItem = new MenuItem("Settings");
        settingsItem.addActionListener(e -> {
            if (onSettingsCallback != null) {
                Platform.runLater(onSettingsCallback);
            }
        });

        MenuItem randomMotionItem = new MenuItem("Play Random Motion");
        randomMotionItem.addActionListener(e -> {
            if (onRandomMotionCallback != null) {
                onRandomMotionCallback.run();
            }
        });

        MenuItem togglePauseItem = new MenuItem("Toggle Idle Pause");
        togglePauseItem.addActionListener(e -> {
            if (onTogglePauseCallback != null) {
                onTogglePauseCallback.run();
            }
        });

        popup.add(showHideItem);
        popup.add(settingsItem);
        popup.addSeparator();
        popup.add(randomMotionItem);
        popup.add(togglePauseItem);
        popup.addSeparator();

        MenuItem exitItem = new MenuItem("Exit");
        exitItem.addActionListener(e -> {
            if (onExitCallback != null) {
                onExitCallback.run();
            } else {
                Platform.runLater(() -> Platform.exit());
            }
        });
        popup.add(exitItem);

        return popup;
    }

    private void toggleWindowVisibility() {
        if (primaryStage.isShowing()) {
            primaryStage.hide();
        } else {
            primaryStage.show();
            primaryStage.toFront();
        }
    }

    private Image createTrayImage() {
        java.awt.image.BufferedImage img = new java.awt.image.BufferedImage(
            16, 16, java.awt.image.BufferedImage.TYPE_INT_ARGB);
        Graphics2D g = img.createGraphics();
        g.setColor(new Color(74, 144, 217));
        g.fillOval(0, 0, 15, 15);
        g.dispose();
        return img;
    }

    public void shutdown() {
        if (tray != null && trayIcon != null) {
            SwingUtilities.invokeLater(() -> tray.remove(trayIcon));
        }
    }
}
