package com.desktoppet.ui;

import com.desktoppet.core.AppOrchestrator;
import com.desktoppet.core.InstanceConfigManager;
import com.desktoppet.core.ModelScanner;
import com.desktoppet.core.PanelStateManager;
import com.desktoppet.core.Scheduler;
import com.desktoppet.model.Envelope;
import com.desktoppet.model.InstanceState;
import com.desktoppet.model.ModelInfo;
import com.desktoppet.model.PanelState;
import com.desktoppet.model.PetInstance;
import com.desktoppet.network.MessageDispatcher;
import com.desktoppet.network.PetWebSocketServer;
import com.desktoppet.network.Protocol;
import com.desktoppet.util.ProcessManager;
import com.google.gson.JsonObject;
import javafx.collections.FXCollections;
import javafx.collections.ObservableList;
import javafx.fxml.FXML;
import javafx.geometry.Pos;
import javafx.application.Platform;
import javafx.scene.control.Alert;
import javafx.scene.control.Button;
import javafx.scene.control.CheckBox;
import javafx.scene.control.ComboBox;
import javafx.scene.control.Label;
import javafx.scene.control.ListView;
import javafx.scene.control.Slider;
import javafx.scene.control.TextField;
import javafx.scene.control.TextInputDialog;
import javafx.css.PseudoClass;
import javafx.scene.Cursor;
import javafx.scene.input.MouseEvent;
import javafx.scene.layout.GridPane;
import javafx.scene.layout.HBox;
import javafx.scene.layout.Priority;
import javafx.scene.layout.Region;
import javafx.scene.layout.StackPane;
import javafx.scene.layout.VBox;
import javafx.scene.shape.Rectangle;
import javafx.animation.TranslateTransition;
import javafx.util.Duration;
import javafx.stage.Stage;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;

public class MainWindowController {
    private static final Logger log = LoggerFactory.getLogger(MainWindowController.class);
    private static final int WS_PORT = 9001;
    private static final int MAX_RESTART_ATTEMPTS = 5;
    private static final long[] RESTART_BACKOFF_MS = {2000, 4000, 8000, 16000, 30000};

    private final ObservableList<PetInstance> instances = FXCollections.observableArrayList();
    private final Map<Integer, ProcessManager> processManagers = new ConcurrentHashMap<>();
    private final Map<Integer, MessageDispatcher> dispatchers = new ConcurrentHashMap<>();
    private final Map<Integer, Scheduler> schedulers = new ConcurrentHashMap<>();
    private final Map<Integer, Integer> restartAttempts = new ConcurrentHashMap<>();
    private final java.util.Set<Integer> manuallyStopping = ConcurrentHashMap.newKeySet();
    private final InstanceConfigManager instanceConfigManager = new InstanceConfigManager();
    private PanelStateManager panelStateManager;
    private PetWebSocketServer wsServer;

    private static final Map<String, String> MOTION_ICONS = Map.ofEntries(
            Map.entry("idle", "✨"),
            Map.entry("tapbody", "👆"),
            Map.entry("taphead", "🤚"),
            Map.entry("flick", "💨"),
            Map.entry("shake", "🔄"),
            Map.entry("touch", "✋"),
            Map.entry("special", "⭐"),
            Map.entry("pinch", "🤏")
    );
    private static final String DEFAULT_MOTION_ICON = "▶";
    private static final PseudoClass SEG_ACTIVE = PseudoClass.getPseudoClass("seg-active");

    private PetInstance currentInstance;
    private ModelInfo currentModelInfo;
    private boolean updatingUI;
    private double dragOffsetX;
    private double dragOffsetY;

    private static final int RESIZE_MARGIN = 6;
    private boolean resizing;
    private double resizeStartX;
    private double resizeStartY;
    private double resizeStartW;
    private double resizeStartH;
    private double resizeStartStageX;
    private double resizeStartStageY;
    private ResizeDirection resizeDir = ResizeDirection.NONE;

    private enum ResizeDirection {
        NONE, N, S, E, W, NE, NW, SE, SW
    }

    private static final Map<String, String> THEMES = new LinkedHashMap<>();
    static {
        THEMES.put("深紫梦幻", "/css/style.css");
        THEMES.put("樱花浅粉", "/css/theme-sakura.css");
        THEMES.put("赛博霓虹", "/css/theme-cyber.css");
        THEMES.put("暖橘小窝", "/css/theme-warm.css");
        THEMES.put("深海蔚蓝", "/css/theme-ocean.css");
        THEMES.put("终端黑客", "/css/theme-terminal.css");
        THEMES.put("云石浅灰", "/css/theme-stone.css");
    }

    @FXML private ComboBox<String> themeCombo;
    @FXML private HBox titleBar;
    @FXML private VBox instanceListBox;
    @FXML private Button addInstanceBtn;
    @FXML private Label modelNameBig;
    @FXML private Label instanceTagLabel;
    @FXML private Label connectionBadge;
    @FXML private Button toggleStatusBtn;
    @FXML private Label avatarLabel;
    @FXML private Label modelNameCard;
    @FXML private ComboBox<String> modelSelectCombo;
    @FXML private Label statsLabel;
    @FXML private GridPane motionGrid;
    @FXML private Label expressionTitle;
    @FXML private HBox expressionRow;
    @FXML private Slider opacitySlider;
    @FXML private Label opacityValueLabel;
    @FXML private Button dragDirectBtn;
    @FXML private Button dragPhysicsBtn;
    @FXML private Slider idleSlider;
    @FXML private Label idleValueLabel;
    @FXML private TextField posXField;
    @FXML private TextField posYField;
    @FXML private CheckBox autoStartCheck;
    @FXML private ListView<String> logListView;

    @FXML
    public void initialize() {
        themeCombo.setItems(FXCollections.observableArrayList(THEMES.keySet()));
        themeCombo.setValue("深紫梦幻");

        initSharedWebSocketServer();

        opacitySlider.valueProperty().addListener((obs, oldValue, newValue) -> {
            if (updatingUI || currentInstance == null) {
                return;
            }
            double value = newValue.doubleValue();
            currentInstance.setOpacity(value);
            opacityValueLabel.setText(String.format("%.1f", value));
            JsonObject payload = new JsonObject();
            payload.addProperty("opacity", value);
            sendInstanceCommand(currentInstance, "set_opacity", payload);
        });

        idleSlider.valueProperty().addListener((obs, oldValue, newValue) -> {
            if (updatingUI || currentInstance == null) {
                return;
            }
            int seconds = (int) Math.round(newValue.doubleValue());
            currentInstance.setIdleInterval(seconds);
            idleValueLabel.setText(seconds + "s");
            Scheduler scheduler = schedulers.get(currentInstance.getId());
            if (scheduler != null) {
                scheduler.updateInterval(Math.max(1, seconds) * 1000);
            }
        });

        posXField.textProperty().addListener((obs, oldValue, newValue) -> {
            if (updatingUI || currentInstance == null) {
                return;
            }
            try {
                int x = Integer.parseInt(newValue.trim());
                currentInstance.setPosX(x);
                JsonObject payload = new JsonObject();
                payload.addProperty("x", x);
                payload.addProperty("y", currentInstance.getPosY());
                sendInstanceCommand(currentInstance, "set_position", payload);
            } catch (NumberFormatException ignored) {
            }
        });

        posYField.textProperty().addListener((obs, oldValue, newValue) -> {
            if (updatingUI || currentInstance == null) {
                return;
            }
            try {
                int y = Integer.parseInt(newValue.trim());
                currentInstance.setPosY(y);
                JsonObject payload = new JsonObject();
                payload.addProperty("x", currentInstance.getPosX());
                payload.addProperty("y", y);
                sendInstanceCommand(currentInstance, "set_position", payload);
            } catch (NumberFormatException ignored) {
            }
        });

        autoStartCheck.selectedProperty().addListener((obs, oldValue, newValue) -> {
            if (updatingUI || currentInstance == null) {
                return;
            }
            currentInstance.setAutoStart(newValue);
        });

        clipSliderToBounds(opacitySlider);
        clipSliderToBounds(idleSlider);
        setupToggleSwitch(autoStartCheck);

        dragDirectBtn.pseudoClassStateChanged(SEG_ACTIVE, true);
        dragPhysicsBtn.pseudoClassStateChanged(SEG_ACTIVE, false);

        buildMotionGrid(null);
        buildExpressionButtons(null);
        renderSidebar();
    }

    public void setOrchestrator(AppOrchestrator orchestrator) {
    }

    public void setPanelStateManager(PanelStateManager manager) {
        this.panelStateManager = manager;
    }

    public void restoreState() {
        if (panelStateManager == null) {
            return;
        }

        PanelState state = panelStateManager.load();

        if (state.theme() != null && THEMES.containsKey(state.theme())) {
            themeCombo.setValue(state.theme());
            onThemeChanged();
        }

        for (InstanceState instState : state.instances()) {
            String rendererPath = instState.rendererPath();
            if (rendererPath == null || rendererPath.isBlank()) {
                rendererPath = findRendererExecutable();
            }
            if (rendererPath == null) {
                log.warn("Skipping restored instance '{}': no renderer available", instState.label());
                continue;
            }

            PetInstance instance = PetInstance.fromInstanceState(instState);
            if (!instance.getRendererPath().equals(rendererPath)) {
                instance.setRendererPath(rendererPath);
            }

            instanceConfigManager.createConfig(instance.getId(), instance.getLabel(), instance.getRendererPath());
            instance.addLog("◆ 恢复实例「" + instance.getLabel() + "」");
            instances.add(instance);
        }

        if (!instances.isEmpty()) {
            selectInstance(instances.getFirst());

            for (PetInstance instance : instances) {
                if (instance.isAutoStart()) {
                    startInstance(instance);
                }
            }
        }

        renderSidebar();
        log.info("Restored {} instances from panel state", state.instances().size());
    }

    public PanelState buildCurrentState() {
        Stage stage = titleBar.getScene() != null
                ? (Stage) titleBar.getScene().getWindow() : null;

        double panelX = stage != null ? stage.getX() : -1;
        double panelY = stage != null ? stage.getY() : -1;
        double panelW = stage != null ? stage.getWidth() : 1200;
        double panelH = stage != null ? stage.getHeight() : 760;
        String theme = themeCombo.getValue() != null ? themeCombo.getValue() : "深紫梦幻";

        List<InstanceState> instStates = new ArrayList<>();
        for (PetInstance inst : instances) {
            instStates.add(inst.toInstanceState());
        }

        return new PanelState(panelX, panelY, panelW, panelH, theme, instStates);
    }

    private void saveState() {
        if (panelStateManager == null) {
            return;
        }
        panelStateManager.save(buildCurrentState());
    }

    private void selectInstance(PetInstance instance) {
        currentInstance = instance;
        refreshModelList();
        renderSidebar();
        renderDetail();
    }

    private void refreshModelList() {
        currentModelInfo = null;

        if (currentInstance == null) {
            modelSelectCombo.getItems().clear();
            return;
        }

        String rendererPath = currentInstance.getRendererPath();
        List<String> models = ModelScanner.scanAvailableModels(rendererPath);

        updatingUI = true;
        try {
            modelSelectCombo.getItems().setAll(models);

            String currentModel = currentInstance.getModel();
            if (currentModel != null && !currentModel.isEmpty() && models.contains(currentModel)) {
                modelSelectCombo.setValue(currentModel);
            } else if (!models.isEmpty()) {
                modelSelectCombo.setValue(models.getFirst());
                currentInstance.setModel(models.getFirst());
            }
        } finally {
            updatingUI = false;
        }

        refreshModelInfo();
    }

    private void refreshModelInfo() {
        currentModelInfo = null;

        if (currentInstance == null) {
            return;
        }

        String modelName = currentInstance.getModel();
        String rendererPath = currentInstance.getRendererPath();
        if (modelName == null || modelName.isEmpty()) {
            return;
        }

        ModelScanner.getModelInfo(rendererPath, modelName).ifPresent(info -> {
            currentModelInfo = info;
            log.info("Model info loaded for {}: {} motion groups, {} expressions, {} hit areas",
                    modelName,
                    info.motionGroups().size(),
                    info.expressions().size(),
                    info.hitAreas().size());
        });
    }

    private void renderSidebar() {
        instanceListBox.getChildren().clear();

        for (PetInstance inst : instances) {
            VBox card = new VBox(10);
            card.getStyleClass().add("instance-card");
            if (inst == currentInstance) {
                card.getStyleClass().add("instance-card-active");
            }

            HBox top = new HBox(8);
            top.setAlignment(Pos.CENTER_LEFT);

            Label modelLabel = new Label(inst.getModel());
            modelLabel.getStyleClass().add("instance-model-name");

            Region spacer = new Region();
            HBox.setHgrow(spacer, Priority.ALWAYS);

            Region statusDot = new Region();
            statusDot.getStyleClass().add(inst.isRunning() ? "status-dot-running" : "status-dot-stopped");

            top.getChildren().addAll(modelLabel, spacer, statusDot);

            HBox bottom = new HBox(8);
            bottom.setAlignment(Pos.CENTER_LEFT);

            Label tag = new Label(inst.getLabel());
            tag.getStyleClass().add("instance-tag");

            Region bottomSpacer = new Region();
            HBox.setHgrow(bottomSpacer, Priority.ALWAYS);

            Label statusText = new Label(inst.isRunning() ? "运行中" : "已停止");
            statusText.getStyleClass().add("setting-label");

            bottom.getChildren().addAll(tag, bottomSpacer, statusText);

            card.getChildren().addAll(top, bottom);
            card.setOnMouseClicked(event -> selectInstance(inst));
            instanceListBox.getChildren().add(card);
        }
    }

    private void renderDetail() {
        if (currentInstance == null) {
            return;
        }

        updatingUI = true;
        try {
            modelNameBig.setText(currentInstance.getModel());
            instanceTagLabel.setText("✎ " + currentInstance.getLabel());

            connectionBadge.getStyleClass().removeAll("connection-badge-connected", "connection-badge-disconnected");
            if (currentInstance.isConnected()) {
                connectionBadge.getStyleClass().add("connection-badge-connected");
                connectionBadge.setText("● 已连接");
            } else {
                connectionBadge.getStyleClass().add("connection-badge-disconnected");
                connectionBadge.setText("● 已断开");
            }

            toggleStatusBtn.getStyleClass().removeAll("btn-danger-outline", "btn-success-outline");
            if (currentInstance.isRunning()) {
                toggleStatusBtn.setText("停止运行");
                toggleStatusBtn.getStyleClass().add("btn-danger-outline");
            } else {
                toggleStatusBtn.setText("启动运行");
                toggleStatusBtn.getStyleClass().add("btn-success-outline");
            }

            String model = currentInstance.getModel();
            avatarLabel.setText(model.isEmpty() ? "?" : String.valueOf(model.charAt(0)));
            modelNameCard.setText(model);

            if (modelSelectCombo.getItems().contains(model)) {
                modelSelectCombo.setValue(model);
            }

            if (currentModelInfo != null) {
                int motionCount = currentModelInfo.motionGroups().size();
                int exprCount = currentModelInfo.expressions().size();
                int hitCount = currentModelInfo.hitAreas().size();
                statsLabel.setText(motionCount + " 动作组 · " + exprCount + " 表情 · " + hitCount + " 触控区");
            } else {
                statsLabel.setText("— 动作组 · — 表情 · — 触控区");
            }

            expressionTitle.setText("✦ 表情控制 当前: " + currentInstance.getCurrentExpression());

            buildMotionGrid(currentModelInfo);
            buildExpressionButtons(currentModelInfo);
            updateExpressionActiveStyles();

            opacitySlider.setValue(currentInstance.getOpacity());
            opacityValueLabel.setText(String.format("%.1f", currentInstance.getOpacity()));

            boolean isDirect = !"physics".equals(currentInstance.getDragMode());
            dragDirectBtn.pseudoClassStateChanged(SEG_ACTIVE, isDirect);
            dragPhysicsBtn.pseudoClassStateChanged(SEG_ACTIVE, !isDirect);

            idleSlider.setValue(currentInstance.getIdleInterval());
            idleValueLabel.setText(currentInstance.getIdleInterval() + "s");
            posXField.setText(String.valueOf(currentInstance.getPosX()));
            posYField.setText(String.valueOf(currentInstance.getPosY()));
            autoStartCheck.setSelected(currentInstance.isAutoStart());
            logListView.setItems(currentInstance.getLogs());
        } finally {
            updatingUI = false;
        }
    }

    private void buildMotionGrid(ModelInfo info) {
        motionGrid.getChildren().clear();

        if (info == null || info.motionGroups().isEmpty()) {
            Label placeholder = new Label("未检测到动作组");
            placeholder.getStyleClass().add("setting-label");
            motionGrid.add(placeholder, 0, 0);
            return;
        }

        int i = 0;
        for (var entry : info.motionGroups().entrySet()) {
            String name = entry.getKey();
            int count = entry.getValue();
            String icon = MOTION_ICONS.getOrDefault(name.toLowerCase(), DEFAULT_MOTION_ICON);

            VBox btn = new VBox(6);
            btn.getStyleClass().add("motion-btn");
            btn.setAlignment(Pos.CENTER);

            Label iconLabel = new Label(icon);
            iconLabel.getStyleClass().add("motion-icon");

            Label nameLabel = new Label(name);
            nameLabel.getStyleClass().add("motion-name");

            Label countLabel = new Label("×" + count);
            countLabel.getStyleClass().add("motion-count");

            btn.getChildren().addAll(iconLabel, nameLabel, countLabel);
            btn.setOnMouseClicked(event -> onMotionTriggered(name));

            int row = i / 2;
            int col = i % 2;
            motionGrid.add(btn, col, row);
            i++;
        }
    }

    private void buildExpressionButtons(ModelInfo info) {
        expressionRow.getChildren().clear();

        if (info == null || info.expressions().isEmpty()) {
            Label placeholder = new Label("无表情数据");
            placeholder.getStyleClass().add("setting-label");
            expressionRow.getChildren().add(placeholder);
            return;
        }

        for (String exp : info.expressions()) {
            Button button = new Button(exp);
            button.getStyleClass().add("exp-btn");
            button.setOnAction(event -> onExpressionClicked(exp));
            expressionRow.getChildren().add(button);
        }
        updateExpressionActiveStyles();
    }

    private void updateExpressionActiveStyles() {
        if (currentInstance == null) {
            return;
        }
        for (javafx.scene.Node node : expressionRow.getChildren()) {
            if (!(node instanceof Button)) {
                continue;
            }
            Button button = (Button) node;
            button.getStyleClass().remove("exp-btn-active");
            if (button.getText().equals(currentInstance.getCurrentExpression())) {
                button.getStyleClass().add("exp-btn-active");
            }
        }
    }

    private void onExpressionClicked(String expName) {
        if (currentInstance == null) {
            return;
        }
        currentInstance.setCurrentExpression(expName);
        JsonObject payload = new JsonObject();
        payload.addProperty("expression_id", expName);
        sendInstanceCommand(currentInstance, "set_expression", payload);
        currentInstance.addLog("✦ 切换表情: " + expName);
        renderDetail();
    }

    private void onMotionTriggered(String groupName) {
        if (currentInstance == null) {
            return;
        }
        JsonObject payload = new JsonObject();
        payload.addProperty("group", groupName);
        payload.addProperty("index", 0);
        payload.addProperty("priority", 2);
        sendInstanceCommand(currentInstance, "play_motion", payload);
        currentInstance.addLog("✦ 触发动作组: " + groupName);
    }

    public void updateConnectionStatus(boolean connected) {
    }

    public void updateModelName(String name) {
    }

    public void updateIdleInterval(int seconds) {
    }

    public void updateModelInfo(ModelInfo info) {
    }

    public void addActivity(String message) {
    }

    public void addMessageLog(String direction, String type, String action, String summary) {
    }

    private void initSharedWebSocketServer() {
        wsServer = new PetWebSocketServer(WS_PORT);
        wsServer.setReuseAddr(true);

        wsServer.setConnectionCallback((instanceId, connected) -> Platform.runLater(() -> {
            PetInstance instance = findInstanceById(instanceId);
            if (instance == null) return;

            instance.setConnected(connected);
            if (connected) {
                instance.addLog("◇ 渲染引擎已连接");
                restartAttempts.put(instanceId, 0);
            } else {
                instance.addLog("◇ 渲染引擎已断开");
                Scheduler sch = schedulers.get(instanceId);
                if (sch != null) sch.pause();
            }
            renderSidebar();
            if (currentInstance == instance) renderDetail();
        }));

        wsServer.setMessageCallback((instanceId, rawMsg) ->
            Protocol.deserialize(rawMsg).ifPresent(envelope -> {
                PetInstance instance = findInstanceById(instanceId);
                if (instance != null) {
                    Platform.runLater(() ->
                        instance.addLog("← " + envelope.type() + "/" + envelope.action()));
                }
                MessageDispatcher dispatcher = dispatchers.get(instanceId);
                if (dispatcher != null) dispatcher.dispatch(envelope);
            })
        );

        wsServer.start();
        log.info("Shared WebSocket server started on port {}", WS_PORT);
    }

    private PetInstance findInstanceById(int instanceId) {
        return instances.stream()
                .filter(i -> i.getId() == instanceId)
                .findFirst()
                .orElse(null);
    }

    private String findRendererExecutable() {
        Path currentDir = Path.of(".").toAbsolutePath().normalize();
        try (var stream = Files.walk(currentDir, 2)) {
            return stream
                    .filter(Files::isRegularFile)
                    .filter(p -> {
                        String name = p.getFileName().toString().toLowerCase();
                        return name.endsWith(".exe") && name.contains("renderer");
                    })
                    .findFirst()
                    .map(p -> p.toAbsolutePath().toString())
                    .orElse(null);
        } catch (IOException e) {
            log.warn("Failed to scan for renderer executable: {}", e.getMessage());
            return null;
        }
    }

    @FXML
    private void onAddInstance() {
        String rendererPath = findRendererExecutable();
        if (rendererPath == null) {
            Alert alert = new Alert(Alert.AlertType.ERROR);
            alert.setTitle("错误");
            alert.setHeaderText("未找到渲染引擎");
            alert.setContentText("在当前目录下未找到渲染引擎可执行文件（*renderer*.exe），请确保渲染引擎位于当前工作目录中。");
            alert.showAndWait();
            return;
        }

        TextInputDialog dialog = new TextInputDialog("新实例");
        dialog.setTitle("添加实例");
        dialog.setHeaderText("创建新的桌面宠物实例");
        dialog.setContentText("实例名称:");

        dialog.showAndWait().map(String::trim).filter(s -> !s.isEmpty()).ifPresent(label -> {
            PetInstance instance = new PetInstance(label, "", "stopped", false, rendererPath);

            List<String> scannedModels = ModelScanner.scanAvailableModels(instance.getRendererPath());
            if (!scannedModels.isEmpty()) {
                instance.setModel(scannedModels.getFirst());
            }

            instanceConfigManager.createConfig(instance.getId(), instance.getLabel(), instance.getRendererPath());
            instance.addLog("◆ 创建实例「" + instance.getLabel() + "」");
            instance.addLog("◇ 配置文件已创建: " + instanceConfigManager.getConfigPath(instance.getId()));
            instance.addLog("◇ 渲染引擎: " + instance.getRendererPath());
            instance.addLog("◇ 扫描到 " + scannedModels.size() + " 个模型: " + String.join(", ", scannedModels));
            instances.add(instance);
            selectInstance(instance);

            startInstance(instance);
        });
    }

    @FXML
    private void onEditTag() {
        if (currentInstance == null) {
            return;
        }
        TextInputDialog dialog = new TextInputDialog(currentInstance.getLabel());
        dialog.setTitle("编辑标签");
        dialog.setHeaderText("修改实例标签");
        dialog.setContentText("标签:");
        Optional<String> result = dialog.showAndWait();
        result.map(String::trim).filter(s -> !s.isEmpty()).ifPresent(newTag -> {
            currentInstance.setLabel(newTag);
            currentInstance.addLog("◇ 标签更新为: " + newTag);
            renderSidebar();
            renderDetail();
        });
    }

    @FXML
    private void onToggleStatus() {
        if (currentInstance == null) {
            return;
        }
        if (currentInstance.isRunning()) {
            stopInstance(currentInstance);
        } else {
            startInstance(currentInstance);
        }
        renderSidebar();
        renderDetail();
    }

    private void startInstance(PetInstance instance) {
        String rendererPath = instance.getRendererPath();
        if (rendererPath == null || rendererPath.isBlank()) {
            instance.addLog("✖ 启动失败: 未配置渲染引擎路径");
            log.warn("Cannot start instance {}: no renderer path configured", instance.getId());
            return;
        }

        File rendererFile = new File(rendererPath);
        if (!rendererFile.exists()) {
            instance.addLog("✖ 启动失败: 渲染引擎不存在 - " + rendererPath);
            log.warn("Renderer not found at: {}", rendererPath);
            return;
        }

        MessageDispatcher dispatcher = new MessageDispatcher();
        dispatchers.put(instance.getId(), dispatcher);

        registerInstanceEventHandlers(instance, dispatcher);

        ProcessManager pm = new ProcessManager(rendererPath, WS_PORT);

        pm.setShutdownCommandSender(() ->
            wsServer.sendToInstance(instance.getId(),
                    Protocol.serialize(Protocol.createCommand("shutdown", new JsonObject())))
        );

        pm.setExitCallback(exitCode -> Platform.runLater(() -> {
            instance.setStatus("stopped");
            instance.setConnected(false);
            instance.addLog("◆ 渲染引擎退出 (code=" + exitCode + ")");
            wsServer.closeInstance(instance.getId());
            renderSidebar();
            if (currentInstance == instance) {
                renderDetail();
            }

            if (!manuallyStopping.remove(instance.getId())) {
                scheduleRestart(instance);
            }
        }));

        try {
            pm.startRenderer(instance.getId());
            processManagers.put(instance.getId(), pm);
            instance.setStatus("running");
            instance.setConnected(false);
            restartAttempts.put(instance.getId(), 0);
            instance.addLog("◆ 实例「" + instance.getLabel() + "」已启动");
            instance.addLog("◇ 渲染引擎 PID: " + pm.getProcess().map(p -> String.valueOf(p.pid())).orElse("?"));
            instance.addLog("◇ WebSocket 端口: " + WS_PORT);
            log.info("Instance {} started: renderer={}, port={}", instance.getId(), rendererPath, WS_PORT);
        } catch (IOException e) {
            instance.addLog("✖ 启动失败: " + e.getMessage());
            log.error("Failed to start instance {}: {}", instance.getId(), e.getMessage(), e);
        }

        renderSidebar();
        if (currentInstance == instance) {
            renderDetail();
        }
    }

    private void scheduleRestart(PetInstance instance) {
        int attempts = restartAttempts.getOrDefault(instance.getId(), 0);
        if (attempts >= MAX_RESTART_ATTEMPTS) {
            instance.addLog("✖ 渲染引擎重启次数已达上限 (" + MAX_RESTART_ATTEMPTS + ")，请手动重启");
            log.error("Instance {} exceeded max restart attempts", instance.getId());
            return;
        }

        long delayMs = RESTART_BACKOFF_MS[Math.min(attempts, RESTART_BACKOFF_MS.length - 1)];
        restartAttempts.put(instance.getId(), attempts + 1);
        instance.addLog("↺ 将在 " + delayMs / 1000 + "s 后重启 (第" + (attempts + 1) + "次)");

        CompletableFuture.runAsync(() -> {
            try {
                Thread.sleep(delayMs);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                return;
            }
            Platform.runLater(() -> {
                if (!"running".equals(instance.getStatus())) {
                    startInstance(instance);
                }
            });
        });
    }

    private void stopInstance(PetInstance instance) {
        int id = instance.getId();
        manuallyStopping.add(id);

        Scheduler scheduler = schedulers.remove(id);
        if (scheduler != null) scheduler.shutdown();

        dispatchers.remove(id);

        ProcessManager pm = processManagers.remove(id);
        if (pm != null && pm.isRunning()) {
            pm.stopRenderer();
            instance.addLog("◆ 实例「" + instance.getLabel() + "」已停止");
        }
        wsServer.closeInstance(id);
        instance.setStatus("stopped");
        instance.setConnected(false);
        restartAttempts.remove(id);
        log.info("Instance {} stopped", id);
    }

    private void registerInstanceEventHandlers(PetInstance instance,
                                                   MessageDispatcher dispatcher) {
        int id = instance.getId();

        dispatcher.registerEventHandler("ready", envelope -> Platform.runLater(() -> {
            instance.addLog("◇ 渲染引擎就绪");

            String modelName = instance.getModel();
            if (modelName != null && !modelName.isEmpty()) {
                JsonObject payload = new JsonObject();
                payload.addProperty("model_path", modelName);
                Envelope cmd = Protocol.createCommand("load_model", payload);
                wsServer.sendToInstance(id, Protocol.serialize(cmd));
                instance.addLog("→ 加载模型: " + modelName);
            }

            JsonObject posPayload = new JsonObject();
            posPayload.addProperty("x", instance.getPosX());
            posPayload.addProperty("y", instance.getPosY());
            wsServer.sendToInstance(id, Protocol.serialize(
                    Protocol.createCommand("set_position", posPayload)));

            JsonObject opacityPayload = new JsonObject();
            opacityPayload.addProperty("opacity", instance.getOpacity());
            wsServer.sendToInstance(id, Protocol.serialize(
                    Protocol.createCommand("set_opacity", opacityPayload)));

            renderSidebar();
            if (currentInstance == instance) renderDetail();
        }));

        dispatcher.registerEventHandler("model_loaded", envelope -> Platform.runLater(() -> {
            instance.addLog("◇ 模型加载完成");
            startInstanceScheduler(instance);
            renderSidebar();
            if (currentInstance == instance) renderDetail();
        }));

        dispatcher.registerEventHandler("model_load_failed", envelope -> Platform.runLater(() -> {
            instance.addLog("✖ 模型加载失败: " + envelope.payload());
        }));

        dispatcher.registerEventHandler("motion_started", envelope -> Platform.runLater(() -> {
            String group = envelope.payload().has("group")
                    ? envelope.payload().get("group").getAsString() : "?";
            instance.addLog("▶ 动作开始: " + group);
        }));

        dispatcher.registerEventHandler("motion_finished", envelope -> Platform.runLater(() -> {
            String group = envelope.payload().has("group")
                    ? envelope.payload().get("group").getAsString() : "?";
            instance.addLog("■ 动作结束: " + group);
        }));

        dispatcher.registerEventHandler("hit", envelope -> Platform.runLater(() -> {
            String areaId = envelope.payload().has("area_id")
                    ? envelope.payload().get("area_id").getAsString() : "?";
            instance.addLog("👆 点击命中: " + areaId);
        }));

        dispatcher.registerEventHandler("drag_start", envelope ->
                Platform.runLater(() -> instance.addLog("↕ 拖拽开始")));

        dispatcher.registerEventHandler("drag_end", envelope -> Platform.runLater(() -> {
            if (envelope.payload().has("window_x") && envelope.payload().has("window_y")) {
                int wx = envelope.payload().get("window_x").getAsInt();
                int wy = envelope.payload().get("window_y").getAsInt();
                instance.setPosX(wx);
                instance.setPosY(wy);
                instance.addLog("↕ 拖拽结束: (" + wx + ", " + wy + ")");
                if (currentInstance == instance) renderDetail();
            }
        }));

        dispatcher.registerEventHandler("error", envelope -> Platform.runLater(() ->
                instance.addLog("⚠ 渲染引擎错误: " + envelope.payload())));
    }

    private void startInstanceScheduler(PetInstance instance) {
        Scheduler existing = schedulers.remove(instance.getId());
        if (existing != null) existing.shutdown();

        String modelName = instance.getModel();
        String rendererPath = instance.getRendererPath();

        List<String> idleMotions = List.of("Idle");
        var modelInfo = ModelScanner.getModelInfo(rendererPath, modelName);
        if (modelInfo.isPresent() && !modelInfo.get().motionGroups().isEmpty()) {
            idleMotions = List.copyOf(modelInfo.get().motionGroups().keySet());
        }

        Scheduler scheduler = Scheduler.createDefault();
        int intervalMillis = Math.max(1, instance.getIdleInterval()) * 1000;
        final List<String> finalIdleMotions = idleMotions;

        int id = instance.getId();
        scheduler.start(intervalMillis, finalIdleMotions, motionGroup -> {
            if (instance.isConnected()) {
                JsonObject payload = new JsonObject();
                payload.addProperty("group", motionGroup);
                payload.addProperty("index", 0);
                payload.addProperty("priority", 1);
                wsServer.sendToInstance(id, Protocol.serialize(
                        Protocol.createCommand("play_motion", payload)));
            }
        });
        scheduler.resume();

        schedulers.put(id, scheduler);
        instance.addLog("◇ 调度器已启动: 间隔=" + instance.getIdleInterval() + "s");
    }

    private void sendInstanceCommand(PetInstance instance, String action, JsonObject payload) {
        int id = instance.getId();
        if (wsServer != null && wsServer.hasActiveConnection(id)) {
            Envelope cmd = Protocol.createCommand(action, payload);
            wsServer.sendToInstance(id, Protocol.serialize(cmd));
            instance.addLog("→ " + action);
        }
    }

    @FXML
    private void onRestartEngine() {
        if (currentInstance == null) {
            return;
        }
        currentInstance.addLog("↺ 正在重启引擎...");
        stopInstance(currentInstance);
        startInstance(currentInstance);
    }

    @FXML
    private void onThemeChanged() {
        String selected = themeCombo.getValue();
        if (selected == null) {
            return;
        }
        String cssPath = THEMES.get(selected);
        if (cssPath == null) {
            return;
        }
        var scene = themeCombo.getScene();
        if (scene == null) {
            return;
        }
        String cssUrl = getClass().getResource(cssPath).toExternalForm();
        scene.getStylesheets().clear();
        scene.getStylesheets().add(cssUrl);
    }

    @FXML
    private void onModelChanged() {
        if (updatingUI || currentInstance == null) {
            return;
        }
        String selected = modelSelectCombo.getValue();
        if (selected == null || selected.isBlank() || selected.equals(currentInstance.getModel())) {
            return;
        }
        currentInstance.setModel(selected);
        currentInstance.addLog("✦ 模型切换为: " + selected);
        JsonObject payload = new JsonObject();
        payload.addProperty("model_path", selected);
        sendInstanceCommand(currentInstance, "load_model", payload);
        refreshModelInfo();
        renderSidebar();
        renderDetail();
    }

    @FXML
    private void onDragDirect() {
        if (currentInstance == null) {
            return;
        }
        currentInstance.setDragMode("direct");
        renderDetail();
    }

    @FXML
    private void onDragPhysics() {
        if (currentInstance == null) {
            return;
        }
        currentInstance.setDragMode("physics");
        renderDetail();
    }

    @FXML
    private void onClearLog() {
        if (currentInstance == null) {
            return;
        }
        currentInstance.getLogs().clear();
    }

    private void setupToggleSwitch(CheckBox checkBox) {
        StackPane track = new StackPane();
        track.getStyleClass().add("toggle-track");

        Region thumb = new Region();
        thumb.getStyleClass().add("toggle-thumb");
        StackPane.setAlignment(thumb, Pos.CENTER_LEFT);
        track.getChildren().add(thumb);

        double offX = 3;
        double onX = 21;

        checkBox.selectedProperty().addListener((obs, old, selected) -> {
            TranslateTransition tt = new TranslateTransition(Duration.millis(120), thumb);
            tt.setToX(selected ? onX : offX);
            tt.play();
            track.getStyleClass().remove("toggle-track-on");
            if (selected) {
                track.getStyleClass().add("toggle-track-on");
            }
        });

        thumb.setTranslateX(checkBox.isSelected() ? onX : offX);
        if (checkBox.isSelected()) {
            track.getStyleClass().add("toggle-track-on");
        }

        checkBox.setGraphic(track);
    }

    private void clipSliderToBounds(Slider slider) {
        Rectangle clip = new Rectangle();
        clip.widthProperty().bind(slider.widthProperty());
        clip.heightProperty().bind(slider.heightProperty());
        slider.setClip(clip);
    }

    @FXML
    private void onTitleBarPressed(MouseEvent event) {
        Stage stage = (Stage) titleBar.getScene().getWindow();
        dragOffsetX = event.getScreenX() - stage.getX();
        dragOffsetY = event.getScreenY() - stage.getY();
    }

    @FXML
    private void onTitleBarDragged(MouseEvent event) {
        Stage stage = (Stage) titleBar.getScene().getWindow();
        stage.setX(event.getScreenX() - dragOffsetX);
        stage.setY(event.getScreenY() - dragOffsetY);
    }

    @FXML
    private void onMinimize() {
        Stage stage = (Stage) titleBar.getScene().getWindow();
        stage.setIconified(true);
    }

    @FXML
    private void onCloseWindow() {
        saveState();

        for (PetInstance instance : instances) {
            if (instance.isRunning()) {
                stopInstance(instance);
            }
        }
        schedulers.values().forEach(Scheduler::shutdown);
        schedulers.clear();
        dispatchers.clear();
        if (wsServer != null) {
            try {
                wsServer.stop(1000);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
        }
        Platform.exit();
    }

    public void enableWindowResize(Stage stage) {
        var scene = stage.getScene();

        scene.addEventFilter(MouseEvent.MOUSE_MOVED, e -> {
            if (resizing) return;
            ResizeDirection dir = detectEdge(e, stage);
            scene.setCursor(cursorFor(dir));
        });

        scene.addEventFilter(MouseEvent.MOUSE_PRESSED, e -> {
            ResizeDirection dir = detectEdge(e, stage);
            if (dir != ResizeDirection.NONE) {
                resizing = true;
                resizeDir = dir;
                resizeStartX = e.getScreenX();
                resizeStartY = e.getScreenY();
                resizeStartW = stage.getWidth();
                resizeStartH = stage.getHeight();
                resizeStartStageX = stage.getX();
                resizeStartStageY = stage.getY();
                e.consume();
            }
        });

        scene.addEventFilter(MouseEvent.MOUSE_DRAGGED, e -> {
            if (!resizing) return;
            double dx = e.getScreenX() - resizeStartX;
            double dy = e.getScreenY() - resizeStartY;
            double minW = stage.getMinWidth();
            double minH = stage.getMinHeight();

            switch (resizeDir) {
                case E -> stage.setWidth(Math.max(minW, resizeStartW + dx));
                case S -> stage.setHeight(Math.max(minH, resizeStartH + dy));
                case SE -> {
                    stage.setWidth(Math.max(minW, resizeStartW + dx));
                    stage.setHeight(Math.max(minH, resizeStartH + dy));
                }
                case W -> {
                    double newW = Math.max(minW, resizeStartW - dx);
                    stage.setX(resizeStartStageX + resizeStartW - newW);
                    stage.setWidth(newW);
                }
                case N -> {
                    double newH = Math.max(minH, resizeStartH - dy);
                    stage.setY(resizeStartStageY + resizeStartH - newH);
                    stage.setHeight(newH);
                }
                case NW -> {
                    double newW = Math.max(minW, resizeStartW - dx);
                    double newH = Math.max(minH, resizeStartH - dy);
                    stage.setX(resizeStartStageX + resizeStartW - newW);
                    stage.setY(resizeStartStageY + resizeStartH - newH);
                    stage.setWidth(newW);
                    stage.setHeight(newH);
                }
                case NE -> {
                    stage.setWidth(Math.max(minW, resizeStartW + dx));
                    double newH = Math.max(minH, resizeStartH - dy);
                    stage.setY(resizeStartStageY + resizeStartH - newH);
                    stage.setHeight(newH);
                }
                case SW -> {
                    double newW = Math.max(minW, resizeStartW - dx);
                    stage.setX(resizeStartStageX + resizeStartW - newW);
                    stage.setWidth(newW);
                    stage.setHeight(Math.max(minH, resizeStartH + dy));
                }
                default -> {}
            }
            e.consume();
        });

        scene.addEventFilter(MouseEvent.MOUSE_RELEASED, e -> {
            if (resizing) {
                resizing = false;
                resizeDir = ResizeDirection.NONE;
                scene.setCursor(Cursor.DEFAULT);
                e.consume();
            }
        });
    }

    private ResizeDirection detectEdge(MouseEvent e, Stage stage) {
        double x = e.getSceneX();
        double y = e.getSceneY();
        double w = stage.getWidth();
        double h = stage.getHeight();

        boolean top = y < RESIZE_MARGIN;
        boolean bottom = y > h - RESIZE_MARGIN;
        boolean left = x < RESIZE_MARGIN;
        boolean right = x > w - RESIZE_MARGIN;

        if (top && left) return ResizeDirection.NW;
        if (top && right) return ResizeDirection.NE;
        if (bottom && left) return ResizeDirection.SW;
        if (bottom && right) return ResizeDirection.SE;
        if (top) return ResizeDirection.N;
        if (bottom) return ResizeDirection.S;
        if (left) return ResizeDirection.W;
        if (right) return ResizeDirection.E;
        return ResizeDirection.NONE;
    }

    private Cursor cursorFor(ResizeDirection dir) {
        return switch (dir) {
            case N -> Cursor.N_RESIZE;
            case S -> Cursor.S_RESIZE;
            case E -> Cursor.E_RESIZE;
            case W -> Cursor.W_RESIZE;
            case NE -> Cursor.NE_RESIZE;
            case NW -> Cursor.NW_RESIZE;
            case SE -> Cursor.SE_RESIZE;
            case SW -> Cursor.SW_RESIZE;
            default -> Cursor.DEFAULT;
        };
    }
}
