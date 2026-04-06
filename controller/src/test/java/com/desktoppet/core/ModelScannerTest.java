package com.desktoppet.core;

import com.desktoppet.model.ModelInfo;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Optional;

import static org.junit.jupiter.api.Assertions.*;

class ModelScannerTest {

    private static final Path REAL_RENDERER = Path.of(
            "../third_party/CubismSdkForNative/Samples/Resources/Hiyori/dummy_renderer"
    );

    @Test
    void resolveResourcesDir_validPath_returnsResourcesSibling() {
        Path result = ModelScanner.resolveResourcesDir("/opt/bin/renderer");
        assertNotNull(result);
        assertEquals(Path.of("/opt/bin/Resources"), result);
    }

    @Test
    void resolveResourcesDir_nullPath_returnsNull() {
        assertNull(ModelScanner.resolveResourcesDir(null));
    }

    @Test
    void resolveResourcesDir_blankPath_returnsNull() {
        assertNull(ModelScanner.resolveResourcesDir("  "));
    }

    @Test
    void resolveResourcesDir_filenameOnly_returnsNull() {
        assertNull(ModelScanner.resolveResourcesDir("renderer"));
    }

    @Test
    void scanAvailableModels_nonExistentPath_returnsEmpty() {
        List<String> result = ModelScanner.scanAvailableModels("/non/existent/renderer");
        assertTrue(result.isEmpty());
    }

    @Test
    void scanAvailableModels_nullPath_returnsEmpty() {
        List<String> result = ModelScanner.scanAvailableModels(null);
        assertTrue(result.isEmpty());
    }

    @Test
    void scanAvailableModels_validDir_findsModelsWithModel3Json(@TempDir Path tempDir) throws IOException {
        Path modelsDir = tempDir.resolve("Resources").resolve("Models");
        Files.createDirectories(modelsDir);

        Path validModel = modelsDir.resolve("TestModel");
        Files.createDirectories(validModel);
        Files.writeString(validModel.resolve("TestModel.model3.json"), validModel3Json());

        Path anotherModel = modelsDir.resolve("AnotherModel");
        Files.createDirectories(anotherModel);
        Files.writeString(anotherModel.resolve("AnotherModel.model3.json"), validModel3Json());

        Path notAModel = modelsDir.resolve("SharedAssets");
        Files.createDirectories(notAModel);
        Files.writeString(notAModel.resolve("readme.txt"), "not a model");

        String rendererPath = tempDir.resolve("renderer.exe").toString();

        List<String> models = ModelScanner.scanAvailableModels(rendererPath);

        assertEquals(2, models.size());
        assertEquals("AnotherModel", models.get(0));
        assertEquals("TestModel", models.get(1));
    }

    @Test
    void scanAvailableModels_ignoresDirsWithoutModel3Json(@TempDir Path tempDir) throws IOException {
        Path modelsDir = tempDir.resolve("Resources").resolve("Models");
        Files.createDirectories(modelsDir);

        Path noJsonDir = modelsDir.resolve("FakeModel");
        Files.createDirectories(noJsonDir);
        Files.writeString(noJsonDir.resolve("something.json"), "{}");

        String rendererPath = tempDir.resolve("renderer.exe").toString();

        List<String> models = ModelScanner.scanAvailableModels(rendererPath);

        assertTrue(models.isEmpty());
    }

    @Test
    void getModelInfo_validModel_parsesMotionsAndHitAreas(@TempDir Path tempDir) throws IOException {
        Path modelsDir = tempDir.resolve("Resources").resolve("Models");
        Path modelDir = modelsDir.resolve("MyModel");
        Files.createDirectories(modelDir);
        Files.writeString(modelDir.resolve("MyModel.model3.json"), validModel3Json());

        String rendererPath = tempDir.resolve("renderer.exe").toString();

        Optional<ModelInfo> result = ModelScanner.getModelInfo(rendererPath, "MyModel");

        assertTrue(result.isPresent());
        ModelInfo info = result.get();
        assertTrue(info.motionGroups().containsKey("Idle"));
        assertEquals(2, info.motionGroups().get("Idle"));
        assertTrue(info.motionGroups().containsKey("TapBody"));
        assertEquals(1, info.motionGroups().get("TapBody"));
        assertEquals(List.of("Head", "Body"), info.hitAreas());
        assertEquals(List.of("Happy", "Sad"), info.expressions());
    }

    @Test
    void getModelInfo_missingModel_returnsEmpty(@TempDir Path tempDir) throws IOException {
        Path modelsDir = tempDir.resolve("Resources").resolve("Models");
        Files.createDirectories(modelsDir);

        String rendererPath = tempDir.resolve("renderer.exe").toString();

        Optional<ModelInfo> result = ModelScanner.getModelInfo(rendererPath, "NonExistent");

        assertTrue(result.isEmpty());
    }

    @Test
    void getModelInfo_nullModelName_returnsEmpty() {
        Optional<ModelInfo> result = ModelScanner.getModelInfo("/some/renderer", null);
        assertTrue(result.isEmpty());
    }

    @Test
    void scanAvailableModels_realSdkResources_findsKnownModels() {
        Path sdkResources = Path.of("../third_party/CubismSdkForNative/Samples/Resources");
        if (!Files.isDirectory(sdkResources)) {
            return;
        }

        String fakRenderer = sdkResources.getParent().resolve("fake_renderer").toString();
        Path fakeResourcesDir = ModelScanner.resolveResourcesDir(fakRenderer);

        if (fakeResourcesDir == null || !fakeResourcesDir.equals(sdkResources)) {
            String rendererInSamples = sdkResources.resolveSibling("dummy_renderer").toString();
            List<String> models = ModelScanner.scanAvailableModels(rendererInSamples);
            assertTrue(models.contains("Hiyori"), "Should find Hiyori model");
            assertTrue(models.contains("Haru"), "Should find Haru model");
            assertTrue(models.size() >= 6, "Should find at least 6 models");
        }
    }

    private String validModel3Json() {
        return """
                {
                  "Version": 3,
                  "FileReferences": {
                    "Moc": "Model.moc3",
                    "Textures": ["texture_00.png"],
                    "Motions": {
                      "Idle": [
                        {"File": "motions/idle_01.motion3.json"},
                        {"File": "motions/idle_02.motion3.json"}
                      ],
                      "TapBody": [
                        {"File": "motions/tap.motion3.json"}
                      ]
                    },
                    "Expressions": [
                      {"Name": "Happy", "File": "exp/happy.exp3.json"},
                      {"Name": "Sad", "File": "exp/sad.exp3.json"}
                    ]
                  },
                  "HitAreas": [
                    {"Id": "HitArea", "Name": "Head"},
                    {"Id": "HitArea2", "Name": "Body"}
                  ]
                }
                """;
    }
}
