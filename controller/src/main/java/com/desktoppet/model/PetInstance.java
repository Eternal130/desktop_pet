package com.desktoppet.model;

import javafx.beans.property.*;
import javafx.collections.FXCollections;
import javafx.collections.ObservableList;

import java.time.LocalTime;
import java.time.format.DateTimeFormatter;
import java.util.concurrent.atomic.AtomicInteger;

public class PetInstance {

    private static final AtomicInteger ID_GEN = new AtomicInteger(1);
    private static final DateTimeFormatter TIME_FMT = DateTimeFormatter.ofPattern("HH:mm:ss");
    private static final int MAX_LOG_ENTRIES = 50;

    private final int id;
    private final StringProperty label;
    private final StringProperty model;
    private final StringProperty status;       // "running" | "stopped"
    private final BooleanProperty connected;
    private final DoubleProperty opacity;
    private final StringProperty dragMode;     // "direct" | "physics"
    private final IntegerProperty idleInterval;
    private final IntegerProperty posX;
    private final IntegerProperty posY;
    private final BooleanProperty autoStart;
    private final IntegerProperty targetFps;
    private final StringProperty currentExpression;
    private final StringProperty rendererPath;
    private final ObservableList<String> logs;

    public PetInstance(String label, String model, String status, boolean connected, String rendererPath) {
        this.id = ID_GEN.getAndIncrement();
        this.label = new SimpleStringProperty(label);
        this.model = new SimpleStringProperty(model);
        this.status = new SimpleStringProperty(status);
        this.connected = new SimpleBooleanProperty(connected);
        this.opacity = new SimpleDoubleProperty(1.0);
        this.dragMode = new SimpleStringProperty("direct");
        this.idleInterval = new SimpleIntegerProperty(10);
        this.posX = new SimpleIntegerProperty(1200);
        this.posY = new SimpleIntegerProperty(600);
        this.autoStart = new SimpleBooleanProperty(false);
        this.targetFps = new SimpleIntegerProperty(0);
        this.currentExpression = new SimpleStringProperty("F01");
        this.rendererPath = new SimpleStringProperty(rendererPath);
        this.logs = FXCollections.observableArrayList();
    }

    public PetInstance(String label, String model, String status, boolean connected) {
        this(label, model, status, connected, "");
    }

    public static PetInstance fromInstanceState(InstanceState state) {
        PetInstance instance = new PetInstance(
                state.label(), state.model(), "stopped", false, state.rendererPath());
        instance.setOpacity(state.opacity());
        instance.setDragMode(state.dragMode());
        instance.setIdleInterval(state.idleInterval());
        instance.setPosX(state.posX());
        instance.setPosY(state.posY());
        instance.setAutoStart(state.autoStart());
        instance.setTargetFps(state.targetFps());
        instance.setCurrentExpression(state.currentExpression());
        return instance;
    }

    public InstanceState toInstanceState() {
        return new InstanceState(
                getLabel(), getModel(), getRendererPath(),
                getOpacity(), getDragMode(), getIdleInterval(),
                getPosX(), getPosY(), isAutoStart(), getCurrentExpression(),
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

    public ObservableList<String> getLogs() { return logs; }
}
