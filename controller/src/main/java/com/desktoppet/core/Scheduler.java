package com.desktoppet.core;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.Random;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.TimeUnit;
import java.util.function.Consumer;

public class Scheduler {
    private static final Logger log = LoggerFactory.getLogger(Scheduler.class);

    private final ScheduledExecutorService executor;
    private final Random random = new Random();

    private volatile List<String> idleMotions = new ArrayList<>();
    private volatile Consumer<String> onTrigger = motion -> {
    };
    private volatile int intervalMillis;
    private volatile boolean paused = false;
    private volatile boolean running = false;
    private volatile ScheduledFuture<?> scheduledTask;

    public Scheduler(ScheduledExecutorService executor) {
        this.executor = Objects.requireNonNull(executor, "executor must not be null");
    }

    public static Scheduler createDefault() {
        return new Scheduler(Executors.newSingleThreadScheduledExecutor(r -> {
            Thread t = new Thread(r, "scheduler-thread");
            t.setDaemon(true);
            return t;
        }));
    }

    public synchronized void start(int intervalMillis, List<String> idleMotions, Consumer<String> onTrigger) {
        if (intervalMillis < 0) {
            throw new IllegalArgumentException("intervalMillis must be >= 0");
        }

        this.intervalMillis = intervalMillis;
        this.idleMotions = new ArrayList<>(Objects.requireNonNull(idleMotions, "idleMotions must not be null"));
        this.onTrigger = Objects.requireNonNull(onTrigger, "onTrigger must not be null");
        this.running = true;
        this.paused = false;
        cancelCurrentTask();
        scheduleTask();
    }

    public void pause() {
        paused = true;
    }

    public void resume() {
        paused = false;
    }

    public void setIdleMotions(List<String> motions) {
        idleMotions = new ArrayList<>(Objects.requireNonNull(motions, "motions must not be null"));
    }

    public synchronized void updateInterval(int intervalMillis) {
        if (intervalMillis < 0) {
            throw new IllegalArgumentException("intervalMillis must be >= 0");
        }

        this.intervalMillis = intervalMillis;
        if (running) {
            cancelCurrentTask();
            scheduleTask();
        }
    }

    public boolean isRunning() {
        return running;
    }

    public synchronized void shutdown() {
        running = false;
        paused = true;
        cancelCurrentTask();
        executor.shutdownNow();
    }

    private synchronized void scheduleTask() {
        if (!running) {
            return;
        }

        scheduledTask = executor.scheduleAtFixedRate(() -> {
            if (paused) {
                return;
            }

            List<String> motions = idleMotions;
            if (motions.isEmpty()) {
                return;
            }

            String motion = motions.get(random.nextInt(motions.size()));
            try {
                onTrigger.accept(motion);
            } catch (Exception e) {
                log.error("Trigger error: {}", e.getMessage(), e);
            }
        }, intervalMillis, intervalMillis, TimeUnit.MILLISECONDS);
    }

    private void cancelCurrentTask() {
        ScheduledFuture<?> current = scheduledTask;
        if (current != null) {
            current.cancel(false);
            scheduledTask = null;
        }
    }
}
