package com.desktoppet.util;

import org.junit.jupiter.api.Test;

import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.*;

class ProcessManagerTest {

    private Process buildMockProcess() throws IOException {
        Process mockProcess = mock(Process.class);
        when(mockProcess.getInputStream()).thenReturn(new ByteArrayInputStream(new byte[0]));
        when(mockProcess.onExit()).thenReturn(new CompletableFuture<>());
        when(mockProcess.isAlive()).thenReturn(true);
        return mockProcess;
    }

    private ProcessBuilder buildMockProcessBuilder(Process mockProcess) throws IOException {
        ProcessBuilder pb = mock(ProcessBuilder.class);
        when(pb.redirectErrorStream(true)).thenReturn(pb);
        doReturn(mockProcess).when(pb).start();
        return pb;
    }

    @Test
    void isRunning_falseWhenNotStarted() {
        ProcessManager pm = new ProcessManager("/fake/path", 9000);
        assertFalse(pm.isRunning());
        assertTrue(pm.getProcess().isEmpty());
    }

    @Test
    void startRenderer_buildsCorrectCommand() throws IOException {
        Process mockProcess = buildMockProcess();
        AtomicReference<List<String>> capturedCommand = new AtomicReference<>();

        ProcessManager.ProcessBuilderFactory capturingFactory = command -> {
            capturedCommand.set(command);
            try {
                return buildMockProcessBuilder(mockProcess);
            } catch (IOException e) {
                throw new RuntimeException(e);
            }
        };

        ProcessManager pm = new ProcessManager("/fake/renderer", 9000, capturingFactory);
        pm.startRenderer(1);

        List<String> cmd = capturedCommand.get();
        assertNotNull(cmd);
        assertEquals("/fake/renderer", cmd.get(0));
        assertEquals("--port", cmd.get(1));
        assertEquals("9000", cmd.get(2));
        assertEquals("--instance-id", cmd.get(3));
        assertEquals("1", cmd.get(4));
    }

    @Test
    void stopRenderer_callsShutdownBeforeDestroy() throws IOException {
        Process mockProcess = buildMockProcess();
        AtomicBoolean shutdownCalled = new AtomicBoolean(false);

        ProcessManager.ProcessBuilderFactory factory = command -> {
            try {
                return buildMockProcessBuilder(mockProcess);
            } catch (IOException e) {
                throw new RuntimeException(e);
            }
        };

        ProcessManager pm = new ProcessManager("/fake/renderer", 9000, factory);
        pm.setShutdownCommandSender(() -> {
            shutdownCalled.set(true);
            doReturn(false).when(mockProcess).isAlive();
        });
        pm.startRenderer(1);

        pm.stopRenderer();

        assertTrue(shutdownCalled.get());
    }

    @Test
    void stopRenderer_doesNothingWhenNotRunning() {
        AtomicBoolean shutdownCalled = new AtomicBoolean(false);
        ProcessManager pm = new ProcessManager("/fake/renderer", 9000);
        pm.setShutdownCommandSender(() -> shutdownCalled.set(true));

        pm.stopRenderer();

        assertFalse(shutdownCalled.get());
    }

    @Test
    void exitCallback_invokedOnProcessExit() throws IOException, InterruptedException {
        CompletableFuture<Process> exitFuture = new CompletableFuture<>();
        Process mockProcess = mock(Process.class);
        when(mockProcess.getInputStream()).thenReturn(new ByteArrayInputStream(new byte[0]));
        when(mockProcess.onExit()).thenReturn(exitFuture);
        when(mockProcess.isAlive()).thenReturn(true);
        when(mockProcess.exitValue()).thenReturn(0);

        AtomicInteger capturedExitCode = new AtomicInteger(-1);

        ProcessManager.ProcessBuilderFactory factory = command -> {
            try {
                return buildMockProcessBuilder(mockProcess);
            } catch (IOException e) {
                throw new RuntimeException(e);
            }
        };

        ProcessManager pm = new ProcessManager("/fake/renderer", 9000, factory);
        pm.setExitCallback(capturedExitCode::set);
        pm.startRenderer(1);

        exitFuture.complete(mockProcess);
        Thread.sleep(100);

        assertEquals(0, capturedExitCode.get());
    }
}
