package com.desktoppet.core;

import com.desktoppet.model.ModelInfo;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Optional;

import static org.junit.jupiter.api.Assertions.*;

class ModelInfoParserTest {

    // Path relative to controller/ working directory (where mvn test runs)
    private static final Path HIYORI_MODEL3 = Path.of(
            "../third_party/CubismSdkForNative/Samples/Resources/Hiyori/Hiyori.model3.json"
    );

    @Test
    void parse_validModel_extractsMotionGroups() {
        Optional<ModelInfo> result = ModelInfoParser.parse(HIYORI_MODEL3);

        assertTrue(result.isPresent(), "Should parse Hiyori model3.json successfully");
        ModelInfo info = result.get();

        // Hiyori has Idle (9 motions) and TapBody (1 motion)
        assertFalse(info.motionGroups().isEmpty(), "motionGroups should not be empty");
        assertTrue(info.motionGroups().containsKey("Idle"), "Should contain 'Idle' motion group");
        assertEquals(9, info.motionGroups().get("Idle"), "Idle group should have 9 motions");
        assertTrue(info.motionGroups().containsKey("TapBody"), "Should contain 'TapBody' motion group");
        assertEquals(1, info.motionGroups().get("TapBody"), "TapBody group should have 1 motion");
    }

    @Test
    void parse_validModel_extractsHitAreas() {
        Optional<ModelInfo> result = ModelInfoParser.parse(HIYORI_MODEL3);

        assertTrue(result.isPresent(), "Should parse Hiyori model3.json successfully");
        ModelInfo info = result.get();

        // Hiyori has HitAreas: [{"Id": "HitArea", "Name": "Body"}]
        assertFalse(info.hitAreas().isEmpty(), "hitAreas should not be empty");
        assertTrue(info.hitAreas().contains("Body"), "hitAreas should contain 'Body'");
    }

    @Test
    void parse_validModel_expressionsIsEmptyWhenMissing() {
        // Hiyori model3.json has no Expressions section — should return empty list, not crash
        Optional<ModelInfo> result = ModelInfoParser.parse(HIYORI_MODEL3);

        assertTrue(result.isPresent(), "Should parse Hiyori model3.json successfully");
        ModelInfo info = result.get();

        // Hiyori has no Expressions section, so list should be empty
        assertNotNull(info.expressions(), "expressions list should not be null");
        assertTrue(info.expressions().isEmpty(), "expressions should be empty when section is absent");
    }

    @Test
    void parse_nonExistentFile_returnsEmpty() {
        Path nonExistent = Path.of("/non/existent/file.json");

        Optional<ModelInfo> result = ModelInfoParser.parse(nonExistent);

        assertTrue(result.isEmpty(), "Should return Optional.empty() for non-existent file");
    }

    @Test
    void parse_invalidJson_returnsEmpty(@TempDir Path tempDir) throws IOException {
        Path invalidJson = tempDir.resolve("invalid.model3.json");
        Files.writeString(invalidJson, "not valid json at all {{{");

        Optional<ModelInfo> result = ModelInfoParser.parse(invalidJson);

        assertTrue(result.isEmpty(), "Should return Optional.empty() for invalid JSON");
    }

    @Test
    void parse_missingMotionsSection_emptyGroups(@TempDir Path tempDir) throws IOException {
        // JSON with FileReferences but no Motions key
        String json = """
                {
                  "Version": 3,
                  "FileReferences": {
                    "Moc": "Model.moc3"
                  },
                  "HitAreas": []
                }
                """;
        Path modelFile = tempDir.resolve("noMotions.model3.json");
        Files.writeString(modelFile, json);

        Optional<ModelInfo> result = ModelInfoParser.parse(modelFile);

        assertTrue(result.isPresent(), "Should parse successfully even without Motions section");
        ModelInfo info = result.get();
        assertTrue(info.motionGroups().isEmpty(), "motionGroups should be empty when Motions section is absent");
        assertTrue(info.expressions().isEmpty(), "expressions should be empty when section is absent");
        assertTrue(info.hitAreas().isEmpty(), "hitAreas should be empty when HitAreas array is empty");
    }
}
