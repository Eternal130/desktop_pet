package com.desktoppet.core;

import com.desktoppet.model.PetState;
import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

class PetStateManagerTest {

    @Test
    void initialState_hasCorrectDefaults() {
        PetStateManager manager = new PetStateManager();

        PetState state = manager.getState();

        assertEquals(PetState.initial(), state);
    }

    @Test
    void updateModelName_reflectedInState() {
        PetStateManager manager = new PetStateManager();

        manager.updateModelName("Hiyori");

        assertEquals("Hiyori", manager.getState().currentModelName());
    }

    @Test
    void updateWindowPosition_reflectedInState() {
        PetStateManager manager = new PetStateManager();

        manager.updateWindowPosition(100, 200);

        PetState state = manager.getState();
        assertEquals(100, state.windowX());
        assertEquals(200, state.windowY());
    }

    @Test
    void setConnected_reflectedInState() {
        PetStateManager manager = new PetStateManager();

        manager.setConnected(true);

        assertTrue(manager.getState().connected());
    }

    @Test
    void setModelLoaded_reflectedInState() {
        PetStateManager manager = new PetStateManager();

        manager.setModelLoaded(true);

        assertTrue(manager.getState().modelLoaded());
    }

    @Test
    void getState_returnsImmutableSnapshot() {
        PetStateManager manager = new PetStateManager();
        PetState before = manager.getState();

        manager.updateWindowPosition(320, 480);
        PetState after = manager.getState();

        assertEquals(0, before.windowX());
        assertEquals(0, before.windowY());
        assertEquals(320, after.windowX());
        assertEquals(480, after.windowY());
    }

    @Test
    void threadSafety_concurrentUpdates() {
        PetStateManager manager = new PetStateManager();
        int threads = 10;
        ExecutorService pool = Executors.newFixedThreadPool(threads);
        CountDownLatch startGate = new CountDownLatch(1);
        List<Future<?>> futures = new ArrayList<>();

        assertDoesNotThrow(() -> {
            for (int i = 0; i < threads; i++) {
                final int value = i;
                futures.add(pool.submit(() -> {
                    startGate.await();
                    manager.updateWindowPosition(value, value * 10);
                    return null;
                }));
            }

            startGate.countDown();
            for (Future<?> future : futures) {
                future.get(2, TimeUnit.SECONDS);
            }
        });

        pool.shutdownNow();
    }
}
