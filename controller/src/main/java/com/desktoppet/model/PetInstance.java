package com.desktoppet.model;

import javafx.beans.property.*;
import javafx.collections.FXCollections;
import javafx.collections.ObservableList;

import java.time.LocalTime;
import java.time.format.DateTimeFormatter;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicInteger;

public class PetInstance {

    private static final AtomicInteger ID_GEN = new AtomicInteger(1);
    private static final DateTimeFormatter TIME_FMT = DateTimeFormatter.ofPattern("HH:mm:ss");
    private static final int MAX_LOG_ENTRIES = 50;

    private final int id;
    private final String configId;
    private final StringProperty label;
    private final StringProperty model;
    private final StringProperty status;       // "running" | "stopped"
    private final BooleanProperty connected;
    private final DoubleProperty opacity;
    private final StringProperty dragMode;     // "direct" | "physics"
    private final IntegerProperty idleInterval;
    private final IntegerProperty posX;
    private final IntegerProperty posY;
    private final IntegerProperty windowWidth;
    private final IntegerProperty windowHeight;
    private final BooleanProperty autoStart;
    private final IntegerProperty targetFps;
    private final StringProperty currentExpression;
    private final StringProperty rendererPath;
    private final DoubleProperty modelScale;
    private final StringProperty voicePack;
    private final DoubleProperty volume;
    private final DoubleProperty layoutOffsetX;
    private final DoubleProperty layoutOffsetY;
    private final DoubleProperty layoutScale;
    private final ObservableList<String> logs;

    public PetInstance(String configId, String label, String model, String status,
                       boolean connected, String rendererPath) {
        this.id = ID_GEN.getAndIncrement();
        this.configId = configId;
        this.label = new SimpleStringProperty(label);
        this.model = new SimpleStringProperty(model);
        this.status = new SimpleStringProperty(status);
        this.connected = new SimpleBooleanProperty(connected);
        this.opacity = new SimpleDoubleProperty(1.0);
        this.dragMode = new SimpleStringProperty("direct");
        this.idleInterval = new SimpleIntegerProperty(10);
        this.posX = new SimpleIntegerProperty(1200);
        this.posY = new SimpleIntegerProperty(600);
        this.windowWidth = new SimpleIntegerProperty(400);
        this.windowHeight = new SimpleIntegerProperty(500);
        this.autoStart = new SimpleBooleanProperty(false);
        this.targetFps = new SimpleIntegerProperty(0);
        this.currentExpression = new SimpleStringProperty("F01");
        this.rendererPath = new SimpleStringProperty(rendererPath);
        this.modelScale = new SimpleDoubleProperty(1.0);
        this.voicePack = new SimpleStringProperty(null);
        this.volume = new SimpleDoubleProperty(1.0);
        this.layoutOffsetX = new SimpleDoubleProperty(0.0);
        this.layoutOffsetY = new SimpleDoubleProperty(0.0);
        this.layoutScale = new SimpleDoubleProperty(1.0);
        this.logs = FXCollections.observableArrayList();
    }

    public PetInstance(String label, String model, String status, boolean connected, String rendererPath) {
        this(UUID.randomUUID().toString(), label, model, status, connected, rendererPath);
    }

    public PetInstance(String label, String model, String status, boolean connected) {
        this(label, model, status, connected, "");
    }

    public static PetInstance fromInstanceConfig(InstanceConfig config) {
        PetInstance instance = new PetInstance(
                config.id(), config.label(), config.modelName(),
                "stopped", false, config.rendererPath());
        instance.setOpacity(config.opacity());
        instance.setDragMode(config.dragMode());
        instance.setIdleInterval(config.idleInterval());
        instance.setPosX(config.windowX());
        instance.setPosY(config.windowY());
        instance.setWindowWidth(config.windowWidth());
        instance.setWindowHeight(config.windowHeight());
        instance.setAutoStart(config.autoStart());
        instance.setTargetFps(config.targetFps());
        instance.setCurrentExpression(config.currentExpression());
        instance.setModelScale(config.modelScale());
        instance.setVoicePack(config.voicePack());
        instance.setVolume(config.volume());
        instance.setLayoutOffsetX(config.layoutOffsetX());
        instance.setLayoutOffsetY(config.layoutOffsetY());
        instance.setLayoutScale(config.layoutScale());
        return instance;
    }

    public InstanceConfig toInstanceConfig() {
        return new InstanceConfig(
                configId,
                getLabel(), getRendererPath(),
                getModel(), getModelScale(),
                getPosX(), getPosY(), getWindowWidth(), getWindowHeight(),
                getOpacity(),
                getDragMode(), getIdleInterval(), getTargetFps(),
                isAutoStart(), getCurrentExpression(),
                getVoicePack(),
                getVolume(),
                getLayoutOffsetX(), getLayoutOffsetY(), getLayoutScale()
        );
    }

    public static PetInstance fromInstanceState(InstanceState state) {
        PetInstance instance = new PetInstance(
                state.label(), state.model(), "stopped", false, state.rendererPath());
        instance.setOpacity(state.opacity());
        instance.setDragMode(state.dragMode());
        instance.setIdleInterval(state.idleInterval());
        instance.setPosX(state.posX());
        instance.setPosY(state.posY());
        instance.setWindowWidth(state.windowWidth());
        instance.setWindowHeight(state.windowHeight());
        instance.setAutoStart(state.autoStart());
        instance.setTargetFps(state.targetFps());
        instance.setCurrentExpression(state.currentExpression());
        return instance;
    }

    public InstanceState toInstanceState() {
        return new InstanceState(
                getLabel(), getModel(), getRendererPath(),
                getOpacity(), getDragMode(), getIdleInterval(),
                getPosX(), getPosY(), getWindowWidth(), getWindowHeight(),
                isAutoStart(), getCurrentExpression(),
                getTargetFps()
        );
    }

    public void addLog(String message) {
        String entry = LocalTime.now().format(TIME_FMT) + "  " + message;
        logs.addFirst(entry);
        if (logs.size() > MAX_LOG_ENTRIES) {
            logs.removeLast();
        }
    }

    public boolean isRunning() {
        return "running".equals(getStatus());
    }

    public int getId() { return id; }
    public String getConfigId() { return configId; }

    public String getLabel() { return label.get(); }
    public void setLabel(String v) { label.set(v); }
    public StringProperty labelProperty() { return label; }

    public String getModel() { return model.get(); }
    public void setModel(String v) { model.set(v); }
    public StringProperty modelProperty() { return model; }

    public String getStatus() { return status.get(); }
    public void setStatus(String v) { status.set(v); }
    public StringProperty statusProperty() { return status; }


    public boolean isConnected() { return connected.get(); }
    public void setConnected(boolean v) { connected.set(v); }
    public BooleanProperty connectedProperty() { return connected; }


    public double getOpacity() { return opacity.get(); }
    public void setOpacity(double v) { opacity.set(v); }
    public DoubleProperty opacityProperty() { return opacity; }


    public double getVolume() { return volume.get(); }
    public void setVolume(double v) { volume.set(v); }
    public DoubleProperty volumeProperty() { return volume; }


    public String getDragMode() { return dragMode.get(); }
    public void setDragMode(String v) { dragMode.set(v); }
    public StringProperty dragModeProperty() { return dragMode; }


    public int getIdleInterval() { return idleInterval.get(); }
    public void setIdleInterval(int v) { idleInterval.set(v); }
    public IntegerProperty idleIntervalProperty() { return idleInterval; }


    public int getPosX() { return posX.get(); }
    public void setPosX(int v) { posX.set(v); }
    public IntegerProperty posXProperty() { return posX; }


    public int getPosY() { return posY.get(); }
    public void setPosY(int v) { posY.set(v); }
    public IntegerProperty posYProperty() { return posY; }

    public int getWindowWidth() { return windowWidth.get(); }
    public void setWindowWidth(int v) { windowWidth.set(v); }
    public IntegerProperty windowWidthProperty() { return windowWidth; }

    public int getWindowHeight() { return windowHeight.get(); }
    public void setWindowHeight(int v) { windowHeight.set(v); }
    public IntegerProperty windowHeightProperty() { return windowHeight; }

    public boolean isAutoStart() { return autoStart.get(); }
    public void setAutoStart(boolean v) { autoStart.set(v); }
    public BooleanProperty autoStartProperty() { return autoStart; }


    public int getTargetFps() { return targetFps.get(); }
    public void setTargetFps(int v) { targetFps.set(v); }
    public IntegerProperty targetFpsProperty() { return targetFps; }

    public String getCurrentExpression() { return currentExpression.get(); }
    public void setCurrentExpression(String v) { currentExpression.set(v); }
    public StringProperty currentExpressionProperty() { return currentExpression; }


    public String getRendererPath() { return rendererPath.get(); }
    public void setRendererPath(String v) { rendererPath.set(v); }
    public StringProperty rendererPathProperty() { return rendererPath; }

    public double getModelScale() { return modelScale.get(); }
    public void setModelScale(double v) { modelScale.set(v); }
    public DoubleProperty modelScaleProperty() { return modelScale; }

    public String getVoicePack() { return voicePack.get(); }
    public void setVoicePack(String v) { voicePack.set(v); }
    public StringProperty voicePackProperty() { return voicePack; }

    public double getLayoutOffsetX() { return layoutOffsetX.get(); }
    public void setLayoutOffsetX(double v) { layoutOffsetX.set(v); }

    public double getLayoutOffsetY() { return layoutOffsetY.get(); }
    public void setLayoutOffsetY(double v) { layoutOffsetY.set(v); }

    public double getLayoutScale() { return layoutScale.get(); }
    public void setLayoutScale(double v) { layoutScale.set(v); }

    public ObservableList<String> getLogs() { return logs; }
}
