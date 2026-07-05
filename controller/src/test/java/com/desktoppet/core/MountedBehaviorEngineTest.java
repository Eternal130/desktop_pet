package com.desktoppet.core;

import com.desktoppet.model.Envelope;
import com.desktoppet.model.VoicePackAction;
import com.desktoppet.model.VoicePackGroup;
import com.desktoppet.model.VoicePackInfo;
import com.desktoppet.network.Protocol;
import com.google.gson.JsonObject;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

import java.nio.file.Path;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

class MountedBehaviorEngineTest {

    private MountedBehaviorEngine engine;
    private Path basePath;

    @BeforeEach
    void setUp() {
        basePath = Path.of("C:", "fake", "voice-pack");

        List<VoicePackAction> tapHeadActions = List.of(
            new VoicePackAction(1, "motions/33.motion3.json", "audios/33.ogg", null, "Hello!", 1000L, 1000L),
            new VoicePackAction(2, "motions/34.motion3.json", null, null, "Hi!", 500L, 500L)
        );

        List<VoicePackAction> audioOnlyActions = List.of(
            new VoicePackAction(3, null, "audios/1.ogg", null, "Audio only", 1000L, 1000L)
        );

        Map<String, VoicePackGroup> groups = new LinkedHashMap<>();
        groups.put("tap_head", new VoicePackGroup("tap_head", "触摸头部", 2, tapHeadActions));
        groups.put("audio_only", new VoicePackGroup("audio_only", "Audio only", 2, audioOnlyActions));

        VoicePackInfo voicePack = new VoicePackInfo("test-pack", "Test Pack", "test-code", basePath, groups, List.of());
        engine = new MountedBehaviorEngine(voicePack);
    }

    @Test
    void hasGroupForArea_returnsTrueForKnownArea() {
        assertTrue(engine.hasGroupForArea("tap_head"));
        assertFalse(engine.hasGroupForArea("unknown_area"));
    }

    @Test
    void buildMotionCommand_forTapHead_returnsValidCommandEnvelope() {
        String json = engine.buildMotionCommand("tap_head");
        assertNotNull(json);

        Envelope envelope = Protocol.deserialize(json)
            .orElseThrow(() -> new AssertionError("Expected valid envelope JSON"));

        assertEquals("command", envelope.type());
        assertEquals("play_motion_ext", envelope.action());

        JsonObject payload = envelope.payload();
        assertTrue(payload.has("motion_path"));
        assertTrue(payload.has("priority"));
        assertTrue(payload.has("fade_in"));
        assertTrue(payload.has("fade_out"));
        assertEquals(2, payload.get("priority").getAsInt());

        String motionPath = payload.get("motion_path").getAsString();
        assertTrue(
            motionPath.endsWith("33.motion3.json") || motionPath.endsWith("34.motion3.json"),
            "Unexpected motion path: " + motionPath
        );
    }

    @Test
    void buildMotionCommand_forUnknownArea_returnsNull() {
        assertNull(engine.buildMotionCommand("unknown_area"));
    }

    @Test
    void buildMotionCommand_forAudioOnlyArea_returnsNull() {
        assertNull(engine.buildMotionCommand("audio_only"));
    }

    @Test
    void buildAudioOnlyCommand_forAudioOnlyArea_returnsPlayAudio() {
        String json = engine.buildAudioOnlyCommand("audio_only");
        assertNotNull(json);

        Envelope envelope = Protocol.deserialize(json)
            .orElseThrow(() -> new AssertionError("Expected valid envelope JSON"));

        assertEquals("command", envelope.type());
        assertEquals("play_audio", envelope.action());

        JsonObject payload = envelope.payload();
        assertNotNull(payload.get("audio_path"));
        assertTrue(payload.get("audio_path").getAsString().endsWith("1.ogg"),
            "Unexpected audio path: " + payload.get("audio_path"));
    }

    @Test
    void buildAudioOnlyCommand_forMotionGroup_returnsNull() {
        assertNull(engine.buildAudioOnlyCommand("tap_head"));
    }

    @Test
    void buildMotionCommand_convertsFadeMillisecondsToSeconds() {
        for (int i = 0; i < 20; i++) {
            String json = engine.buildMotionCommand("tap_head");
            assertNotNull(json);

            Envelope envelope = Protocol.deserialize(json)
                .orElseThrow(() -> new AssertionError("Expected valid envelope JSON"));

            float fadeIn = envelope.payload().get("fade_in").getAsFloat();
            float fadeOut = envelope.payload().get("fade_out").getAsFloat();

            boolean validFadeIn = Float.compare(fadeIn, 1.0f) == 0 || Float.compare(fadeIn, 0.5f) == 0;
            boolean validFadeOut = Float.compare(fadeOut, 1.0f) == 0 || Float.compare(fadeOut, 0.5f) == 0;

            assertTrue(validFadeIn, "fade_in should be seconds (0.5 or 1.0), got: " + fadeIn);
            assertTrue(validFadeOut, "fade_out should be seconds (0.5 or 1.0), got: " + fadeOut);
        }
    }
}
