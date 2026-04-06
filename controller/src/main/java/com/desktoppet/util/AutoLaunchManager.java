package com.desktoppet.util;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.List;
import java.util.function.Supplier;

/**
 * Manages OS-specific auto-launch (startup) behavior.
 * <p>
 * Windows: HKCU\Software\Microsoft\Windows\CurrentVersion\Run registry key.
 * Linux: ~/.config/autostart/desktop-pet.desktop file.
 */
public class AutoLaunchManager {

    private static final Logger log = LoggerFactory.getLogger(AutoLaunchManager.class);
    private static final String REG_KEY = "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    private static final String REG_VALUE_NAME = "DesktopPet";
    private static final String DESKTOP_FILE_NAME = "desktop-pet.desktop";

    /**
     * Factory interface for creating ProcessBuilder instances (enables testing).
     * Follows the same pattern as {@link ProcessManager.ProcessBuilderFactory}.
     */
    @FunctionalInterface
    public interface ProcessBuilderFactory {
        ProcessBuilder create(List<String> command);
    }

    private final ProcessBuilderFactory processBuilderFactory;
    private final Supplier<String> appPathSupplier;
    private final Supplier<Path> linuxConfigDirSupplier;
    private final Supplier<Boolean> windowsDetector;
    private final Supplier<Boolean> linuxDetector;

    public AutoLaunchManager() {
        this(
                command -> new ProcessBuilder(command),
                AutoLaunchManager::getDefaultAppPath,
                AutoLaunchManager::getDefaultLinuxConfigDir,
                AutoLaunchManager::defaultIsWindows,
                AutoLaunchManager::defaultIsLinux
        );
    }

    AutoLaunchManager(ProcessBuilderFactory processBuilderFactory,
                      Supplier<String> appPathSupplier,
                      Supplier<Path> linuxConfigDirSupplier,
                      Supplier<Boolean> windowsDetector,
                      Supplier<Boolean> linuxDetector) {
        this.processBuilderFactory = processBuilderFactory;
        this.appPathSupplier = appPathSupplier;
        this.linuxConfigDirSupplier = linuxConfigDirSupplier;
        this.windowsDetector = windowsDetector;
        this.linuxDetector = linuxDetector;
    }

    public boolean isEnabled() {
        if (windowsDetector.get()) {
            return checkWindowsRegistry();
        } else if (linuxDetector.get()) {
            return checkLinuxDesktopFile();
        }
        return false;
    }

    public void enable() {
        if (windowsDetector.get()) {
            enableWindows();
        } else if (linuxDetector.get()) {
            enableLinux();
        } else {
            log.warn("Auto-launch not supported on this platform");
        }
    }

    public void disable() {
        if (windowsDetector.get()) {
            disableWindows();
        } else if (linuxDetector.get()) {
            disableLinux();
        } else {
            log.warn("Auto-launch not supported on this platform");
        }
    }

    // --- Windows Implementation ---

    private boolean checkWindowsRegistry() {
        try {
            ProcessBuilder pb = processBuilderFactory.create(
                    List.of("reg", "query", REG_KEY, "/v", REG_VALUE_NAME));
            pb.redirectErrorStream(true);
            Process process = pb.start();
            int exitCode = process.waitFor();
            return exitCode == 0;
        } catch (Exception e) {
            log.warn("Failed to check Windows registry for auto-launch", e);
            return false;
        }
    }

    private void enableWindows() {
        try {
            String appPath = appPathSupplier.get();
            ProcessBuilder pb = processBuilderFactory.create(
                    List.of("reg", "add", REG_KEY,
                            "/v", REG_VALUE_NAME, "/t", "REG_SZ", "/d", appPath, "/f"));
            pb.redirectErrorStream(true);
            Process process = pb.start();
            int exitCode = process.waitFor();
            if (exitCode == 0) {
                log.info("Auto-launch enabled (Windows registry)");
            } else {
                log.warn("Failed to enable auto-launch, exit code: {}", exitCode);
            }
        } catch (Exception e) {
            log.error("Failed to enable auto-launch on Windows", e);
        }
    }

    private void disableWindows() {
        try {
            ProcessBuilder pb = processBuilderFactory.create(
                    List.of("reg", "delete", REG_KEY,
                            "/v", REG_VALUE_NAME, "/f"));
            pb.redirectErrorStream(true);
            Process process = pb.start();
            int exitCode = process.waitFor();
            if (exitCode == 0) {
                log.info("Auto-launch disabled (Windows registry)");
            } else {
                log.warn("Failed to disable auto-launch, exit code: {}", exitCode);
            }
        } catch (Exception e) {
            log.error("Failed to disable auto-launch on Windows", e);
        }
    }

    // --- Linux Implementation ---

    private boolean checkLinuxDesktopFile() {
        Path desktopFile = getDesktopFilePath();
        return Files.exists(desktopFile);
    }

    private void enableLinux() {
        try {
            Path autostartDir = linuxConfigDirSupplier.get().resolve("autostart");
            Files.createDirectories(autostartDir);
            Path desktopFile = autostartDir.resolve(DESKTOP_FILE_NAME);
            String appPath = appPathSupplier.get();
            String content = "[Desktop Entry]\n" +
                    "Type=Application\n" +
                    "Name=Desktop Pet\n" +
                    "Exec=" + appPath + "\n" +
                    "Hidden=false\n";
            Files.writeString(desktopFile, content);
            log.info("Auto-launch enabled (Linux .desktop file)");
        } catch (Exception e) {
            log.error("Failed to enable auto-launch on Linux", e);
        }
    }

    private void disableLinux() {
        try {
            Path desktopFile = getDesktopFilePath();
            Files.deleteIfExists(desktopFile);
            log.info("Auto-launch disabled (Linux .desktop file)");
        } catch (Exception e) {
            log.error("Failed to disable auto-launch on Linux", e);
        }
    }

    private Path getDesktopFilePath() {
        return linuxConfigDirSupplier.get().resolve("autostart").resolve(DESKTOP_FILE_NAME);
    }

    // --- Utility Methods ---

    private static boolean defaultIsWindows() {
        return System.getProperty("os.name").toLowerCase().contains("win");
    }

    private static boolean defaultIsLinux() {
        String os = System.getProperty("os.name").toLowerCase();
        return os.contains("nix") || os.contains("nux") || os.contains("aix");
    }

    private static String getDefaultAppPath() {
        String classPath = System.getProperty("java.class.path");
        if (classPath != null && classPath.endsWith(".jar")) {
            return "javaw -jar \"" + classPath + "\"";
        }
        return "javaw -jar desktop-pet-controller.jar";
    }

    private static Path getDefaultLinuxConfigDir() {
        return Paths.get(System.getProperty("user.home"), ".config");
    }
}
