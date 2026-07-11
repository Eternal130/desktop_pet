package com.desktoppet.ui;

import com.desktoppet.core.InstanceConfigManager;
import com.desktoppet.core.ModelScanner;
import com.desktoppet.core.PanelStateManager;
import com.desktoppet.core.Scheduler;
import com.desktoppet.core.HitAreaCacheManager;
import com.desktoppet.core.InteractionHandler;
import com.desktoppet.core.MetaMkoParser;
import com.desktoppet.core.MountedBehaviorEngine;
import com.desktoppet.core.VoicePackScanner;
import com.desktoppet.core.ResourceStatsCollector;
import com.desktoppet.model.ControllerStats;
import com.desktoppet.model.Envelope;
import com.desktoppet.model.InstanceConfig;
import com.desktoppet.model.ModelInfo;
import com.desktoppet.model.ModelConfig;
import com.desktoppet.model.PanelConfig;
import com.desktoppet.model.PetInstance;
import com.desktoppet.model.RendererStats;
import com.desktoppet.model.SubtitleStyle;
import com.desktoppet.model.VoicePackInfo;
import com.desktoppet.network.MessageDispatcher;
import com.desktoppet.network.PetWebSocketServer;
import com.desktoppet.network.Protocol;
import com.desktoppet.util.ProcessManager;
import com.desktoppet.util.AutoLaunchManager;
import com.google.gson.JsonObject;
import oshi.SystemInfo;
import javafx.collections.FXCollections;
import javafx.collections.ObservableList;
import javafx.fxml.FXML;
import javafx.fxml.FXMLLoader;
import javafx.geometry.Pos;
import javafx.application.Platform;
import javafx.scene.control.Alert;
import javafx.scene.control.Button;
import javafx.scene.control.ButtonBar;
import javafx.scene.control.ButtonType;
import javafx.scene.control.CheckBox;
import javafx.scene.control.ComboBox;
import javafx.scene.control.Label;
import javafx.scene.control.ListView;
import javafx.scene.control.Slider;
import javafx.scene.control.TextField;
import javafx.scene.control.TextInputDialog;
import javafx.scene.control.ScrollPane;
import javafx.css.PseudoClass;
import javafx.scene.Cursor;
import javafx.scene.Node;
import javafx.scene.input.MouseEvent;
import javafx.scene.layout.GridPane;
import javafx.scene.layout.FlowPane;
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

import java.awt.SystemTray;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.TimeUnit;
import java.util.function.Consumer;

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
    private final HitAreaCacheManager hitAreaCacheManager = new HitAreaCacheManager();
    private final Map<Integer, MountedBehaviorEngine> mountedEngines = new ConcurrentHashMap<>();
    private final Map<Integer, InteractionHandler> interactionHandlers = new ConcurrentHashMap<>();
    private final Map<Integer, Integer> idleMotionCounts = new ConcurrentHashMap<>();
    private PanelStateManager panelStateManager;
    private PetWebSocketServer wsServer;
    private boolean startMinimized = false;
    private String closeAction = "exit";
    private boolean confirmOnExit = false;
    private boolean autoLaunchSystem = false;

    // --- Resource monitor (T4) ---
    // monitor-executor is a daemon single-threaded ScheduledExecutorService.
    // All MonitorDataModel mutations hop through fxRunner so the executor and
    // WS threads never touch UI state directly (project anti-pattern; see
    // .omo/plans/resource-monitor.md T4 for the threading contract).
    private static final long MONITOR_POLL_INTERVAL_MS = 2_000L;
    private static final long MONITOR_STALE_THRESHOLD_MS = 10_000L;
    private ScheduledExecutorService monitorExecutor;
    private ScheduledFuture<?> monitorTask;
    private final ScheduledExecutorService restartExecutor = Executors.newSingleThreadScheduledExecutor(r -> {
        Thread t = new Thread(r, "restart-scheduler");
        t.setDaemon(true);
        return t;
    });
    private ResourceStatsCollector resourceStatsCollector;
    int currentMonitoredInstanceId = -1;
    // Volatile: read by the executor tick, written by the WS handler and
    // instance-switch hook on different threads.
    volatile long lastStatsStateReceivedMs;
    MonitorDataModel monitorModel;
    Consumer<Runnable> fxRunner = r -> Platform.runLater(r);

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
    private Set<String> currentVoicePackGroupNames = Set.of();
    private boolean updatingUI;
    private double dragOffsetX;
    private double dragOffsetY;
    private SettingsPageController settingsPageController;
    private Node settingsPageNode;
    private MonitorPageController monitorPageController;
    private Node monitorPageNode;
    private VBox welcomePane;
    private Label envOpenglStatus;
    private Label envOpenglDetail;
    private Label envVulkanStatus;
    private Label envVulkanDetail;
    private Label envJavaStatus;
    private Label envJavaDetail;
    private Label envCubismStatus;
    private Label envCubismDetail;
    private Label envModelStatus;
    private Label envModelDetail;
    private Label envPortStatus;
    private Label envPortDetail;

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
    @FXML private Button deleteInstanceBtn;
    @FXML private Label avatarLabel;
    @FXML private Label modelNameCard;
    @FXML private ComboBox<String> modelSelectCombo;
    @FXML private Label statsLabel;
    @FXML private FlowPane motionGrid;
    @FXML private Label expressionTitle;
    @FXML private FlowPane expressionRow;
    @FXML private Slider opacitySlider;
    @FXML private Label opacityValueLabel;
    @FXML private Slider volumeSlider;
    @FXML private Label volumeValueLabel;
    @FXML private CheckBox muteCheckBox;
    @FXML private Button dragDirectBtn;
    @FXML private Button dragPhysicsBtn;
    @FXML private Button backendOpenglBtn;
    @FXML private Button backendVulkanBtn;
    @FXML private Slider idleSlider;
    @FXML private Label idleValueLabel;
    @FXML private Button fpsAdaptiveBtn;
    @FXML private Button fpsFixedBtn;
    @FXML private Slider fpsSlider;
    @FXML private Label fpsValueLabel;
    @FXML private TextField posXField;
    @FXML private TextField posYField;
    @FXML private CheckBox autoStartCheck;
    @FXML private CheckBox subtitleAdjustCheck;
    @FXML private ComboBox<String> subtitleStyleCombo;
    @FXML private ComboBox<String> voicePackCombo;
    @FXML private ListView<String> logListView;
    @FXML private StackPane contentStackPane;
    @FXML private ScrollPane mainScrollPane;

    @FXML
    public void initialize() {
        themeCombo.setItems(FXCollections.observableArrayList(THEMES.keySet()));
        themeCombo.setValue("深紫梦幻");

        initSharedWebSocketServer();
        initMonitorInfrastructure();

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

        volumeSlider.valueProperty().addListener((obs, oldValue, newValue) -> {
            if (updatingUI || currentInstance == null) {
                return;
            }
            // Auto-unmute when user drags volume slider
            if (muteCheckBox.isSelected()) {
                muteCheckBox.setSelected(false);
                // muteCheckBox listener fires and sends set_volume{muted:false}
            }
            double value = newValue.doubleValue();
            currentInstance.setVolume(value);
            volumeValueLabel.setText(Math.round(value * 100) + "%");
            JsonObject payload = new JsonObject();
            payload.addProperty("volume", value);
            sendInstanceCommand(currentInstance, "set_volume", payload);
        });

        muteCheckBox.selectedProperty().addListener((obs, oldValue, newValue) -> {
            if (updatingUI || currentInstance == null) {
                return;
            }
            currentInstance.setMuted(newValue);
            JsonObject payload = new JsonObject();
            payload.addProperty("muted", newValue);
            sendInstanceCommand(currentInstance, "set_volume", payload);
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

        fpsSlider.valueProperty().addListener((obs, oldValue, newValue) -> {
            if (updatingUI || currentInstance == null) {
                return;
            }
            int fps = (int) Math.round(newValue.doubleValue());
            fpsValueLabel.setText(String.valueOf(fps));
            if (currentInstance.getTargetFps() > 0) {
                currentInstance.setTargetFps(fps);
                sendFpsCommand(currentInstance, fps);
            }
        });

        fpsAdaptiveBtn.pseudoClassStateChanged(SEG_ACTIVE, true);
        fpsFixedBtn.pseudoClassStateChanged(SEG_ACTIVE, false);
        fpsSlider.setDisable(true);

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

        subtitleAdjustCheck.selectedProperty().addListener((obs, oldValue, val) -> {
            if (updatingUI || currentInstance == null) {
                return;
            }
            int id = currentInstance.getId();
            if (wsServer != null && wsServer.hasActiveConnection(id)) {
                Envelope cmd = Protocol.setSubtitleAdjustMode(val);
                wsServer.sendToInstance(id, Protocol.serialize(cmd));
                currentInstance.addLog(val ? "▶ 字幕调整模式开启" : "▶ 字幕调整模式关闭");
            }
        });

        subtitleStyleCombo.valueProperty().addListener((obs, oldValue, val) -> {
            if (updatingUI || currentInstance == null || val == null) {
                return;
            }
            currentInstance.setSubtitleStylePreset(val);
            saveInstanceConfig(currentInstance);
            int id = currentInstance.getId();
            if (wsServer != null && wsServer.hasActiveConnection(id)) {
                SubtitleStyle style = mapPresetToStyle(val);
                Envelope cmd = Protocol.setSubtitleStyle(style);
                wsServer.sendToInstance(id, Protocol.serialize(cmd));
            }
        });

        clipSliderToBounds(opacitySlider);
        clipSliderToBounds(volumeSlider);
        clipSliderToBounds(idleSlider);
        clipSliderToBounds(fpsSlider);
        setupToggleSwitch(autoStartCheck);
        setupToggleSwitch(muteCheckBox);
        setupToggleSwitch(subtitleAdjustCheck);
        subtitleStyleCombo.getItems().addAll("默认", "阴影", "气泡框", "极简");

        dragDirectBtn.pseudoClassStateChanged(SEG_ACTIVE, true);
        dragPhysicsBtn.pseudoClassStateChanged(SEG_ACTIVE, false);

        buildMotionGrid(null);
        buildExpressionButtons(null);
        renderSidebar();

        // Load settings page
        try {
            FXMLLoader settingsLoader = new FXMLLoader(getClass().getResource("/fxml/settings-page.fxml"));
            settingsPageNode = settingsLoader.load();
            settingsPageController = settingsLoader.getController();
            settingsPageController.setMainWindowController(this);
            contentStackPane.getChildren().add(settingsPageNode);
            settingsPageNode.setVisible(false);
            settingsPageNode.setManaged(false);
        } catch (IOException e) {
            log.error("Failed to load settings page", e);
        }

        // Load monitor page (resource-monitor plan T5)
        try {
            FXMLLoader monitorLoader = new FXMLLoader(getClass().getResource("/fxml/monitor-page.fxml"));
            monitorPageNode = monitorLoader.load();
            monitorPageController = monitorLoader.getController();
            monitorPageController.setMainWindowController(this);
            contentStackPane.getChildren().add(monitorPageNode);
            monitorPageNode.setVisible(false);
            monitorPageNode.setManaged(false);
        } catch (IOException e) {
            log.error("Failed to load monitor page", e);
        }

        // Build welcome pane
        welcomePane = buildWelcomePane();
        contentStackPane.getChildren().add(welcomePane);
        updateContentPaneVisibility();
    }

    private VBox buildWelcomePane() {
        VBox pane = new VBox(24);
        pane.getStyleClass().add("welcome-pane");
        pane.setAlignment(Pos.CENTER);

        Label icon = new Label("✧");
        icon.getStyleClass().add("welcome-icon");

        Label title = new Label("欢迎使用 Live2D 桌面宠物");
        title.getStyleClass().add("welcome-title");

        Label subtitle = new Label("点击下方按钮创建你的第一个桌面宠物实例");
        subtitle.getStyleClass().add("welcome-subtitle");

        Button ctaBtn = new Button("＋ 创建第一个实例");
        ctaBtn.getStyleClass().add("btn-primary");
        ctaBtn.setPrefWidth(220);
        ctaBtn.setOnAction(e -> onAddInstance());

        javafx.scene.control.Separator sep1 = new javafx.scene.control.Separator();

        HBox featuresRow = new HBox(16);
        featuresRow.setAlignment(Pos.CENTER);

        featuresRow.getChildren().addAll(
                buildFeatureCard("🎭", "多实例并行", "同时运行多个桌面宠物"),
                buildFeatureCard("🎨", "7 套主题", "深紫梦幻、樱花浅粉等"),
                buildFeatureCard("🎙", "语音包挂载", "为模型赋予声音与行为")
        );

        javafx.scene.control.Separator sep2 = new javafx.scene.control.Separator();

        VBox envPanel = new VBox(8);
        envPanel.getStyleClass().add("env-panel");

        Label envTitle = new Label("环境检测");
        envTitle.getStyleClass().add("section-title");

        envOpenglStatus = new Label();
        envOpenglDetail = new Label();
        envOpenglDetail.getStyleClass().add("env-detail");
        HBox openglRow = buildEnvRow("OpenGL 渲染引擎", envOpenglStatus, envOpenglDetail);

        envVulkanStatus = new Label();
        envVulkanDetail = new Label();
        envVulkanDetail.getStyleClass().add("env-detail");
        HBox vulkanRow = buildEnvRow("Vulkan 渲染引擎", envVulkanStatus, envVulkanDetail);

        envJavaStatus = new Label();
        envJavaDetail = new Label();
        envJavaDetail.getStyleClass().add("env-detail");
        HBox javaRow = buildEnvRow("Java 运行时", envJavaStatus, envJavaDetail);

        envCubismStatus = new Label();
        envCubismDetail = new Label();
        envCubismDetail.getStyleClass().add("env-detail");
        HBox cubismRow = buildEnvRow("Cubism SDK", envCubismStatus, envCubismDetail);

        envModelStatus = new Label();
        envModelDetail = new Label();
        envModelDetail.getStyleClass().add("env-detail");
        HBox modelRow = buildEnvRow("可用模型", envModelStatus, envModelDetail);

        envPortStatus = new Label();
        envPortDetail = new Label();
        envPortDetail.getStyleClass().add("env-detail");
        HBox portRow = buildEnvRow("WS 端口", envPortStatus, envPortDetail);

        envPanel.getChildren().addAll(envTitle, openglRow, vulkanRow, javaRow, cubismRow, modelRow, portRow);

        pane.getChildren().addAll(icon, title, subtitle, ctaBtn, sep1, featuresRow, sep2, envPanel);
        return pane;
    }

    private VBox buildFeatureCard(String emoji, String titleText, String descText) {
        VBox card = new VBox(6);
        card.getStyleClass().add("feature-card");
        card.setAlignment(Pos.CENTER);

        Label emojiLabel = new Label(emoji);
        emojiLabel.getStyleClass().add("feature-icon");

        Label titleLabel = new Label(titleText);
        titleLabel.getStyleClass().add("feature-title");

        Label descLabel = new Label(descText);
        descLabel.getStyleClass().add("feature-desc");

        card.getChildren().addAll(emojiLabel, titleLabel, descLabel);
        return card;
    }

    private HBox buildEnvRow(String labelText, Label statusLabel, Label detailLabel) {
        HBox row = new HBox(8);
        row.getStyleClass().add("env-row");
        row.setAlignment(Pos.CENTER_LEFT);

        Label label = new Label(labelText);
        label.getStyleClass().add("env-label");

        Region spacer = new Region();
        HBox.setHgrow(spacer, Priority.ALWAYS);

        row.getChildren().addAll(label, spacer, statusLabel, detailLabel);
        return row;
    }

    private void refreshEnvironmentStatus() {
        String openglPath = resolveRendererPath("opengl");
        if (openglPath != null) {
            envOpenglStatus.setText("● 已就绪");
            envOpenglStatus.getStyleClass().setAll("env-status-ok");
            File f = new File(openglPath);
            envOpenglDetail.setText(f.getName());
        } else {
            envOpenglStatus.setText("● 未找到");
            envOpenglStatus.getStyleClass().setAll("env-status-err");
            envOpenglDetail.setText("请确认 desktop-pet-renderer.exe 存在");
        }

        String vulkanPath = resolveRendererPath("vulkan");
        if (vulkanPath != null) {
            envVulkanStatus.setText("● 已就绪");
            envVulkanStatus.getStyleClass().setAll("env-status-ok");
            File f = new File(vulkanPath);
            envVulkanDetail.setText(f.getName());
        } else {
            envVulkanStatus.setText("● 未找到");
            envVulkanStatus.getStyleClass().setAll("env-status-err");
            envVulkanDetail.setText("请确认 desktop-pet-renderer-vulkan.exe 存在");
        }

        String javaVersion = System.getProperty("java.version");
        String jvmName = System.getProperty("java.vm.name", "");
        if (javaVersion != null) {
            envJavaStatus.setText("● 已就绪");
            envJavaStatus.getStyleClass().setAll("env-status-ok");
            envJavaDetail.setText(jvmName + " " + javaVersion);
        } else {
            envJavaStatus.setText("● 异常");
            envJavaStatus.getStyleClass().setAll("env-status-err");
            envJavaDetail.setText("无法获取 Java 版本信息");
        }

        String primaryRendererPath = openglPath != null ? openglPath : vulkanPath;
        if (primaryRendererPath != null) {
            Path modelsDir = ModelScanner.resolveModelsDir(primaryRendererPath);
            if (modelsDir != null && Files.isDirectory(modelsDir)) {
                try (var stream = Files.list(modelsDir)) {
                    long modelCount = stream
                            .filter(Files::isDirectory)
                            .count();
                    envCubismStatus.setText("● 已就绪");
                    envCubismStatus.getStyleClass().setAll("env-status-ok");
                    envCubismDetail.setText("Models 目录包含 " + modelCount + " 个子目录");
                } catch (IOException e) {
                    envCubismStatus.setText("● 已就绪");
                    envCubismStatus.getStyleClass().setAll("env-status-ok");
                    envCubismDetail.setText(modelsDir.getFileName().toString());
                }
            } else {
                envCubismStatus.setText("● 未找到");
                envCubismStatus.getStyleClass().setAll("env-status-err");
                envCubismDetail.setText("请确认 Resources/Models 目录");
            }

            List<String> models = ModelScanner.scanAvailableModels(primaryRendererPath);
            if (models.isEmpty()) {
                envModelStatus.setText("● 无模型");
                envModelStatus.getStyleClass().setAll("env-status-err");
                envModelDetail.setText("请将模型放入 Resources/Models 目录");
            } else {
                envModelStatus.setText("● " + models.size() + " 个");
                envModelStatus.getStyleClass().setAll("env-status-ok");
                envModelDetail.setText(String.join(", ", models));
            }
        } else {
            envCubismStatus.setText("● —");
            envCubismStatus.getStyleClass().setAll("env-status-err");
            envCubismDetail.setText("");
            envModelStatus.setText("● —");
            envModelStatus.getStyleClass().setAll("env-status-err");
            envModelDetail.setText("");
        }

        if (wsServer != null) {
            envPortStatus.setText("● 已就绪");
            envPortStatus.getStyleClass().setAll("env-status-ok");
            envPortDetail.setText(":" + WS_PORT);
        } else {
            envPortStatus.setText("● 未启动");
            envPortStatus.getStyleClass().setAll("env-status-err");
            envPortDetail.setText(":" + WS_PORT);
        }
    }

    private void updateContentPaneVisibility() {
        boolean hasInstances = !instances.isEmpty();

        if (welcomePane != null) {
            welcomePane.setVisible(!hasInstances);
            welcomePane.setManaged(!hasInstances);
        }
        if (mainScrollPane != null) {
            mainScrollPane.setVisible(hasInstances);
            mainScrollPane.setManaged(hasInstances);
        }
        if (settingsPageNode != null) {
            settingsPageNode.setVisible(false);
            settingsPageNode.setManaged(false);
        }

        if (!hasInstances && welcomePane != null) {
            refreshEnvironmentStatus();
        }
    }

    public void setPanelStateManager(PanelStateManager manager) {
        this.panelStateManager = manager;
    }

    public boolean isStartMinimized() {
        return startMinimized;
    }

    public void restoreState() {
        if (panelStateManager == null) {
            return;
        }

        PanelConfig panelConfig = panelStateManager.load();

        if (panelConfig.theme() != null && THEMES.containsKey(panelConfig.theme())) {
            themeCombo.setValue(panelConfig.theme());
            onThemeChanged();
        }

        Platform.runLater(() -> {
            applyFontSize(panelConfig.fontSize());
            applyPanelOpacity(panelConfig.panelOpacity());
        });

        this.startMinimized = panelConfig.startMinimized();
        this.closeAction = panelConfig.closeAction();
        this.confirmOnExit = panelConfig.confirmOnExit();
        this.autoLaunchSystem = panelConfig.autoLaunchSystem();

        List<InstanceConfig> configs = instanceConfigManager.loadAll(panelConfig.instanceIds());
        for (InstanceConfig instConfig : configs) {
            String rendererPath = instConfig.rendererPath();
            if (rendererPath == null || rendererPath.isBlank()) {
                rendererPath = resolveRendererPath(instConfig.graphicsBackend());
            }
            if (rendererPath == null) {
                log.warn("Skipping restored instance '{}': no renderer available", instConfig.label());
                continue;
            }

            PetInstance instance = PetInstance.fromInstanceConfig(instConfig);
            if (!instance.getRendererPath().equals(rendererPath)) {
                instance.setRendererPath(rendererPath);
            }

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
        log.info("Restored {} instances from panel config", configs.size());
    }

    public PanelConfig buildCurrentPanelConfig() {
        Stage stage = titleBar.getScene() != null
                ? (Stage) titleBar.getScene().getWindow() : null;

        double panelX = stage != null ? stage.getX() : -1;
        double panelY = stage != null ? stage.getY() : -1;
        double panelW = stage != null ? stage.getWidth() : 1200;
        double panelH = stage != null ? stage.getHeight() : 760;
        String theme = themeCombo.getValue() != null ? themeCombo.getValue() : "深紫梦幻";

        List<String> instanceIds = new ArrayList<>();
        for (PetInstance inst : instances) {
            instanceIds.add(inst.getConfigId());
        }

        int fontSize = 13;
        double panelOpacity = 1.0;
        if (contentStackPane != null && contentStackPane.getScene() != null) {
            String style = contentStackPane.getScene().getRoot().getStyle();
            if (style != null && style.contains("-fx-font-size:")) {
                try {
                    String sizeStr = style.replaceAll(".*-fx-font-size:\\s*(\\d+)px.*", "$1");
                    fontSize = Integer.parseInt(sizeStr);
                } catch (NumberFormatException ignored) {}
            }
            try {
                Stage configStage = (Stage) contentStackPane.getScene().getWindow();
                if (configStage != null) {
                    panelOpacity = configStage.getOpacity();
                }
            } catch (Exception ignored) {}
        }
        return new PanelConfig(panelX, panelY, panelW, panelH, theme, fontSize, panelOpacity, instanceIds,
                autoLaunchSystem, startMinimized, closeAction, confirmOnExit);
    }

    private void saveState() {
        if (panelStateManager == null) {
            return;
        }
        panelStateManager.save(buildCurrentPanelConfig());
        for (PetInstance inst : instances) {
            instanceConfigManager.save(inst.toInstanceConfig());
        }
    }

    private void saveInstanceConfig(PetInstance instance) {
        instanceConfigManager.save(instance.toInstanceConfig());
    }

    private void selectInstance(PetInstance instance) {
        currentInstance = instance;
        onMonitoredInstanceChanged(instance);
        if (welcomePane != null) {
            welcomePane.setVisible(false);
            welcomePane.setManaged(false);
        }
        refreshModelList();
        refreshVoicePackList();
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

    private void refreshVoicePackList() {
        if (currentInstance == null) {
            voicePackCombo.getItems().clear();
            return;
        }

        Path voicePacksDir = ModelScanner.resolveVoicePacksDir(currentInstance.getRendererPath());
        List<String> voicePacks = VoicePackScanner.scanAvailableVoicePacks(voicePacksDir);

        updatingUI = true;
        try {
            voicePackCombo.getItems().clear();
            voicePackCombo.getItems().add("(无)");
            voicePackCombo.getItems().addAll(voicePacks);

            String vp = currentInstance.getVoicePack();
            voicePackCombo.setValue(vp != null ? vp : "(无)");
        } finally {
            updatingUI = false;
        }
    }

    @FXML
    private void onVoicePackChanged() {
        if (updatingUI || currentInstance == null) {
            return;
        }
        String selected = voicePackCombo.getValue();
        if (selected == null || "(无)".equals(selected)) {
            currentInstance.setVoicePack(null);
            currentInstance.addLog("✦ 卸载语音包");
        } else {
            currentInstance.setVoicePack(selected);
            currentInstance.addLog("✦ 挂载语音包: " + selected);
        }
        saveInstanceConfig(currentInstance);
        initMountedEngine(currentInstance);
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
        updateContentPaneVisibility();
    }

    private SubtitleStyle mapPresetToStyle(String preset) {
        if (preset == null) return SubtitleStyle.defaultStyle();
        return switch (preset) {
            case "阴影" -> new SubtitleStyle("Microsoft YaHei", 48.0, 0x00FFFFFFL, 0x00000000L, 2.0, 0x00000000L, 3.0, 2, 30);
            case "气泡框" -> new SubtitleStyle("Microsoft YaHei", 48.0, 0x00FFFFFFL, 0x00000000L, 0.0, 0x00000000L, 0.0, 2, 30);
            case "极简" -> new SubtitleStyle("Microsoft YaHei", 48.0, 0x00FFFFFFL, 0x00000000L, 0.0, 0x00000000L, 0.0, 2, 30);
            default -> SubtitleStyle.defaultStyle(); // "默认" = white text, black outline
        };
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

            // Ensure engine exists before reading group names (resolve from disk, no renderer needed)
            int instanceId = currentInstance.getId();
            if (!mountedEngines.containsKey(instanceId) && currentInstance.getVoicePack() != null) {
                initMountedEngine(currentInstance);
            }
            MountedBehaviorEngine currentEngine = mountedEngines.get(instanceId);
            currentVoicePackGroupNames = (currentEngine != null) ? currentEngine.groupNames() : Set.of();

            if (currentModelInfo != null || !currentVoicePackGroupNames.isEmpty()) {
                Set<String> allMotions = new LinkedHashSet<>();
                if (currentModelInfo != null && currentModelInfo.motionGroups() != null) {
                    allMotions.addAll(currentModelInfo.motionGroups().keySet());
                }
                allMotions.addAll(currentVoicePackGroupNames);

                Set<String> allExpressions = new LinkedHashSet<>();
                if (currentModelInfo != null && currentModelInfo.expressions() != null) {
                    allExpressions.addAll(currentModelInfo.expressions());
                }
                allExpressions.addAll(currentVoicePackGroupNames);

                int hitCount = (currentModelInfo != null) ? currentModelInfo.hitAreas().size() : 0;
                statsLabel.setText(allMotions.size() + " 动作组 · " + allExpressions.size() + " 表情 · " + hitCount + " 触控区");
            } else {
                statsLabel.setText("— 动作组 · — 表情 · — 触控区");
            }

            expressionTitle.setText("✦ 表情控制 当前: " + currentInstance.getCurrentExpression());

            buildMotionGrid(currentModelInfo);
            buildExpressionButtons(currentModelInfo);
            updateExpressionActiveStyles();

            opacitySlider.setValue(currentInstance.getOpacity());
            opacityValueLabel.setText(String.format("%.1f", currentInstance.getOpacity()));

            volumeSlider.setValue(currentInstance.getVolume());
            volumeValueLabel.setText(Math.round(currentInstance.getVolume() * 100) + "%");

            muteCheckBox.setSelected(currentInstance.isMuted());

            boolean isDirect = !"physics".equals(currentInstance.getDragMode());
            dragDirectBtn.pseudoClassStateChanged(SEG_ACTIVE, isDirect);
            dragPhysicsBtn.pseudoClassStateChanged(SEG_ACTIVE, !isDirect);

            boolean isOpengl = !"vulkan".equals(currentInstance.getGraphicsBackend());
            backendOpenglBtn.pseudoClassStateChanged(SEG_ACTIVE, isOpengl);
            backendVulkanBtn.pseudoClassStateChanged(SEG_ACTIVE, !isOpengl);

            idleSlider.setValue(currentInstance.getIdleInterval());
            idleValueLabel.setText(currentInstance.getIdleInterval() + "s");

            int targetFps = currentInstance.getTargetFps();
            boolean isAdaptive = targetFps <= 0;
            fpsAdaptiveBtn.pseudoClassStateChanged(SEG_ACTIVE, isAdaptive);
            fpsFixedBtn.pseudoClassStateChanged(SEG_ACTIVE, !isAdaptive);
            fpsSlider.setDisable(isAdaptive);
            fpsSlider.setValue(isAdaptive ? 30 : targetFps);
            fpsValueLabel.setText(isAdaptive ? "30" : String.valueOf(targetFps));

            posXField.setText(String.valueOf(currentInstance.getPosX()));
            posYField.setText(String.valueOf(currentInstance.getPosY()));
            autoStartCheck.setSelected(currentInstance.isAutoStart());

            subtitleAdjustCheck.setSelected(false); // Always start off — user toggles manually
            subtitleStyleCombo.setValue(currentInstance.getSubtitleStylePreset());

            String vp = currentInstance.getVoicePack();
            voicePackCombo.setValue(vp != null ? vp : "(无)");

            logListView.setItems(currentInstance.getLogs());
        } finally {
            updatingUI = false;
        }
    }

    private void buildMotionGrid(ModelInfo info) {
        motionGrid.getChildren().clear();

        Map<String, Integer> mergedGroups = new LinkedHashMap<>();
        if (info != null && info.motionGroups() != null) {
            mergedGroups.putAll(info.motionGroups());
        }
        for (String vpGroup : currentVoicePackGroupNames) {
            if (!mergedGroups.containsKey(vpGroup)) {
                mergedGroups.put(vpGroup, 1);
            }
        }

        if (mergedGroups.isEmpty()) {
            Label placeholder = new Label("未检测到动作组");
            placeholder.getStyleClass().add("setting-label");
            motionGrid.getChildren().add(placeholder);
            return;
        }

        for (var entry : mergedGroups.entrySet()) {
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

            motionGrid.getChildren().add(btn);
        }
    }

    private void buildExpressionButtons(ModelInfo info) {
        expressionRow.getChildren().clear();

        List<String> mergedExpressions = new ArrayList<>();
        if (info != null && info.expressions() != null) {
            mergedExpressions.addAll(info.expressions());
        }
        for (String vpGroup : currentVoicePackGroupNames) {
            if (!mergedExpressions.contains(vpGroup)) {
                mergedExpressions.add(vpGroup);
            }
        }

        if (mergedExpressions.isEmpty()) {
            Label placeholder = new Label("无表情数据");
            placeholder.getStyleClass().add("setting-label");
            expressionRow.getChildren().add(placeholder);
            return;
        }

        for (String exp : mergedExpressions) {
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

        if (currentVoicePackGroupNames.contains(expName)) {
            MountedBehaviorEngine engine = mountedEngines.get(currentInstance.getId());
            if (engine != null) {
                String cmd = engine.buildMotionCommand(expName);
                if (cmd != null) {
                    Scheduler sch = schedulers.get(currentInstance.getId());
                    if (sch != null) sch.pause();
                    sendSerializedCommand(currentInstance, cmd);
                    currentInstance.addLog("✦ 播放语音包动作: " + expName);
                    return;
                }
            }
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

        if (currentVoicePackGroupNames.contains(groupName)) {
            MountedBehaviorEngine engine = mountedEngines.get(currentInstance.getId());
            if (engine != null) {
                String cmd = engine.buildMotionCommand(groupName);
                if (cmd != null) {
                    Scheduler sch = schedulers.get(currentInstance.getId());
                    if (sch != null) sch.pause();
                    sendSerializedCommand(currentInstance, cmd);
                    currentInstance.addLog("✦ 触发语音包动作: " + groupName);
                    return;
                }
            }
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
                    Platform.runLater(() -> {
                        if ("response".equals(envelope.type()) && Boolean.FALSE.equals(envelope.success())) {
                            String errMsg = envelope.errorMessage() != null && !envelope.errorMessage().isEmpty()
                                    ? envelope.errorMessage() : "code=" + envelope.errorCode();
                            instance.addLog("✖ ← response/" + envelope.action() + ": " + errMsg);
                        } else {
                            instance.addLog("← " + envelope.type() + "/" + envelope.action());
                        }
                    });
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

    private static String resolveMotionLabel(Envelope envelope) {
        if (envelope.payload().has("group")) {
            return envelope.payload().get("group").getAsString();
        }
        if (envelope.payload().has("motion_path")) {
            String path = envelope.payload().get("motion_path").getAsString();
            int sep = Math.max(path.lastIndexOf('/'), path.lastIndexOf('\\'));
            return sep >= 0 ? path.substring(sep + 1) : path;
        }
        return "?";
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

    private String resolveRendererPath(String backend) {
        Path currentDir = Path.of(".").toAbsolutePath().normalize();
        String targetName;
        if ("vulkan".equals(backend)) {
            targetName = "desktop-pet-renderer-vulkan";
        } else {
            targetName = "desktop-pet-renderer";
        }

        try (var stream = Files.walk(currentDir, 3)) {
            return stream
                    .filter(Files::isRegularFile)
                    .filter(p -> {
                        String name = p.getFileName().toString().toLowerCase();
                        return name.equals(targetName.toLowerCase() + ".exe")
                            || name.equals(targetName.toLowerCase());
                    })
                    .findFirst()
                    .map(p -> p.toAbsolutePath().toString())
                    .orElse(null);
        } catch (IOException e) {
            log.warn("Failed to resolve renderer path for {}: {}", backend, e.getMessage());
            return null;
        }
    }

    @FXML
    private void onAddInstance() {
        String rendererPath = resolveRendererPath("opengl");
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
            InstanceConfig newConfig = InstanceConfig.create(label, rendererPath);
            PetInstance instance = PetInstance.fromInstanceConfig(newConfig);

            List<String> scannedModels = ModelScanner.scanAvailableModels(instance.getRendererPath());
            if (!scannedModels.isEmpty()) {
                instance.setModel(scannedModels.getFirst());
            }

            saveInstanceConfig(instance);
            instance.addLog("◆ 创建实例「" + instance.getLabel() + "」");
            instance.addLog("◇ 配置文件: " + instanceConfigManager.getConfigPath(instance.getConfigId()));
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

    @FXML
    private void onDeleteInstance() {
        if (currentInstance == null) return;
        deleteInstance(currentInstance);
    }

    private void startInstance(PetInstance instance) {
        String rendererPath = instance.getRendererPath();
        if (rendererPath == null || rendererPath.isBlank()) {
            rendererPath = resolveRendererPath(instance.getGraphicsBackend());
            if (rendererPath != null) {
                instance.setRendererPath(rendererPath);
            }
        }
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
        wsServer.registerToken(instance.getId(), pm.getAuthToken());

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
            pm.startRenderer(
                    instance.getId(),
                    instance.getModel(),
                    instance.getPosX(),
                    instance.getPosY(),
                    instance.getWindowWidth(),
                    instance.getWindowHeight()
            );
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

        restartExecutor.schedule(() -> Platform.runLater(() -> {
            if (instances.stream().noneMatch(i -> i.getId() == instance.getId())) {
                return;
            }
            if (!"running".equals(instance.getStatus())) {
                startInstance(instance);
            }
        }), delayMs, TimeUnit.MILLISECONDS);
    }

    private void stopInstance(PetInstance instance) {
        int id = instance.getId();
        manuallyStopping.add(id);

        Scheduler scheduler = schedulers.remove(id);
        if (scheduler != null) scheduler.shutdown();

        dispatchers.remove(id);

        ProcessManager pm = processManagers.remove(id);
        if (pm != null && pm.isRunning()) {
            pm.setExitCallback(null);
            pm.stopRenderer();
            pm.shutdown();
            instance.addLog("◆ 实例「" + instance.getLabel() + "」已停止");
        }
        wsServer.closeInstance(id);
        wsServer.removeToken(id);
        instance.setStatus("stopped");
        instance.setConnected(false);
        restartAttempts.remove(id);
        mountedEngines.remove(id);
        interactionHandlers.remove(id);
        idleMotionCounts.remove(id);
        log.info("Instance {} stopped", id);
    }

    private void deleteInstance(PetInstance instance) {
        Alert alert = new Alert(Alert.AlertType.CONFIRMATION);
        alert.setTitle("确认删除");
        alert.setHeaderText(null);
        alert.setContentText("确定要删除实例 \"" + instance.getLabel() + "\" 吗？\n\n" +
            "• 实例配置将被永久删除\n" +
            "• 正在运行的渲染进程将被终止\n" +
            "• 此操作不可撤销");
        alert.getDialogPane().lookupButton(ButtonType.CANCEL).requestFocus();
        Optional<ButtonType> result = alert.showAndWait();
        if (result.isEmpty() || result.get() != ButtonType.OK) return;

        if (instance.isRunning()) {
            stopInstance(instance);
        }

        manuallyStopping.remove(instance.getId());

        instanceConfigManager.delete(instance.getConfigId());

        int removedIndex = instances.indexOf(instance);
        instances.remove(instance);

        if (currentInstance == instance) {
            if (!instances.isEmpty()) {
                int newIndex = Math.min(removedIndex, instances.size() - 1);
                selectInstance(instances.get(newIndex));
            } else {
                currentInstance = null;
                updateContentPaneVisibility();
            }
        }

        renderSidebar();

        saveState();
    }

    private void registerInstanceEventHandlers(PetInstance instance,
                                                   MessageDispatcher dispatcher) {
        int id = instance.getId();

        // Resource monitor: register stats_state on every per-instance
        // dispatcher. Only currentMonitoredInstanceId is polled, so only that
        // renderer emits; others are registered-but-idle. Plan T4.
        registerStatsStateHandler(dispatcher);

        InteractionHandler interactionHandler = new InteractionHandler(msg -> {
            if (wsServer != null && wsServer.hasActiveConnection(id)) {
                wsServer.sendToInstance(id, msg);
            }
        });
        interactionHandlers.put(id, interactionHandler);

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

            JsonObject sizePayload = new JsonObject();
            sizePayload.addProperty("width", instance.getWindowWidth());
            sizePayload.addProperty("height", instance.getWindowHeight());
            wsServer.sendToInstance(id, Protocol.serialize(
                    Protocol.createCommand("set_size", sizePayload)));

            JsonObject opacityPayload = new JsonObject();
            opacityPayload.addProperty("opacity", instance.getOpacity());
            wsServer.sendToInstance(id, Protocol.serialize(
                    Protocol.createCommand("set_opacity", opacityPayload)));

            JsonObject fpsPayload = new JsonObject();
            fpsPayload.addProperty("fps", instance.getTargetFps());
            wsServer.sendToInstance(id, Protocol.serialize(
                    Protocol.createCommand("set_fps", fpsPayload)));

            JsonObject volumePayload = new JsonObject();
            volumePayload.addProperty("volume", instance.getVolume());
            volumePayload.addProperty("muted", instance.isMuted());
            wsServer.sendToInstance(id, Protocol.serialize(
                    Protocol.createCommand("set_volume", volumePayload)));

            sendLayout(instance);

            if (wsServer != null && wsServer.hasActiveConnection(id)) {
                Envelope layoutCmd = Protocol.setSubtitleLayout(
                    instance.getSubtitleOffsetX(),
                    instance.getSubtitleOffsetY(),
                    instance.getSubtitleAreaWidth(),
                    instance.getSubtitleAreaHeight(),
                    instance.getSubtitleFontSize()
                );
                wsServer.sendToInstance(id, Protocol.serialize(layoutCmd));

                SubtitleStyle style = mapPresetToStyle(instance.getSubtitleStylePreset());
                Envelope styleCmd = Protocol.setSubtitleStyle(style);
                wsServer.sendToInstance(id, Protocol.serialize(styleCmd));
            }

            renderSidebar();
            if (currentInstance == instance) renderDetail();
        }));

        dispatcher.registerEventHandler("model_loaded", envelope -> Platform.runLater(() -> {
            instance.addLog("◇ 模型加载完成");

            if (envelope.payload().has("model_id")) {
                String modelId = envelope.payload().get("model_id").getAsString();

                if (envelope.payload().has("hit_areas")
                        && envelope.payload().get("hit_areas").isJsonArray()) {
                    List<String> hitAreas = new ArrayList<>();
                    for (com.google.gson.JsonElement item
                            : envelope.payload().getAsJsonArray("hit_areas")) {
                        if (item.isJsonPrimitive()) {
                            hitAreas.add(item.getAsString());
                        }
                    }
                    if (!hitAreas.isEmpty()) {
                        hitAreaCacheManager.updateHitAreas(modelId, hitAreas);
                    }
                }

                sendHitAreasToRenderer(instance, modelId);
            }

            loadModelConfig(instance);
            initMountedEngine(instance);
            startInstanceScheduler(instance);
            renderSidebar();
            if (currentInstance == instance) renderDetail();
        }));

        dispatcher.registerEventHandler("model_load_failed", envelope -> Platform.runLater(() -> {
            instance.addLog("✖ 模型加载失败: " + envelope.payload());
        }));

        dispatcher.registerEventHandler("motion_started", envelope -> Platform.runLater(() -> {
            String label = resolveMotionLabel(envelope);
            instance.addLog("▶ 动作开始: " + label);
        }));

        dispatcher.registerEventHandler("motion_finished", envelope -> Platform.runLater(() -> {
            String label = resolveMotionLabel(envelope);
            instance.addLog("■ 动作结束: " + label);

            boolean isIdle;
            if (envelope.payload().has("group")) {
                String group = envelope.payload().get("group").getAsString();
                isIdle = "Idle".equalsIgnoreCase(group);
            } else if (envelope.payload().has("motion_path")) {
                String path = envelope.payload().get("motion_path").getAsString();
                MountedBehaviorEngine engine = mountedEngines.get(id);
                isIdle = engine != null && engine.isIdleMotionPath(path);
            } else {
                isIdle = true;
            }

            if (!isIdle && instance.isConnected()) {
                Scheduler sch = schedulers.get(id);
                if (sch != null) sch.triggerNow();
            }
        }));

        dispatcher.registerEventHandler("hit", envelope -> Platform.runLater(() -> {
            String areaId = envelope.payload().has("area_id")
                    ? envelope.payload().get("area_id").getAsString() : "?";
            instance.addLog("👆 点击命中: " + areaId);

            MountedBehaviorEngine engine = mountedEngines.get(id);
            if (engine != null && engine.hasGroupForArea(areaId)) {
                MountedBehaviorEngine.BehaviorResult behResult = engine.buildBehaviorCommand(areaId);
                String cmd = behResult != null ? behResult.commandJson() : null;
                if (cmd != null) {
                    Scheduler sch = schedulers.get(id);
                    if (sch != null) sch.pause();
                    wsServer.sendToInstance(id, cmd);
                    instance.addLog("▶ 语音包动作: " + areaId);
                    return;
                }
            }

            InteractionHandler handler = interactionHandlers.get(id);
            if (handler != null) {
                handler.handleHitEvent(envelope);
                if (wsServer != null && wsServer.hasActiveConnection(id)) {
                    Envelope subCmd = Protocol.showSubtitle("？", SubtitleStyle.defaultStyle(), 3000);
                    wsServer.sendToInstance(id, Protocol.serialize(subCmd));
                }
            }
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
                saveInstanceConfig(instance);
                if (currentInstance == instance) renderDetail();
            }
        }));

        dispatcher.registerEventHandler("layout_changed", envelope -> Platform.runLater(() -> {
            double ox = envelope.payload().has("offset_x") ? envelope.payload().get("offset_x").getAsDouble() : 0.0;
            double oy = envelope.payload().has("offset_y") ? envelope.payload().get("offset_y").getAsDouble() : 0.0;
            double sc = envelope.payload().has("scale") ? envelope.payload().get("scale").getAsDouble() : 1.0;
            instance.setLayoutOffsetX(ox);
            instance.setLayoutOffsetY(oy);
            instance.setLayoutScale(sc);
            saveInstanceConfig(instance);
        }));

        dispatcher.registerEventHandler("subtitle_layout_changed", envelope -> Platform.runLater(() -> {
            if (envelope.payload() == null) return;
            double offsetX = envelope.payload().has("offset_x") ? envelope.payload().get("offset_x").getAsDouble() : 0;
            double offsetY = envelope.payload().has("offset_y") ? envelope.payload().get("offset_y").getAsDouble() : 0;
            int areaW = envelope.payload().has("area_width") ? envelope.payload().get("area_width").getAsInt() : 0;
            int areaH = envelope.payload().has("area_height") ? envelope.payload().get("area_height").getAsInt() : 0;
            double fontSize = envelope.payload().has("font_size") ? envelope.payload().get("font_size").getAsDouble() : 48.0;
            instance.setSubtitleOffsetX(offsetX);
            instance.setSubtitleOffsetY(offsetY);
            instance.setSubtitleAreaWidth(areaW);
            instance.setSubtitleAreaHeight(areaH);
            instance.setSubtitleFontSize(fontSize);
            saveInstanceConfig(instance);
        }));

        dispatcher.registerEventHandler("window_resized", envelope -> Platform.runLater(() -> {
            if (envelope.payload().has("window_width") && envelope.payload().has("window_height")) {
                int w = envelope.payload().get("window_width").getAsInt();
                int h = envelope.payload().get("window_height").getAsInt();
                instance.setWindowWidth(w);
                instance.setWindowHeight(h);
                if (envelope.payload().has("window_x") && envelope.payload().has("window_y")) {
                    instance.setPosX(envelope.payload().get("window_x").getAsInt());
                    instance.setPosY(envelope.payload().get("window_y").getAsInt());
                }
                instance.addLog("⇲ 窗口缩放: " + w + "×" + h);
                saveInstanceConfig(instance);
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

        int idleMotionCount = 0;
        var modelInfo = ModelScanner.getModelInfo(rendererPath, modelName);
        if (modelInfo.isPresent()) {
            Integer count = modelInfo.get().motionGroups().get("Idle");
            if (count != null && count > 0) {
                idleMotionCount = count;
            }
        }
        idleMotionCounts.put(instance.getId(), idleMotionCount);

        Scheduler scheduler = Scheduler.createDefault();
        int intervalMillis = Math.max(1, instance.getIdleInterval()) * 1000;

        scheduler.start(intervalMillis, List.of("Idle"), motionGroup -> {
            if (instance.isConnected()) {
                triggerIdleMotion(instance);
            }
        });
        scheduler.resume();

        schedulers.put(instance.getId(), scheduler);
        instance.addLog("◇ 调度器已启动: 间隔=" + instance.getIdleInterval() + "s");
    }

    private void triggerIdleMotion(PetInstance instance) {
        int id = instance.getId();

        MountedBehaviorEngine engine = mountedEngines.get(id);
        if (engine != null) {
            String idleKey = engine.hasGroupForArea("Idle") ? "Idle"
                    : engine.hasGroupForArea("idle") ? "idle" : null;
            if (idleKey != null) {
                String cmd = engine.buildMotionCommand(idleKey);
                if (cmd != null) {
                    wsServer.sendToInstance(id, cmd);
                    return;
                }
            }
        }

        int motionCount = idleMotionCounts.getOrDefault(id, 0);
        if (motionCount > 0) {
            int index = java.util.concurrent.ThreadLocalRandom.current().nextInt(motionCount);
            JsonObject payload = new JsonObject();
            payload.addProperty("group", "Idle");
            payload.addProperty("index", index);
            payload.addProperty("priority", 1);
            wsServer.sendToInstance(id, Protocol.serialize(
                    Protocol.createCommand("play_motion", payload)));
        }
    }

    private void initMountedEngine(PetInstance instance) {
        int id = instance.getId();
        String voicePackName = instance.getVoicePack();
        if (voicePackName == null) {
            mountedEngines.remove(id);
            return;
        }

        VoicePackInfo info = resolveVoicePackInfo(instance.getRendererPath(), voicePackName);
        if (info != null) {
            mountedEngines.put(id, new MountedBehaviorEngine(info));
            log.info("Mounted voice pack '{}' on instance {} model '{}'",
                    voicePackName, id, instance.getModel());
        } else {
            log.warn("Voice pack '{}' not found for instance {} model '{}'",
                    voicePackName, id, instance.getModel());
            mountedEngines.remove(id);
        }
    }

    private VoicePackInfo resolveVoicePackInfo(String rendererPath, String voicePackName) {
        Path voicePacksDir = ModelScanner.resolveVoicePacksDir(rendererPath);
        if (voicePacksDir == null || voicePackName == null) {
            return null;
        }
        Path vpDir = voicePacksDir.resolve(voicePackName);
        if (!Files.isDirectory(vpDir)) {
            return null;
        }
        try {
            return MetaMkoParser.parse(vpDir);
        } catch (Throwable t) {
            log.warn("Failed to parse voice pack '{}': {}", voicePackName, t.getMessage());
            return null;
        }
    }

    private void sendHitAreasToRenderer(PetInstance instance, String modelName) {
        List<String> hitAreas = hitAreaCacheManager.getHitAreas(modelName);

        if (hitAreas.isEmpty()) {
            ModelScanner.getModelInfo(instance.getRendererPath(), modelName).ifPresent(info -> {
                if (!info.hitAreas().isEmpty()) {
                    hitAreaCacheManager.updateHitAreas(modelName, info.hitAreas());
                }
            });
            hitAreas = hitAreaCacheManager.getHitAreas(modelName);
        }

        if (!hitAreas.isEmpty()) {
            JsonObject payload = new JsonObject();
            com.google.gson.JsonArray areasArray = new com.google.gson.JsonArray();
            for (String area : hitAreas) {
                areasArray.add(area);
            }
            payload.add("hit_areas", areasArray);
            sendInstanceCommand(instance, "set_hit_areas", payload);
            log.info("Sent hit areas for instance {} model {}: {}",
                    instance.getId(), modelName, hitAreas);
        }
    }

    private void loadModelConfig(PetInstance instance) {
        String modelName = instance.getModel();
        if (modelName == null || modelName.isEmpty()) {
            return;
        }
        Path modelsDir = ModelScanner.resolveModelsDir(instance.getRendererPath());
        if (modelsDir == null) {
            return;
        }
        InteractionHandler handler = interactionHandlers.get(instance.getId());
        if (handler == null) {
            return;
        }
        Path modelConfigPath = modelsDir.resolve(modelName).resolve("model_config.json");
        if (Files.exists(modelConfigPath)) {
            try {
                String json = Files.readString(modelConfigPath);
                ModelConfig modelConfig = new com.google.gson.Gson().fromJson(json, ModelConfig.class);
                handler.setModelConfig(modelConfig);
                log.info("Loaded model_config.json for instance {} model: {}",
                        instance.getId(), modelName);
            } catch (Exception e) {
                log.warn("Failed to load model_config.json for {}: {}", modelName, e.getMessage());
            }
        }
    }

    private void sendInstanceCommand(PetInstance instance, String action, JsonObject payload) {
        int id = instance.getId();
        if (wsServer != null && wsServer.hasActiveConnection(id)) {
            Envelope cmd = Protocol.createCommand(action, payload);
            wsServer.sendToInstance(id, Protocol.serialize(cmd));
            instance.addLog("→ " + action);
        }
    }

    // 渲染器 load_model 会创建全新的 LAppModel(默认 offset=0/scale=1),任何触发 load_model 的路径之后都必须调用此方法重新应用用户调过的 layout。
    private void sendLayout(PetInstance instance) {
        if (instance.getLayoutOffsetX() != 0.0
                || instance.getLayoutOffsetY() != 0.0
                || instance.getLayoutScale() != 1.0) {
            JsonObject layoutPayload = new JsonObject();
            layoutPayload.addProperty("offset_x", instance.getLayoutOffsetX());
            layoutPayload.addProperty("offset_y", instance.getLayoutOffsetY());
            layoutPayload.addProperty("scale", instance.getLayoutScale());
            sendInstanceCommand(instance, "set_layout", layoutPayload);
        }
    }

    private void sendSerializedCommand(PetInstance instance, String serializedJson) {
        int id = instance.getId();
        if (wsServer != null && wsServer.hasActiveConnection(id)) {
            wsServer.sendToInstance(id, serializedJson);
            instance.addLog("→ play_motion_ext");
        }
    }

    /**
     * Sends a show_subtitle command to the current instance's renderer. Subtitles are
     * time-sensitive — dropped (not cached) when disconnected, matching play_motion.
     */
    public void sendSubtitle(String text, long durationMs) {
        PetInstance instance = currentInstance;
        if (instance == null) {
            return;
        }
        int id = instance.getId();
        if (wsServer != null && wsServer.hasActiveConnection(id)) {
            Envelope cmd = Protocol.showSubtitle(text, SubtitleStyle.defaultStyle(), durationMs);
            wsServer.sendToInstance(id, Protocol.serialize(cmd));
            instance.addLog("→ show_subtitle");
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
        manuallyStopping.remove(currentInstance.getId());
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
        // For non-default themes, load style.css first as base, then theme CSS on top
        if (!"/css/style.css".equals(cssPath)) {
            String baseUrl = getClass().getResource("/css/style.css").toExternalForm();
            scene.getStylesheets().add(baseUrl);
        }
        scene.getStylesheets().add(cssUrl);
        if (settingsPageController != null) {
            settingsPageController.updateThemeSelection(selected);
        }
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
        sendLayout(currentInstance);
        refreshModelInfo();
        refreshVoicePackList();
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
    private void onBackendOpengl() {
        if (currentInstance == null) {
            return;
        }
        currentInstance.setGraphicsBackend("opengl");
        renderDetail();
    }

    @FXML
    private void onBackendVulkan() {
        if (currentInstance == null) {
            return;
        }
        currentInstance.setGraphicsBackend("vulkan");
        renderDetail();
    }

    @FXML
    private void onFpsAdaptive() {
        if (currentInstance == null) {
            return;
        }
        currentInstance.setTargetFps(0);
        fpsSlider.setDisable(true);
        fpsAdaptiveBtn.pseudoClassStateChanged(SEG_ACTIVE, true);
        fpsFixedBtn.pseudoClassStateChanged(SEG_ACTIVE, false);
        sendFpsCommand(currentInstance, 0);
    }

    @FXML
    private void onFpsFixed() {
        if (currentInstance == null) {
            return;
        }
        int fps = (int) Math.round(fpsSlider.getValue());
        currentInstance.setTargetFps(fps);
        fpsSlider.setDisable(false);
        fpsAdaptiveBtn.pseudoClassStateChanged(SEG_ACTIVE, false);
        fpsFixedBtn.pseudoClassStateChanged(SEG_ACTIVE, true);
        sendFpsCommand(currentInstance, fps);
    }

    private void sendFpsCommand(PetInstance instance, int fps) {
        JsonObject payload = new JsonObject();
        payload.addProperty("fps", fps);
        sendInstanceCommand(instance, "set_fps", payload);
    }

    @FXML
    private void onClearLog() {
        if (currentInstance == null) {
            return;
        }
        currentInstance.getLogs().clear();
    }

    public static void setupToggleSwitch(CheckBox checkBox) {
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
        handleCloseRequest();
    }

    public void handleCloseRequest() {
        if ("hide_to_tray".equals(closeAction)) {
            if (!SystemTray.isSupported()) {
                performFullShutdown();
                return;
            }
            primaryStage().hide();
            return;
        }
        if (confirmOnExit) {
            ButtonType exitButton = new ButtonType("退出", ButtonBar.ButtonData.OK_DONE);
            ButtonType cancelButton = new ButtonType("取消", ButtonBar.ButtonData.CANCEL_CLOSE);

            Alert alert = new Alert(Alert.AlertType.CONFIRMATION);
            alert.setTitle("关闭确认");
            alert.setHeaderText(null);
            alert.setContentText("您可以选择退出程序或最小化到系统托盘。");

            ButtonType minimizeButton = null;
            if (SystemTray.isSupported()) {
                minimizeButton = new ButtonType("最小化到托盘", ButtonBar.ButtonData.LEFT);
                alert.getButtonTypes().setAll(exitButton, minimizeButton, cancelButton);
            } else {
                alert.getButtonTypes().setAll(exitButton, cancelButton);
            }

            alert.getDialogPane().lookupButton(cancelButton).requestFocus();

            Optional<ButtonType> result = alert.showAndWait();
            if (result.isEmpty() || result.get() == cancelButton) {
                return;
            }
            if (minimizeButton != null && result.get() == minimizeButton) {
                primaryStage().hide();
                return;
            }
        }
        performFullShutdown();
    }

    public void performFullShutdown() {
        saveState();
        stopMonitorPolling();

        CompletableFuture.runAsync(() -> {
            try {
                for (PetInstance instance : instances) {
                    if (instance.isRunning()) {
                        stopInstance(instance);
                    }
                }
                schedulers.values().forEach(Scheduler::shutdown);
                schedulers.clear();
                dispatchers.clear();
                restartExecutor.shutdownNow();
                if (wsServer != null) {
                    try {
                        wsServer.stop(1000);
                    } catch (InterruptedException e) {
                        Thread.currentThread().interrupt();
                    }
                }
            } catch (Exception e) {
                log.error("Error during shutdown", e);
            } finally {
                Platform.exit();
            }
        });
    }

    // ===== Resource monitor (plan T4) =====

    private void initMonitorInfrastructure() {
        monitorModel = new MonitorDataModel();
        monitorExecutor = Executors.newSingleThreadScheduledExecutor(r -> {
            Thread t = new Thread(r, "monitor-executor");
            t.setDaemon(true);
            return t;
        });
        try {
            resourceStatsCollector = new ResourceStatsCollector(new SystemInfo());
        } catch (RuntimeException e) {
            log.warn("ResourceStatsCollector init failed; controller stats disabled: {}", e.getMessage());
        }
        dispatchers.values().forEach(this::registerStatsStateHandler);
    }

    void registerStatsStateHandler(MessageDispatcher dispatcher) {
        dispatcher.registerEventHandler(Protocol.EVENT_STATS_STATE, this::handleStatsState);
    }

    // WS-server thread entry: parse here, mutate model on the FX thread only.
    private void handleStatsState(Envelope env) {
        try {
            RendererStats rs = RendererStats.fromJson(env.payload());
            lastStatsStateReceivedMs = System.currentTimeMillis();
            fxRunner.accept(() -> {
                if (monitorModel != null) {
                    monitorModel.mergeRenderer(rs);
                    refreshMonitorPageIfVisible();
                }
            });
        } catch (RuntimeException e) {
            log.warn("stats_state parse failed: {}", e.getMessage());
        }
    }

    public void startMonitorPolling() {
        if (monitorExecutor == null || monitorExecutor.isShutdown()) {
            monitorExecutor = Executors.newSingleThreadScheduledExecutor(r -> {
                Thread t = new Thread(r, "monitor-executor");
                t.setDaemon(true);
                return t;
            });
        }
        if (monitorTask != null && !monitorTask.isDone()) {
            monitorTask.cancel(false);
        }
        if (currentInstance != null) {
            currentMonitoredInstanceId = currentInstance.getId();
        }
        lastStatsStateReceivedMs = System.currentTimeMillis();
        monitorTask = monitorExecutor.scheduleAtFixedRate(
                this::monitorTick, 0, MONITOR_POLL_INTERVAL_MS, TimeUnit.MILLISECONDS);
        log.info("Monitor polling started for instance {}", currentMonitoredInstanceId);
    }

    public void stopMonitorPolling() {
        if (monitorExecutor != null) {
            monitorExecutor.shutdownNow();
            log.info("Monitor polling stopped");
        }
    }

    private void monitorTick() {
        try {
            ResourceStatsCollector collector = resourceStatsCollector;
            if (collector != null) {
                ControllerStats cs = collector.collectControllerStats();
                fxRunner.accept(() -> {
                    if (monitorModel != null) {
                        monitorModel.mergeController(cs);
                        refreshMonitorPageIfVisible();
                    }
                });
            }
            int targetId = currentMonitoredInstanceId;
            if (wsServer != null && targetId >= 0) {
                Envelope cmd = Protocol.createCommand(Protocol.ACTION_GET_STATS, new JsonObject());
                wsServer.sendToInstance(targetId, Protocol.serialize(cmd));
            }
            long now = System.currentTimeMillis();
            if (MonitorDataModel.isStaleAt(now, lastStatsStateReceivedMs, MONITOR_STALE_THRESHOLD_MS)) {
                fxRunner.accept(() -> {
                    if (monitorModel != null) {
                        monitorModel.markStale();
                        if (monitorPageNode != null && monitorPageNode.isVisible() && monitorPageController != null) {
                            monitorPageController.markStale();
                        }
                    }
                });
            }
        } catch (RuntimeException e) {
            log.warn("Monitor tick failed: {}", e.getMessage());
        }
    }

    void onMonitoredInstanceChanged(PetInstance instance) {
        currentMonitoredInstanceId = instance != null ? instance.getId() : -1;
        lastStatsStateReceivedMs = System.currentTimeMillis();
        if (monitorModel != null) {
            fxRunner.accept(monitorModel::clearHistory);
        }
        if (monitorPageController != null) {
            fxRunner.accept(monitorPageController::clear);
        }
    }

    private void refreshMonitorPageIfVisible() {
        if (monitorPageNode == null || !monitorPageNode.isVisible()
                || monitorPageController == null || monitorModel == null) {
            return;
        }
        monitorPageController.refresh(monitorModel.getLatestSnapshot());
    }

    void initMonitorForTest() {
        monitorModel = new MonitorDataModel();
        monitorExecutor = Executors.newSingleThreadScheduledExecutor(r -> {
            Thread t = new Thread(r, "monitor-executor");
            t.setDaemon(true);
            return t;
        });
    }

    private Stage primaryStage() {
        return (Stage) titleBar.getScene().getWindow();
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

    public void applyTheme(String themeName) {
        if (themeName == null || !THEMES.containsKey(themeName)) {
            return;
        }
        themeCombo.setValue(themeName);
        onThemeChanged();
        if (settingsPageController != null) {
            settingsPageController.updateThemeSelection(themeName);
        }
    }

    public void applyFontSize(int size) {
        if (contentStackPane != null && contentStackPane.getScene() != null) {
            Platform.runLater(() -> {
                contentStackPane.getScene().getRoot().setStyle(
                        "-fx-font-size: " + size + "px;");
            });
        }
    }

    public void applyPanelOpacity(double opacity) {
        if (contentStackPane != null && contentStackPane.getScene() != null) {
            Platform.runLater(() -> {
                Stage stage = (Stage) contentStackPane.getScene().getWindow();
                stage.setOpacity(opacity);
            });
        }
    }

    public void applyAutoLaunch(boolean enabled) {
        this.autoLaunchSystem = enabled;
        try {
            AutoLaunchManager mgr = new AutoLaunchManager();
            if (enabled) { mgr.enable(); } else { mgr.disable(); }
        } catch (Exception e) {
            log.error("Failed to {} auto-launch", enabled ? "enable" : "disable", e);
        }
        saveState();
    }

    public void applyStartMinimized(boolean enabled) {
        this.startMinimized = enabled;
        saveState();
    }

    public void applyCloseAction(String action) {
        this.closeAction = action;
        saveState();
    }

    public void applyConfirmOnExit(boolean enabled) {
        this.confirmOnExit = enabled;
        saveState();
    }

    public void showInstanceDetail() {
        if (settingsPageNode != null) {
            settingsPageNode.setVisible(false);
            settingsPageNode.setManaged(false);
        }
        if (monitorPageNode != null) {
            monitorPageNode.setVisible(false);
            monitorPageNode.setManaged(false);
        }
        updateContentPaneVisibility();
    }

    @FXML
    private void onOpenSettings() {
        if (settingsPageController == null || settingsPageNode == null) {
            return;
        }
        String currentTheme = themeCombo.getValue();
        PanelConfig config = buildCurrentPanelConfig();
        settingsPageController.loadSettings(currentTheme != null ? currentTheme : "深紫梦幻",
                config.fontSize(), config.panelOpacity(),
                config.autoLaunchSystem(), config.startMinimized(),
                config.closeAction(), config.confirmOnExit());

        mainScrollPane.setVisible(false);
        mainScrollPane.setManaged(false);
        if (welcomePane != null) {
            welcomePane.setVisible(false);
            welcomePane.setManaged(false);
        }
        if (monitorPageNode != null) {
            monitorPageNode.setVisible(false);
            monitorPageNode.setManaged(false);
        }
        settingsPageNode.setVisible(true);
        settingsPageNode.setManaged(true);
    }

    @FXML
    public void onOpenMonitor() {
        if (monitorPageController == null || monitorPageNode == null) {
            return;
        }
        mainScrollPane.setVisible(false);
        mainScrollPane.setManaged(false);
        if (welcomePane != null) {
            welcomePane.setVisible(false);
            welcomePane.setManaged(false);
        }
        if (settingsPageNode != null) {
            settingsPageNode.setVisible(false);
            settingsPageNode.setManaged(false);
        }
        monitorPageNode.setVisible(true);
        monitorPageNode.setManaged(true);
        startMonitorPolling();
        MonitorDataModel model = monitorModel;
        if (model != null && model.getLatestSnapshot() != null) {
            monitorPageController.refresh(model.getLatestSnapshot());
        }
    }
}
