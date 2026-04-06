package com.desktoppet.util;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.concurrent.atomic.AtomicReference;

import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.*;

class AutoLaunchManagerTest {

    private Process buildMockProcess(int exitCode) throws Exception {
        Process mockProcess = mock(Process.class);
        when(mockProcess.getInputStream()).thenReturn(new ByteArrayInputStream(new byte[0]));
        doReturn(exitCode).when(mockProcess).waitFor();
        return mockProcess;
    }

    private ProcessBuilder buildMockProcessBuilder(Process mockProcess) throws IOException {
        ProcessBuilder pb = mock(ProcessBuilder.class);
        when(pb.redirectErrorStream(true)).thenReturn(pb);
        doReturn(mockProcess).when(pb).start();
        return pb;
    }

    private AutoLaunchManager createForWindows(
            AutoLaunchManager.ProcessBuilderFactory factory,
            String appPath) {
        return new AutoLaunchManager(
                factory,
                () -> appPath,
                () -> Path.of("/tmp/.config"),
                () -> true,
                () -> false
        );
    }

    private AutoLaunchManager createForLinux(
            AutoLaunchManager.ProcessBuilderFactory factory,
            String appPath,
            Path configDir) {
        return new AutoLaunchManager(
                factory,
                () -> appPath,
                () -> configDir,
                () -> false,
                () -> true
        );
    }

    private AutoLaunchManager createForUnsupported() {
        return new AutoLaunchManager(
                command -> new ProcessBuilder(command),
                () -> "app",
                () -> Path.of("/tmp"),
                () -> false,
                () -> false
        );
    }

    // --- Windows Tests ---

    @Test
    void windows_isEnabled_returnsTrueWhenRegistryEntryExists() throws Exception {
        Process mockProcess = buildMockProcess(0);
        AtomicReference<List<String>> capturedCmd = new AtomicReference<>();

        AutoLaunchManager.ProcessBuilderFactory factory = command -> {
            capturedCmd.set(command);
            try {
                return buildMockProcessBuilder(mockProcess);
            } catch (IOException e) {
                throw new RuntimeException(e);
            }
        };

        AutoLaunchManager mgr = createForWindows(factory, "app");
        assertTrue(mgr.isEnabled());

        assertEquals(List.of("reg", "query",
                "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                "/v", "DesktopPet"), capturedCmd.get());
    }

    @Test
    void windows_isEnabled_returnsFalseWhenRegistryEntryMissing() throws Exception {
        Process mockProcess = buildMockProcess(1);

        AutoLaunchManager.ProcessBuilderFactory factory = command -> {
            try {
                return buildMockProcessBuilder(mockProcess);
            } catch (IOException e) {
                throw new RuntimeException(e);
            }
        };

        AutoLaunchManager mgr = createForWindows(factory, "app");
        assertFalse(mgr.isEnabled());
    }

    @Test
    void windows_enable_writesRegistryCommand() throws Exception {
        Process mockProcess = buildMockProcess(0);
        AtomicReference<List<String>> capturedCmd = new AtomicReference<>();

        AutoLaunchManager.ProcessBuilderFactory factory = command -> {
            capturedCmd.set(command);
            try {
                return buildMockProcessBuilder(mockProcess);
            } catch (IOException e) {
                throw new RuntimeException(e);
            }
        };

        AutoLaunchManager mgr = createForWindows(factory, "javaw -jar \"app.jar\"");
        mgr.enable();

        List<String> cmd = capturedCmd.get();
        assertEquals("reg", cmd.get(0));
        assertEquals("add", cmd.get(1));
        assertEquals("HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", cmd.get(2));
        assertEquals("/v", cmd.get(3));
        assertEquals("DesktopPet", cmd.get(4));
        assertEquals("/t", cmd.get(5));
        assertEquals("REG_SZ", cmd.get(6));
        assertEquals("/d", cmd.get(7));
        assertEquals("javaw -jar \"app.jar\"", cmd.get(8));
        assertEquals("/f", cmd.get(9));
    }

    @Test
    void windows_disable_deletesRegistryEntry() throws Exception {
        Process mockProcess = buildMockProcess(0);
        AtomicReference<List<String>> capturedCmd = new AtomicReference<>();

        AutoLaunchManager.ProcessBuilderFactory factory = command -> {
            capturedCmd.set(command);
            try {
                return buildMockProcessBuilder(mockProcess);
            } catch (IOException e) {
                throw new RuntimeException(e);
            }
        };

        AutoLaunchManager mgr = createForWindows(factory, "app");
        mgr.disable();

        List<String> cmd = capturedCmd.get();
        assertEquals("reg", cmd.get(0));
        assertEquals("delete", cmd.get(1));
        assertEquals("HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", cmd.get(2));
        assertEquals("/v", cmd.get(3));
        assertEquals("DesktopPet", cmd.get(4));
        assertEquals("/f", cmd.get(5));
    }

    @Test
    void windows_isEnabled_returnsFalseOnException() {
        AutoLaunchManager.ProcessBuilderFactory factory = command -> {
            throw new RuntimeException("simulated failure");
        };

        AutoLaunchManager mgr = createForWindows(factory, "app");
        assertFalse(mgr.isEnabled());
    }

    // --- Linux Tests ---

    @Test
    void linux_enable_createsDesktopFile(@TempDir Path tempDir) {
        Path configDir = tempDir.resolve(".config");
        AutoLaunchManager mgr = createForLinux(null, "/opt/desktop-pet/run.sh", configDir);

        mgr.enable();

        Path desktopFile = configDir.resolve("autostart").resolve("desktop-pet.desktop");
        assertTrue(Files.exists(desktopFile));
    }

    @Test
    void linux_enable_desktopFileHasCorrectContent(@TempDir Path tempDir) throws Exception {
        Path configDir = tempDir.resolve(".config");
        AutoLaunchManager mgr = createForLinux(null, "/opt/desktop-pet/run.sh", configDir);

        mgr.enable();

        Path desktopFile = configDir.resolve("autostart").resolve("desktop-pet.desktop");
        String content = Files.readString(desktopFile);
        assertTrue(content.startsWith("[Desktop Entry]\n"));
        assertTrue(content.contains("Type=Application\n"));
        assertTrue(content.contains("Name=Desktop Pet\n"));
        assertTrue(content.contains("Exec=/opt/desktop-pet/run.sh\n"));
        assertTrue(content.contains("Hidden=false\n"));
    }

    @Test
    void linux_isEnabled_returnsTrueWhenDesktopFileExists(@TempDir Path tempDir) throws Exception {
        Path configDir = tempDir.resolve(".config");
        Path autostartDir = configDir.resolve("autostart");
        Files.createDirectories(autostartDir);
        Files.writeString(autostartDir.resolve("desktop-pet.desktop"), "[Desktop Entry]\n");

        AutoLaunchManager mgr = createForLinux(null, "app", configDir);
        assertTrue(mgr.isEnabled());
    }

    @Test
    void linux_isEnabled_returnsFalseWhenDesktopFileMissing(@TempDir Path tempDir) {
        Path configDir = tempDir.resolve(".config");

        AutoLaunchManager mgr = createForLinux(null, "app", configDir);
        assertFalse(mgr.isEnabled());
    }

    @Test
    void linux_disable_removesDesktopFile(@TempDir Path tempDir) throws Exception {
        Path configDir = tempDir.resolve(".config");
        Path autostartDir = configDir.resolve("autostart");
        Files.createDirectories(autostartDir);
        Path desktopFile = autostartDir.resolve("desktop-pet.desktop");
        Files.writeString(desktopFile, "[Desktop Entry]\n");
        assertTrue(Files.exists(desktopFile));

        AutoLaunchManager mgr = createForLinux(null, "app", configDir);
        mgr.disable();

        assertFalse(Files.exists(desktopFile));
    }

    @Test
    void linux_disable_noOpWhenFileNotPresent(@TempDir Path tempDir) {
        Path configDir = tempDir.resolve(".config");

        AutoLaunchManager mgr = createForLinux(null, "app", configDir);
        assertDoesNotThrow(mgr::disable);
    }

    // --- Unsupported Platform ---

    @Test
    void unsupportedPlatform_isEnabled_returnsFalse() {
        AutoLaunchManager mgr = createForUnsupported();
        assertFalse(mgr.isEnabled());
    }

    @Test
    void unsupportedPlatform_enable_doesNotThrow() {
        AutoLaunchManager mgr = createForUnsupported();
        assertDoesNotThrow(mgr::enable);
    }

    @Test
    void unsupportedPlatform_disable_doesNotThrow() {
        AutoLaunchManager mgr = createForUnsupported();
        assertDoesNotThrow(mgr::disable);
    }
}
