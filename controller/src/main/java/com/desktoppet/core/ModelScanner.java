package com.desktoppet.core;

import com.desktoppet.model.ModelInfo;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Optional;

public final class ModelScanner {

    private static final Logger log = LoggerFactory.getLogger(ModelScanner.class);

    private ModelScanner() {
    }

    public static Path resolveResourcesDir(String rendererPath) {
        if (rendererPath == null || rendererPath.isBlank()) {
            return null;
        }
        Path rendererFile = Path.of(rendererPath);
        Path parentDir = rendererFile.getParent();
        if (parentDir == null) {
            return null;
        }
        return parentDir.resolve("Resources");
    }

    public static List<String> scanAvailableModels(String rendererPath) {
        Path resourcesDir = resolveResourcesDir(rendererPath);
        if (resourcesDir == null || !Files.isDirectory(resourcesDir)) {
            log.debug("Resources directory not found for renderer: {}", rendererPath);
            return Collections.emptyList();
        }

        List<String> models = new ArrayList<>();
        try (var stream = Files.list(resourcesDir)) {
            stream.filter(Files::isDirectory)
                  .filter(dir -> {
                      String name = dir.getFileName().toString();
                      return Files.exists(dir.resolve(name + ".model3.json"));
                  })
                  .map(dir -> dir.getFileName().toString())
                  .sorted()
                  .forEach(models::add);
        } catch (IOException e) {
            log.warn("Failed to scan models in {}: {}", resourcesDir, e.getMessage());
        }

        log.debug("Found {} models in {}: {}", models.size(), resourcesDir, models);
        return models;
    }

    public static Optional<ModelInfo> getModelInfo(String rendererPath, String modelName) {
        Path resourcesDir = resolveResourcesDir(rendererPath);
        if (resourcesDir == null || modelName == null || modelName.isBlank()) {
            return Optional.empty();
        }
        Path model3Json = resourcesDir.resolve(modelName).resolve(modelName + ".model3.json");
        return ModelInfoParser.parse(model3Json);
    }
}
