package com.desktoppet.util;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.BufferedReader;
import java.io.File;
import java.io.IOException;
import java.io.InputStreamReader;
import java.security.SecureRandom;
import java.util.HexFormat;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.function.Consumer;

/**
 * Manages the lifecycle of the renderer process (start/stop/monitor).
 */
public class ProcessManager {

    private static final Logger log = LoggerFactory.getLogger(ProcessManager.class);

    /**
     * Factory interface for creating ProcessBuilder instances (enables testing).
     */
    @FunctionalInterface
    public interface ProcessBuilderFactory {
        ProcessBuilder create(List<String> command);
    }

    private final String rendererPath;
    private final int wsPort;
    private final ProcessBuilderFactory processBuilderFactory;

    private final ExecutorService logExecutor = Executors.newSingleThreadExecutor(r -> {
        Thread t = new Thread(r, "renderer-log");
        t.setDaemon(true);
        return t;
    });

    private final String authToken = generateSecureToken();

    private volatile Process process;
    private volatile Runnable shutdownCommandSender;
    private volatile Consumer<Integer> exitCallback;

    /**
     * Default constructor — uses relative path to renderer binary and port 9001.
     */
    public ProcessManager() {
        this("../renderer/build/bin/desktop-pet-renderer/desktop-pet-renderer", 9001);
    }

    /**
     * Injection constructor for custom path/port (also used by tests).
     */
    public ProcessManager(String rendererPath, int wsPort) {
        this(rendererPath, wsPort, command -> new ProcessBuilder(command));
    }

    /**
     * Full injection constructor for testing (allows mocking ProcessBuilder).
     */
    public ProcessManager(String rendererPath, int wsPort, ProcessBuilderFactory processBuilderFactory) {
        this.rendererPath = rendererPath;
        this.wsPort = wsPort;
        this.processBuilderFactory = processBuilderFactory;
    }

    public void setShutdownCommandSender(Runnable callback) {
        this.shutdownCommandSender = callback;
    }

    public void setExitCallback(Consumer<Integer> callback) {
        this.exitCallback = callback;
    }

    public void startRenderer(int instanceId) throws IOException {
        startRenderer(instanceId, null, -1, -1, -1, -1);
    }

    public void startRenderer(int instanceId, String modelName, int x, int y) throws IOException {
        startRenderer(instanceId, modelName, x, y, -1, -1);
    }

    public void startRenderer(int instanceId, String modelName, int x, int y, int width, int height) throws IOException {
        if (isRunning()) {
            log.warn("Renderer is already running (pid={})", process.pid());
            return;
        }

        List<String> command = new java.util.ArrayList<>(List.of(
                rendererPath,
                "--port",
                String.valueOf(wsPort),
                "--instance-id",
                String.valueOf(instanceId),
                "--token",
                authToken
        ));
        if (modelName != null && !modelName.isEmpty()) {
            command.add("--model");
            command.add(modelName);
        }
        if (x >= 0 && y >= 0) {
            command.add("--x");
            command.add(String.valueOf(x));
            command.add("--y");
            command.add(String.valueOf(y));
        }
        if (width > 0 && height > 0) {
            command.add("--width");
            command.add(String.valueOf(width));
            command.add("--height");
            command.add(String.valueOf(height));
        }

        log.info("Starting renderer: {}", String.join(" ", command));

        ProcessBuilder pb = processBuilderFactory.create(command);

        // Set working directory to the renderer binary's parent so Resources/ resolves correctly
        File rendererFile = new File(rendererPath);
        File workingDir = rendererFile.getParentFile();
        if (workingDir != null && workingDir.exists()) {
            pb.directory(workingDir);
        }

        pb.redirectErrorStream(true);

        process = pb.start();
        log.info("Renderer started (pid={})", process.pid());

        logExecutor.submit(() -> {
            try (BufferedReader reader = new BufferedReader(
                    new InputStreamReader(process.getInputStream()))) {
                String line;
                while ((line = reader.readLine()) != null) {
                    log.info("[renderer] {}", line);
                }
            } catch (IOException ignored) {
            }
        });

        process.onExit().thenAccept(p -> {
            int exitCode = p.exitValue();
            log.info("Renderer exited with code {}", exitCode);
            if (exitCallback != null) {
                exitCallback.accept(exitCode);
            }
        });
    }

    /**
     * Stops the renderer process gracefully, falling back to forcible termination.
     * 1. Sends shutdown command via WebSocket (if configured)
     * 2. Waits up to 5 seconds for natural exit
     * 3. Force-kills if still alive
     */
    public void stopRenderer() {
        if (!isRunning()) {
            log.debug("stopRenderer called but renderer is not running");
            return;
        }

        // Step 1: Graceful shutdown via WebSocket command
        if (shutdownCommandSender != null) {
            log.info("Sending graceful shutdown command to renderer");
            try {
                shutdownCommandSender.run();
            } catch (Exception e) {
                log.warn("Shutdown command sender threw exception: {}", e.getMessage());
            }
        }

        // Step 2: Wait up to 5 seconds for natural exit
        boolean exited;
        try {
            exited = process.waitFor(5, TimeUnit.SECONDS);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            exited = !process.isAlive();
        }

        // Step 3: Force-kill if still alive
        if (!exited) {
            log.warn("Renderer did not exit within 5s — force-killing");
            process.destroyForcibly();
            log.info("Renderer force-killed");
        } else {
            log.info("Renderer exited gracefully (code={})", process.exitValue());
        }
    }

    /**
     * Returns true if the renderer process is currently alive.
     */
    public boolean isRunning() {
        return process != null && process.isAlive();
    }

    /**
     * Returns the underlying Process, if started.
     */
    public Optional<Process> getProcess() {
        return Optional.ofNullable(process);
    }

    /**
     * Returns the per-instance auth token used for WebSocket connection validation.
     */
    public String getAuthToken() {
        return authToken;
    }

    /**
     * Shuts down internal executors. Call after stopRenderer() when discarding this instance.
     */
    public void shutdown() {
        logExecutor.shutdownNow();
    }

    private static String generateSecureToken() {
        byte[] bytes = new byte[32];
        new SecureRandom().nextBytes(bytes);
        return HexFormat.of().formatHex(bytes);
    }
}
