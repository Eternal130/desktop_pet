package com.desktoppet.core;

import com.desktoppet.model.Envelope;
import com.desktoppet.model.PetConfig;
import com.desktoppet.model.WindowConfig;
import com.desktoppet.network.MessageDispatcher;
import com.desktoppet.network.PetWebSocketServer;
import com.desktoppet.network.Protocol;
import com.desktoppet.ui.MainWindowController;
import com.desktoppet.util.ProcessManager;
import com.google.gson.JsonObject;
import javafx.application.Platform;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.desktoppet.model.ModelInfo;
import com.desktoppet.model.ModelSettingsConfig;
import com.desktoppet.model.MountConfig;
import com.desktoppet.model.VoicePackInfo;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

public class AppOrchestrator {
    private static final Logger log = LoggerFactory.getLogger(AppOrchestrator.class);
    private static final int WS_PORT = 9000;
    private static final int LEGACY_INSTANCE_ID = 0;

    private final ConfigManager configManager;
    private final HitAreaCacheManager hitAreaCacheManager;
    private final PetStateManager stateManager;
    private final PetWebSocketServer wsServer;
    private final MessageDispatcher dispatcher;
    private final ProcessManager processManager;
    private final InteractionHandler interactionHandler;
    private final Scheduler scheduler;

    private final AtomicBoolean isDragging = new AtomicBoolean(false);
    private final AtomicInteger restartAttempts = new AtomicInteger(0);
    private static final int MAX_RESTART_ATTEMPTS = 5;
    private static final long[] BACKOFF_DELAYS_MS = {2000, 4000, 8000, 16000, 30000};
    private volatile long lastSuccessfulStartTime = 0;
    private static final long STABLE_THRESHOLD_MS = 60_000;
    private final java.util.concurrent.ConcurrentLinkedQueue<String> pendingCriticalCommands
            = new java.util.concurrent.ConcurrentLinkedQueue<>();
    private static final java.util.Set<String> CRITICAL_ACTIONS = java.util.Set.of(
            "load_model", "set_position", "set_opacity"
    );
    private static final Path RENDERER_DIR = Path.of("../renderer/build/bin/desktop-pet-renderer");

    private MountConfigManager mountConfigManager;
    private volatile MountedBehaviorEngine mountedEngine;
    private final Map<String, VoicePackInfo> voicePackInfoCache = new LinkedHashMap<>();
    private final List<String> availableVoicePacks = new ArrayList<>();

    private PetConfig config;
    private MainWindowController uiController;

    public AppOrchestrator() {
        this.configManager = new ConfigManager();
        this.hitAreaCacheManager = new HitAreaCacheManager();
        this.stateManager = new PetStateManager();
        this.wsServer = new PetWebSocketServer(WS_PORT);
        this.dispatcher = new MessageDispatcher();
        this.processManager = new ProcessManager();
        this.interactionHandler = new InteractionHandler(msg -> {
            Optional<Envelope> envelope = Protocol.deserialize(msg);
            if (envelope.isPresent()) {
                sendOrCache(envelope.get().action(), msg);
            } else if (stateManager.getState().connected()) {
                wsServer.sendToInstance(LEGACY_INSTANCE_ID, msg);
            } else {
                log.debug("Discarded malformed command during disconnect");
            }
        });
        this.scheduler = Scheduler.createDefault();
    }

    public void setUiController(MainWindowController controller) {
        this.uiController = controller;
    }

    public void startup() {
        CompletableFuture.runAsync(() -> {
            try {
                doStartup();
            } catch (Exception e) {
                log.error("Startup failed: {}", e.getMessage(), e);
            }
        });
    }

    private void doStartup() throws Exception {
        log.info("Starting Desktop Pet Controller...");

        config = configManager.load();
        log.info("Config loaded: model={}, port={}", config.model().currentModelName(), WS_PORT);

        this.mountConfigManager = new MountConfigManager();
        Path resourcesDir = RENDERER_DIR.resolve("Resources");
        List<String> vpNames = VoicePackScanner.scanAvailableVoicePacks(resourcesDir);
        for (String vpName : vpNames) {
            Path vpDir = resourcesDir.resolve(vpName);
            VoicePackInfo info = MetaMkoParser.parse(vpDir);
            if (info != null) {
                voicePackInfoCache.put(vpName, info);
            }
        }
        availableVoicePacks.clear();
        availableVoicePacks.addAll(voicePackInfoCache.keySet());
        log.info("Discovered {} voice packs: {}", availableVoicePacks.size(), availableVoicePacks);

        wsServer.setReuseAddr(true);
        wsServer.start();
        log.info("WebSocket server started on port {}", WS_PORT);

        registerEventHandlers();

        processManager.setExitCallback(exitCode -> {
            log.warn("Renderer exited with code {}", exitCode);
            stateManager.setConnected(false);
            stateManager.setModelLoaded(false);
            scheduler.pause();
            updateUiConnectionStatus(false);

            if (exitCode != 0) {
                scheduleRestart();
            }
        });

        processManager.startRenderer(LEGACY_INSTANCE_ID);
        log.info("Renderer process started");
    }

    private void registerEventHandlers() {
        dispatcher.registerEventHandler("ready", envelope -> {
            log.info("Renderer ready: {}", envelope.payload());
            stateManager.setConnected(true);
            updateUiConnectionStatus(true);

            String modelName = config.model().currentModelName();
            JsonObject payload = new JsonObject();
            payload.addProperty("model_path", modelName);
            Envelope cmd = Protocol.createCommand("load_model", payload);

            dispatcher.expectResponse(cmd.id(), Duration.ofSeconds(10))
                .thenAccept(response -> {
                    if (Boolean.TRUE.equals(response.success())) {
                        log.info("Model loaded: {}", modelName);
                        lastSuccessfulStartTime = System.currentTimeMillis();
                        restartAttempts.set(0);
                        loadModelConfig(modelName);

                        JsonObject posPayload = new JsonObject();
                        posPayload.addProperty("x", config.window().positionX());
                        posPayload.addProperty("y", config.window().positionY());
                        sendOrCache(
                                "set_position",
                                Protocol.serialize(Protocol.createCommand("set_position", posPayload))
                        );

                        JsonObject fpsPayload = new JsonObject();
                        fpsPayload.addProperty("fps", config.behavior().targetFps());
                        sendOrCache("set_fps",
                                Protocol.serialize(Protocol.createCommand("set_fps", fpsPayload)));

                        startSchedulerForModel(modelName);
                        stateManager.updateModelName(modelName);
                        updateUiModelName(modelName);
                    } else {
                        log.error("Model load failed: {}", response.errorMessage());
                    }
                })
                .exceptionally(t -> {
                    log.error("load_model response timeout or error: {}", t.getMessage());
                    return null;
                });

            sendOrCache("load_model", Protocol.serialize(cmd));
            flushPendingCommands();
        });

        dispatcher.registerEventHandler("model_loaded", envelope -> {
            stateManager.setModelLoaded(true);
            log.debug("model_loaded event received for: {}", envelope.payload());

            if (envelope.payload().has("model_id")) {
                String modelId = envelope.payload().get("model_id").getAsString();

                if (envelope.payload().has("hit_areas") && envelope.payload().get("hit_areas").isJsonArray()) {
                    List<String> hitAreas = new ArrayList<>();
                    for (com.google.gson.JsonElement item : envelope.payload().getAsJsonArray("hit_areas")) {
                        if (item.isJsonPrimitive()) {
                            hitAreas.add(item.getAsString());
                        }
                    }
                    if (!hitAreas.isEmpty()) {
                        hitAreaCacheManager.updateHitAreas(modelId, hitAreas);
                        log.info("Cached {} hit areas for model {} from renderer", hitAreas.size(), modelId);
                    }
                }

                sendHitAreasToRenderer(modelId);
                initMountedEngine(modelId);
            }
        });

        dispatcher.registerEventHandler("model_load_failed", envelope -> {
            log.error("model_load_failed: {}", envelope.payload());
            stateManager.setModelLoaded(false);
        });

        dispatcher.registerEventHandler("motion_started", envelope -> {
            log.debug("motion_started: {}", envelope.payload());
            String group = envelope.payload().has("group") ? envelope.payload().get("group").getAsString() : "?";
            notifyActivity("动作开始: " + group);
            notifyMessageLog("←", "event", "motion_started", group);
        });

        dispatcher.registerEventHandler("motion_finished", envelope -> {
            log.debug("motion_finished: {}", envelope.payload());
            String group = envelope.payload().has("group") ? envelope.payload().get("group").getAsString() : "?";
            notifyActivity("动作结束: " + group);
            notifyMessageLog("←", "event", "motion_finished", group);
        });

        dispatcher.registerEventHandler("hit", envelope -> {
            log.debug("hit event: {}", envelope.payload());
            String areaId = envelope.payload().has("area_id") ? envelope.payload().get("area_id").getAsString() : "?";
            notifyActivity("点击命中: " + areaId);
            notifyMessageLog("←", "event", "hit", areaId);

            MountedBehaviorEngine engine = mountedEngine;
            if (engine != null && engine.hasGroupForArea(areaId)) {
                String cmd = engine.buildMotionCommand(areaId);
                if (cmd != null) {
                    sendOrCache("play_motion_ext", cmd);
                    return;
                }
            }
            interactionHandler.handleHitEvent(envelope);
        });

        dispatcher.registerEventHandler("drag_start", envelope -> {
            isDragging.set(true);
            log.debug("drag_start: {}", envelope.payload());
        });

        dispatcher.registerEventHandler("drag_end", envelope -> {
            if (!isDragging.getAndSet(false)) {
                log.debug("drag_end ignored (no corresponding drag_start)");
                return;
            }

            if (envelope.payload().has("window_x") && envelope.payload().has("window_y")) {
                int wx = envelope.payload().get("window_x").getAsInt();
                int wy = envelope.payload().get("window_y").getAsInt();
                stateManager.updateWindowPosition(wx, wy);
                log.debug("Window position updated: ({}, {})", wx, wy);

                var updatedWindow = new WindowConfig(wx, wy, config.window().opacity());
                config = new PetConfig(updatedWindow, config.model(), config.behavior(), config.system());
                configManager.save(config);
            }
        });

        dispatcher.registerEventHandler("error", envelope -> {
            log.warn("Renderer error event: {}", envelope.payload());
        });

        wsServer.setMessageCallback((instanceId, rawMsg) -> {
            Optional<Envelope> envelope = Protocol.deserialize(rawMsg);
            envelope.ifPresent(env -> {
                notifyMessageLog("←", env.type(), env.action(), "");
                dispatcher.dispatch(env);
            });
        });

        wsServer.setConnectionCallback((instanceId, connected) -> {
            stateManager.setConnected(connected);
            updateUiConnectionStatus(connected);
            if (!connected) {
                scheduler.pause();
                log.info("Renderer disconnected");
            }
        });
    }

    private void startSchedulerForModel(String modelName) {
        Path rendererDir = Path.of("../renderer/build/bin/desktop-pet-renderer");
        Path model3Json = rendererDir.resolve("Resources").resolve(modelName).resolve(modelName + ".model3.json");

        int idleMotionCount = 0;
        var modelInfo = ModelInfoParser.parse(model3Json);
        if (modelInfo.isPresent()) {
            Integer count = modelInfo.get().motionGroups().get("Idle");
            if (count != null && count > 0) {
                idleMotionCount = count;
            }
        }

        final int motionCount = idleMotionCount;
        int intervalMillis = Math.max(1, config.behavior().idleIntervalSeconds()) * 1000;
        scheduler.start(intervalMillis, List.of("Idle"), motionGroup -> {
            if (!stateManager.getState().connected() || !stateManager.getState().modelLoaded()) {
                return;
            }

            // Priority 1: voice pack's Idle group
            MountedBehaviorEngine engine = mountedEngine;
            if (engine != null) {
                String idleKey = engine.hasGroupForArea("Idle") ? "Idle"
                        : engine.hasGroupForArea("idle") ? "idle" : null;
                if (idleKey != null) {
                    String cmd = engine.buildMotionCommand(idleKey);
                    if (cmd != null) {
                        sendOrCache("play_motion_ext", cmd);
                        log.debug("Idle motion from voice pack: group={}", idleKey);
                        return;
                    }
                }
            }

            // Priority 2: model3.json Idle group with random index
            if (motionCount > 0) {
                int index = java.util.concurrent.ThreadLocalRandom.current().nextInt(motionCount);
                JsonObject payload = new JsonObject();
                payload.addProperty("group", "Idle");
                payload.addProperty("index", index);
                payload.addProperty("priority", 1);
                sendOrCache(
                        "play_motion",
                        Protocol.serialize(Protocol.createCommand("play_motion", payload))
                );
                log.debug("Idle motion from model: Idle[{}]", index);
            }
        });
        scheduler.resume();
        log.info("Scheduler started: interval={}ms, idleMotionCount={}", intervalMillis, motionCount);
    }

    private void loadModelConfig(String modelName) {
        Path rendererDir = Path.of("../renderer/build/bin/desktop-pet-renderer");
        Path modelConfigPath = rendererDir.resolve("Resources").resolve(modelName).resolve("model_config.json");

        if (Files.exists(modelConfigPath)) {
            try {
                String json = Files.readString(modelConfigPath);
                com.google.gson.Gson gson = new com.google.gson.Gson();
                com.desktoppet.model.ModelConfig modelConfig = gson.fromJson(json, com.desktoppet.model.ModelConfig.class);
                interactionHandler.setModelConfig(modelConfig);
                log.info("Loaded model_config.json for model: {}", modelName);
            } catch (Exception e) {
                log.warn("Failed to load model_config.json for {}: {}", modelName, e.getMessage());
            }
        } else {
            log.debug("No model_config.json found for {}, using default mappings", modelName);
        }
    }

    private void sendHitAreasToRenderer(String modelName) {
        List<String> hitAreas = hitAreaCacheManager.getHitAreas(modelName);

        if (hitAreas.isEmpty()) {
            Optional<ModelInfo> modelInfo = getModelInfo(modelName);
            if (modelInfo.isPresent() && !modelInfo.get().hitAreas().isEmpty()) {
                hitAreas = modelInfo.get().hitAreas();
                hitAreaCacheManager.updateHitAreas(modelName, hitAreas);
            }
        }

        if (!hitAreas.isEmpty()) {
            JsonObject payload = new JsonObject();
            com.google.gson.JsonArray areasArray = new com.google.gson.JsonArray();
            for (String area : hitAreas) {
                areasArray.add(area);
            }
            payload.add("hit_areas", areasArray);
            sendCommand("set_hit_areas", payload);
            log.info("Sent hit areas for model {}: {}", modelName, hitAreas);
        } else {
            log.warn("No hit areas found for model {} (neither from renderer event nor cache)", modelName);
        }
    }

    private void initMountedEngine(String modelName) {
        MountConfig mc = mountConfigManager.loadForModel(modelName);
        if (mc.voicePackName() != null && voicePackInfoCache.containsKey(mc.voicePackName())) {
            VoicePackInfo info = voicePackInfoCache.get(mc.voicePackName());
            mountedEngine = new MountedBehaviorEngine(info);
            log.info("Mounted voice pack '{}' on model '{}'", mc.voicePackName(), modelName);
        } else {
            mountedEngine = null;
            if (mc.voicePackName() != null) {
                log.warn("Voice pack '{}' not found in cache for model '{}'", mc.voicePackName(), modelName);
            }
        }
    }

    private void sendOrCache(String action, String serializedEnvelope) {
        if (stateManager.getState().connected()) {
            wsServer.sendToInstance(LEGACY_INSTANCE_ID, serializedEnvelope);
        } else if (CRITICAL_ACTIONS.contains(action)) {
            pendingCriticalCommands.offer(serializedEnvelope);
            log.debug("Cached critical command: {}", action);
        } else {
            log.debug("Discarded non-critical command during disconnect: {}", action);
        }
    }

    private void flushPendingCommands() {
        String cmd;
        while ((cmd = pendingCriticalCommands.poll()) != null) {
            wsServer.sendToInstance(LEGACY_INSTANCE_ID, cmd);
            log.debug("Resent cached command");
        }
    }

    private void scheduleRestart() {
        int attempts = restartAttempts.get();

        if (lastSuccessfulStartTime > 0
                && System.currentTimeMillis() - lastSuccessfulStartTime > STABLE_THRESHOLD_MS) {
            restartAttempts.set(0);
            attempts = 0;
        }

        if (attempts >= MAX_RESTART_ATTEMPTS) {
            log.error("Renderer crashed {} times, giving up. Manual restart required.", attempts);
            updateUiConnectionStatus(false);
            return;
        }

        long delayMs = BACKOFF_DELAYS_MS[Math.min(attempts, BACKOFF_DELAYS_MS.length - 1)];
        log.info(
                "Scheduling renderer restart in {}ms (attempt {}/{})",
                delayMs,
                attempts + 1,
                MAX_RESTART_ATTEMPTS
        );

        CompletableFuture.runAsync(() -> {
            try {
                Thread.sleep(delayMs);
                restartAttempts.incrementAndGet();
                doRestartRenderer();
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            } catch (Exception e) {
                log.error("Restart failed: {}", e.getMessage(), e);
            }
        });
    }

    private void doRestartRenderer() throws Exception {
        log.info("Restarting renderer...");
        stateManager.setConnected(false);
        stateManager.setModelLoaded(false);
        processManager.startRenderer(LEGACY_INSTANCE_ID);
        log.info("Renderer restarted, waiting for ready event...");
    }

    public void shutdown() {
        log.info("Shutting down...");
        scheduler.shutdown();

        if (config != null) {
            var state = stateManager.getState();
            var updatedWindow = new WindowConfig(state.windowX(), state.windowY(), config.window().opacity());
            configManager.save(new PetConfig(updatedWindow, config.model(), config.behavior(), config.system()));
        }

        processManager.setShutdownCommandSender(() ->
            wsServer.sendToInstance(LEGACY_INSTANCE_ID, Protocol.serialize(Protocol.createCommand("shutdown", new JsonObject())))
        );
        processManager.stopRenderer();

        try {
            wsServer.stop(1000);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }

        log.info("Shutdown complete");
    }

    public PetConfig getConfig() {
        return config;
    }

    public PetStateManager getStateManager() {
        return stateManager;
    }

    public Scheduler getScheduler() {
        return scheduler;
    }

    public String getConfigPath() {
        return configManager.getConfigPath();
    }

    public void sendCommand(String action, JsonObject payload) {
        Envelope cmd = Protocol.createCommand(action, payload);
        String serialized = Protocol.serialize(cmd);
        sendOrCache(action, serialized);
        notifyMessageLog("→", "command", action, "");
    }

    public void restartRenderer() {
        CompletableFuture.runAsync(() -> {
            try {
                processManager.setShutdownCommandSender(() ->
                    wsServer.sendToInstance(LEGACY_INSTANCE_ID, Protocol.serialize(
                            Protocol.createCommand("shutdown", new JsonObject()))));
                processManager.stopRenderer();
                Thread.sleep(500);
                restartAttempts.set(0);
                stateManager.setConnected(false);
                stateManager.setModelLoaded(false);
                processManager.startRenderer(LEGACY_INSTANCE_ID);
                log.info("Manual renderer restart initiated");
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            } catch (Exception e) {
                log.error("Manual restart failed: {}", e.getMessage(), e);
            }
        });
    }

    public void stopRenderer() {
        processManager.setShutdownCommandSender(() ->
            wsServer.sendToInstance(LEGACY_INSTANCE_ID, Protocol.serialize(
                    Protocol.createCommand("shutdown", new JsonObject()))));
        processManager.stopRenderer();
    }

    public void loadModel(String modelName) {
        JsonObject payload = new JsonObject();
        payload.addProperty("model_path", modelName);
        sendCommand("load_model", payload);

        var newModel = new ModelSettingsConfig(modelName, config.model().scale());
        config = new PetConfig(config.window(), newModel, config.behavior(), config.system());
        configManager.save(config);
    }

    public List<String> getAvailableModels() {
        Path resourcesDir = RENDERER_DIR.resolve("Resources");
        List<String> models = new ArrayList<>();
        if (Files.exists(resourcesDir)) {
            try (var stream = Files.list(resourcesDir)) {
                stream.filter(Files::isDirectory)
                      .map(p -> p.getFileName().toString())
                      .filter(name -> !name.startsWith("."))
                      .sorted()
                      .forEach(models::add);
            } catch (IOException e) {
                log.warn("Failed to scan models: {}", e.getMessage());
            }
        }
        return models;
    }

    public Optional<ModelInfo> getModelInfo(String modelName) {
        Path model3Json = RENDERER_DIR.resolve("Resources")
                .resolve(modelName).resolve(modelName + ".model3.json");
        return ModelInfoParser.parse(model3Json);
    }

    public List<String> getAvailableVoicePacks() {
        return Collections.unmodifiableList(availableVoicePacks);
    }

    public String getCurrentVoicePackForModel(String modelName) {
        return mountConfigManager != null
            ? mountConfigManager.loadForModel(modelName).voicePackName()
            : null;
    }

    public void mountVoicePack(String modelName, String voicePackName) {
        if (mountConfigManager == null) return;
        mountConfigManager.saveForModel(new MountConfig(modelName, voicePackName));
        String currentModel = stateManager.getState().currentModelName();
        if (modelName.equals(currentModel)) {
            initMountedEngine(modelName);
        }
    }

    public void unmountVoicePack(String modelName) {
        mountVoicePack(modelName, null);
    }

    public void applyConfig(PetConfig newConfig) {
        PetConfig oldConfig = this.config;
        this.config = newConfig;
        configManager.save(newConfig);

        if (oldConfig == null || oldConfig.window().opacity() != newConfig.window().opacity()) {
            JsonObject payload = new JsonObject();
            payload.addProperty("opacity", newConfig.window().opacity());
            sendCommand("set_opacity", payload);
        }

        if (oldConfig == null
                || oldConfig.behavior().idleIntervalSeconds() != newConfig.behavior().idleIntervalSeconds()) {
            scheduler.updateInterval(Math.max(1, newConfig.behavior().idleIntervalSeconds()) * 1000);
        }

        if (oldConfig == null
                || oldConfig.behavior().targetFps() != newConfig.behavior().targetFps()) {
            JsonObject fpsPayload = new JsonObject();
            fpsPayload.addProperty("fps", newConfig.behavior().targetFps());
            sendCommand("set_fps", fpsPayload);
        }

        if (uiController != null) {
            Platform.runLater(() -> uiController.updateIdleInterval(newConfig.behavior().idleIntervalSeconds()));
        }
    }

    public void reloadConfig() {
        config = configManager.load();
        if (uiController != null) {
            Platform.runLater(() -> uiController.updateIdleInterval(config.behavior().idleIntervalSeconds()));
        }
    }

    public void triggerRandomIdleMotion() {
        scheduler.resume();
    }

    public void toggleSchedulerPause() {
        if (scheduler.isPaused()) {
            scheduler.resume();
        } else {
            scheduler.pause();
        }
    }

    private void notifyActivity(String message) {
        if (uiController != null) {
            Platform.runLater(() -> uiController.addActivity(message));
        }
    }

    private void notifyMessageLog(String direction, String type, String action, String summary) {
        if (uiController != null) {
            Platform.runLater(() -> uiController.addMessageLog(direction, type, action, summary));
        }
    }

    private void updateUiConnectionStatus(boolean connected) {
        if (uiController != null) {
            Platform.runLater(() -> uiController.updateConnectionStatus(connected));
        }
    }

    private void updateUiModelName(String name) {
        if (uiController != null) {
            Platform.runLater(() -> {
                uiController.updateModelName(name);
                getModelInfo(name).ifPresent(info -> uiController.updateModelInfo(info));
            });
        }
    }
}
