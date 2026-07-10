package com.desktoppet.core;

import org.junit.jupiter.api.Test;

import java.util.List;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

class SchedulerTest {

    @Test
    void start_triggerCallback_withinInterval() throws InterruptedException {
        ScheduledExecutorService executor = Executors.newSingleThreadScheduledExecutor();
        Scheduler scheduler = new Scheduler(executor);
        CountDownLatch triggered = new CountDownLatch(1);
        AtomicReference<String> motionRef = new AtomicReference<>();

        try {
            scheduler.start(100, List.of("Idle"), motion -> {
                motionRef.set(motion);
                triggered.countDown();
            });

            assertTrue(triggered.await(300, TimeUnit.MILLISECONDS));
            assertEquals("Idle", motionRef.get());
        } finally {
            scheduler.shutdown();
        }
    }

    @Test
    void pause_stopsTriggering() throws InterruptedException {
        ScheduledExecutorService executor = Executors.newSingleThreadScheduledExecutor();
        Scheduler scheduler = new Scheduler(executor);
        CountDownLatch firstTrigger = new CountDownLatch(1);
        AtomicInteger callbackCount = new AtomicInteger(0);

        try {
            scheduler.start(50, List.of("Idle"), motion -> {
                callbackCount.incrementAndGet();
                firstTrigger.countDown();
            });

            assertTrue(firstTrigger.await(300, TimeUnit.MILLISECONDS));
            scheduler.pause();
            int countAfterPause = callbackCount.get();

            Thread.sleep(300);

            assertEquals(countAfterPause, callbackCount.get());
        } finally {
            scheduler.shutdown();
        }
    }

    @Test
    void resume_restartsTriggerring() throws InterruptedException {
        ScheduledExecutorService executor = Executors.newSingleThreadScheduledExecutor();
        Scheduler scheduler = new Scheduler(executor);
        CountDownLatch firstTrigger = new CountDownLatch(1);
        CountDownLatch resumedTrigger = new CountDownLatch(1);
        AtomicInteger callbackCount = new AtomicInteger(0);
        AtomicInteger pausedCount = new AtomicInteger(0);

        try {
            scheduler.start(50, List.of("Idle"), motion -> {
                int count = callbackCount.incrementAndGet();
                firstTrigger.countDown();
                if (count > pausedCount.get()) {
                    resumedTrigger.countDown();
                }
            });

            assertTrue(firstTrigger.await(300, TimeUnit.MILLISECONDS));
            scheduler.pause();
            pausedCount.set(callbackCount.get());
            Thread.sleep(200);
            assertEquals(pausedCount.get(), callbackCount.get());

            scheduler.resume();
            assertTrue(resumedTrigger.await(300, TimeUnit.MILLISECONDS));
        } finally {
            scheduler.shutdown();
        }
    }

    @Test
    void triggerNow_firesImmediately_evenWhenPaused() throws InterruptedException {
        ScheduledExecutorService executor = Executors.newSingleThreadScheduledExecutor();
        Scheduler scheduler = new Scheduler(executor);
        CountDownLatch triggered = new CountDownLatch(1);

        try {
            scheduler.start(5000, List.of("Idle"), motion -> triggered.countDown());
            scheduler.pause();
            assertTrue(scheduler.isPaused());

            scheduler.triggerNow();

            assertTrue(triggered.await(500, TimeUnit.MILLISECONDS),
                    "triggerNow should fire callback immediately even when paused");
            assertFalse(scheduler.isPaused(), "triggerNow should clear paused state");
        } finally {
            scheduler.shutdown();
        }
    }

    @Test
    void setIdleMotions_changesMotionPool() throws InterruptedException {
        ScheduledExecutorService executor = Executors.newSingleThreadScheduledExecutor();
        Scheduler scheduler = new Scheduler(executor);
        CountDownLatch initialTrigger = new CountDownLatch(1);
        CountDownLatch changedMotionTrigger = new CountDownLatch(1);
        AtomicReference<String> firstMotion = new AtomicReference<>();

        try {
            scheduler.start(50, List.of("Idle"), motion -> {
                firstMotion.compareAndSet(null, motion);
                initialTrigger.countDown();
                if ("TapHead".equals(motion)) {
                    changedMotionTrigger.countDown();
                }
            });

            assertTrue(initialTrigger.await(300, TimeUnit.MILLISECONDS));
            assertEquals("Idle", firstMotion.get());

            scheduler.setIdleMotions(List.of("TapHead"));

            assertTrue(changedMotionTrigger.await(300, TimeUnit.MILLISECONDS));
        } finally {
            scheduler.shutdown();
        }
    }

    @Test
    void shutdown_cleansUp() {
        ScheduledExecutorService executor = Executors.newSingleThreadScheduledExecutor();
        Scheduler scheduler = new Scheduler(executor);

        scheduler.start(50, List.of("Idle"), motion -> {
        });

        assertDoesNotThrow(scheduler::shutdown);
        assertFalse(scheduler.isRunning());
    }

    @Test
    void start_picksRandomMotion() throws InterruptedException {
        ScheduledExecutorService executor = Executors.newSingleThreadScheduledExecutor();
        Scheduler scheduler = new Scheduler(executor);
        CountDownLatch tenTriggers = new CountDownLatch(10);
        Set<String> observedMotions = ConcurrentHashMap.newKeySet();

        try {
            scheduler.start(20, List.of("Idle", "TapHead", "TapBody"), motion -> {
                observedMotions.add(motion);
                tenTriggers.countDown();
            });

            assertTrue(tenTriggers.await(600, TimeUnit.MILLISECONDS));
            assertTrue(observedMotions.size() >= 2);
        } finally {
            scheduler.shutdown();
        }
    }
}
