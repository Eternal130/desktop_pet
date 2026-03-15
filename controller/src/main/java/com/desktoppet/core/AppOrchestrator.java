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

import java.nio.file.Path;
import java.time.Duration;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

public class AppOrchestrator {
    private static final Logger log = LoggerFactory.getLogger(AppOrchestrator.class);
    private static final int WS_PORT = 9000;

    private final ConfigManager configManager;
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

    private PetConfig config;
    private MainWindowController uiController;

    public AppOrchestrator() {
        this.configManager = new ConfigManager();
        this.stateManager = new PetStateManager();
        this.wsServer = new PetWebSocketServer(WS_PORT);
        this.dispatcher = new MessageDispatcher();
        this.processManager = new ProcessManager();
        this.interactionHandler = new InteractionHandler(msg -> {
            Optional<Envelope> envelope = Protocol.deserialize(msg);
            if (envelope.isPresent()) {
                sendOrCache(envelope.get().action(), msg);
            } else if (stateManager.getState().connected()) {
                wsServer.sendMessage(msg);
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

        processManager.startRenderer();
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

                        JsonObject posPayload = new JsonObject();
                        posPayload.addProperty("x", config.window().positionX());
                        posPayload.addProperty("y", config.window().positionY());
                        sendOrCache(
                                "set_position",
                                Protocol.serialize(Protocol.createCommand("set_position", posPayload))
                        );

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
        });

        dispatcher.registerEventHandler("model_load_failed", envelope -> {
            log.error("model_load_failed: {}", envelope.payload());
            stateManager.setModelLoaded(false);
        });

        dispatcher.registerEventHandler("hit", envelope -> {
            log.debug("hit event: {}", envelope.payload());
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

        wsServer.setMessageCallback(rawMsg -> {
            Optional<Envelope> envelope = Protocol.deserialize(rawMsg);
            envelope.ifPresent(dispatcher::dispatch);
        });

        wsServer.setConnectionCallback(connected -> {
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

        List<String> idleMotions = List.of("Idle");
        var modelInfo = ModelInfoParser.parse(model3Json);
        if (modelInfo.isPresent() && !modelInfo.get().motionGroups().isEmpty()) {
            idleMotions = List.copyOf(modelInfo.get().motionGroups().keySet());
            log.info("Loaded {} motion groups for model {}", idleMotions.size(), modelName);
        } else {
            log.info("Using default idle motions for model {}", modelName);
        }

        final List<String> finalIdleMotions = idleMotions;
        int intervalMillis = Math.max(1, config.behavior().idleIntervalSeconds()) * 1000;
        scheduler.start(intervalMillis, finalIdleMotions, motionGroup -> {
            if (stateManager.getState().connected() && stateManager.getState().modelLoaded()) {
                JsonObject payload = new JsonObject();
                payload.addProperty("group", motionGroup);
                payload.addProperty("index", 0);
                payload.addProperty("priority", 1);
                sendOrCache(
                        "play_motion",
                        Protocol.serialize(Protocol.createCommand("play_motion", payload))
                );
                log.debug("Idle motion triggered: {}", motionGroup);
            }
        });
        scheduler.resume();
        log.info("Scheduler started: interval={}ms, motions={}", intervalMillis, finalIdleMotions);
    }

    private void sendOrCache(String action, String serializedEnvelope) {
        if (stateManager.getState().connected()) {
            wsServer.sendMessage(serializedEnvelope);
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
            wsServer.sendMessage(cmd);
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
        processManager.startRenderer();
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
            wsServer.sendMessage(Protocol.serialize(Protocol.createCommand("shutdown", new JsonObject())))
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

    private void updateUiConnectionStatus(boolean connected) {
        if (uiController != null) {
            Platform.runLater(() -> uiController.updateConnectionStatus(connected));
        }
    }

    private void updateUiModelName(String name) {
        if (uiController != null) {
            Platform.runLater(() -> uiController.updateModelName(name));
        }
    }
}
