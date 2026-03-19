package com.desktoppet.ui;

import com.desktoppet.core.AppOrchestrator;
import com.desktoppet.core.InstanceConfigManager;
import com.desktoppet.model.ModelInfo;
import com.desktoppet.model.PetInstance;
import com.desktoppet.util.ProcessManager;
import javafx.collections.FXCollections;
import javafx.collections.ObservableList;
import javafx.fxml.FXML;
import javafx.geometry.Insets;
import javafx.geometry.Pos;
import javafx.application.Platform;
import javafx.scene.control.Button;
import javafx.scene.control.ButtonType;
import javafx.scene.control.CheckBox;
import javafx.scene.control.ComboBox;
import javafx.scene.control.Dialog;
import javafx.scene.control.Label;
import javafx.scene.control.ListView;
import javafx.scene.control.Slider;
import javafx.scene.control.TextField;
import javafx.scene.control.TextInputDialog;
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
import javafx.stage.FileChooser;
import javafx.util.Duration;
import javafx.stage.Stage;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.File;
import java.io.IOException;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.ConcurrentHashMap;

public class MainWindowController {
    private static final Logger log = LoggerFactory.getLogger(MainWindowController.class);
    private static final int BASE_WS_PORT = 9001;

    private final ObservableList<PetInstance> instances = FXCollections.observableArrayList();
    private final Map<Integer, ProcessManager> processManagers = new ConcurrentHashMap<>();
    private final InstanceConfigManager instanceConfigManager = new InstanceConfigManager();

    private PetInstance currentInstance;
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

        modelSelectCombo.setItems(FXCollections.observableArrayList("Hiyori", "Mao", "Natori", "Rice"));

        opacitySlider.valueProperty().addListener((obs, oldValue, newValue) -> {
            if (updatingUI || currentInstance == null) {
                return;
            }
            double value = newValue.doubleValue();
            currentInstance.setOpacity(value);
            opacityValueLabel.setText(String.format("%.1f", value));
        });

        idleSlider.valueProperty().addListener((obs, oldValue, newValue) -> {
            if (updatingUI || currentInstance == null) {
                return;
            }
            int seconds = (int) Math.round(newValue.doubleValue());
            currentInstance.setIdleInterval(seconds);
            idleValueLabel.setText(seconds + "s");
        });

        posXField.textProperty().addListener((obs, oldValue, newValue) -> {
            if (updatingUI || currentInstance == null) {
                return;
            }
            try {
                currentInstance.setPosX(Integer.parseInt(newValue.trim()));
            } catch (NumberFormatException ignored) {
            }
        });

        posYField.textProperty().addListener((obs, oldValue, newValue) -> {
            if (updatingUI || currentInstance == null) {
                return;
            }
            try {
                currentInstance.setPosY(Integer.parseInt(newValue.trim()));
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

        buildMotionGrid();
        buildExpressionButtons();
        renderSidebar();
    }

    public void setOrchestrator(AppOrchestrator orchestrator) {
    }

    private void selectInstance(PetInstance instance) {
        currentInstance = instance;
        renderSidebar();
        renderDetail();
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
            modelSelectCombo.setValue(model);
            statsLabel.setText("4 动作组 · 4 表情 · 2 触控区");
            expressionTitle.setText("✦ 表情控制 当前: " + currentInstance.getCurrentExpression());
            updateExpressionActiveStyles();

            opacitySlider.setValue(currentInstance.getOpacity());
            opacityValueLabel.setText(String.format("%.1f", currentInstance.getOpacity()));

            dragDirectBtn.getStyleClass().remove("seg-btn-active");
            dragPhysicsBtn.getStyleClass().remove("seg-btn-active");
            if ("physics".equals(currentInstance.getDragMode())) {
                dragPhysicsBtn.getStyleClass().add("seg-btn-active");
            } else {
                dragDirectBtn.getStyleClass().add("seg-btn-active");
            }

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

    private void buildMotionGrid() {
        motionGrid.getChildren().clear();

        String[][] data = {
                {"Idle", "✨", "×3"},
                {"TapBody", "戳", "×2"},
                {"TapHead", "摸", "×2"},
                {"Flick", "💨", "×1"}
        };

        for (int i = 0; i < data.length; i++) {
            String name = data[i][0];
            String icon = data[i][1];
            String count = data[i][2];

            VBox btn = new VBox(6);
            btn.getStyleClass().add("motion-btn");
            btn.setAlignment(Pos.CENTER);

            Label iconLabel = new Label(icon);
            iconLabel.getStyleClass().add("motion-icon");

            Label nameLabel = new Label(name);
            nameLabel.getStyleClass().add("motion-name");

            Label countLabel = new Label(count);
            countLabel.getStyleClass().add("motion-count");

            btn.getChildren().addAll(iconLabel, nameLabel, countLabel);
            btn.setOnMouseClicked(event -> onMotionTriggered(name));

            int row = i / 2;
            int col = i % 2;
            motionGrid.add(btn, col, row);
        }
    }

    private void buildExpressionButtons() {
        expressionRow.getChildren().clear();
        for (int i = 1; i <= 4; i++) {
            String exp = "F0" + i;
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
        currentInstance.addLog("✦ 切换表情: " + expName);
        renderDetail();
    }

    private void onMotionTriggered(String groupName) {
        if (currentInstance == null) {
            return;
        }
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

    @FXML
    private void onAddInstance() {
        Dialog<PetInstance> dialog = new Dialog<>();
        dialog.setTitle("添加实例");
        dialog.setHeaderText("创建新的桌面宠物实例");
        dialog.getDialogPane().getButtonTypes().addAll(ButtonType.OK, ButtonType.CANCEL);

        GridPane grid = new GridPane();
        grid.setHgap(12);
        grid.setVgap(12);
        grid.setPadding(new Insets(16));

        TextField labelField = new TextField("新实例");
        labelField.setPromptText("实例名称");
        labelField.setPrefWidth(300);

        TextField rendererPathField = new TextField();
        rendererPathField.setPromptText("选择渲染引擎可执行文件");
        rendererPathField.setEditable(false);
        rendererPathField.setPrefWidth(300);

        Button browseBtn = new Button("浏览...");
        browseBtn.setOnAction(e -> {
            FileChooser fileChooser = new FileChooser();
            fileChooser.setTitle("选择渲染引擎程序");
            fileChooser.getExtensionFilters().addAll(
                    new FileChooser.ExtensionFilter("可执行文件", "*.exe"),
                    new FileChooser.ExtensionFilter("所有文件", "*.*")
            );
            File selected = fileChooser.showOpenDialog(dialog.getOwner());
            if (selected != null) {
                rendererPathField.setText(selected.getAbsolutePath());
            }
        });

        HBox rendererRow = new HBox(8, rendererPathField, browseBtn);
        rendererRow.setAlignment(Pos.CENTER_LEFT);
        HBox.setHgrow(rendererPathField, Priority.ALWAYS);

        grid.add(new Label("实例名称:"), 0, 0);
        grid.add(labelField, 1, 0);
        grid.add(new Label("渲染引擎:"), 0, 1);
        grid.add(rendererRow, 1, 1);

        dialog.getDialogPane().setContent(grid);

        Button okBtn = (Button) dialog.getDialogPane().lookupButton(ButtonType.OK);
        okBtn.setDisable(true);
        rendererPathField.textProperty().addListener((obs, oldVal, newVal) ->
                okBtn.setDisable(newVal == null || newVal.isBlank()));

        dialog.setResultConverter(buttonType -> {
            if (buttonType == ButtonType.OK) {
                String label = labelField.getText().trim();
                if (label.isEmpty()) {
                    label = "新实例";
                }
                String rendererPath = rendererPathField.getText().trim();
                PetInstance instance = new PetInstance(label, "", "stopped", false, rendererPath);
                return instance;
            }
            return null;
        });

        dialog.showAndWait().ifPresent(instance -> {
            instanceConfigManager.createConfig(instance.getId(), instance.getLabel(), instance.getRendererPath());
            instance.addLog("◆ 创建实例「" + instance.getLabel() + "」");
            instance.addLog("◇ 配置文件已创建: " + instanceConfigManager.getConfigPath(instance.getId()));
            instance.addLog("◇ 渲染引擎: " + instance.getRendererPath());
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

        int wsPort = BASE_WS_PORT + instance.getId();
        ProcessManager pm = new ProcessManager(rendererPath, wsPort);

        pm.setExitCallback(exitCode -> Platform.runLater(() -> {
            instance.setStatus("stopped");
            instance.setConnected(false);
            instance.addLog("◆ 渲染引擎退出 (code=" + exitCode + ")");
            renderSidebar();
            if (currentInstance == instance) {
                renderDetail();
            }
        }));

        try {
            pm.startRenderer();
            processManagers.put(instance.getId(), pm);
            instance.setStatus("running");
            instance.setConnected(false);
            instance.addLog("◆ 实例「" + instance.getLabel() + "」已启动");
            instance.addLog("◇ 渲染引擎 PID: " + pm.getProcess().map(p -> String.valueOf(p.pid())).orElse("?"));
            instance.addLog("◇ WebSocket 端口: " + wsPort);
            log.info("Instance {} started: renderer={}, port={}", instance.getId(), rendererPath, wsPort);
        } catch (IOException e) {
            instance.addLog("✖ 启动失败: " + e.getMessage());
            log.error("Failed to start instance {}: {}", instance.getId(), e.getMessage(), e);
        }

        renderSidebar();
        if (currentInstance == instance) {
            renderDetail();
        }
    }

    private void stopInstance(PetInstance instance) {
        ProcessManager pm = processManagers.remove(instance.getId());
        if (pm != null && pm.isRunning()) {
            pm.stopRenderer();
            instance.addLog("◆ 实例「" + instance.getLabel() + "」已停止");
        }
        instance.setStatus("stopped");
        instance.setConnected(false);
        log.info("Instance {} stopped", instance.getId());
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
        if (currentInstance == null) {
            return;
        }
        String selected = modelSelectCombo.getValue();
        if (selected == null || selected.isBlank() || selected.equals(currentInstance.getModel())) {
            return;
        }
        currentInstance.setModel(selected);
        currentInstance.addLog("✦ 模型切换为: " + selected);
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
        for (PetInstance instance : instances) {
            if (instance.isRunning()) {
                stopInstance(instance);
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
