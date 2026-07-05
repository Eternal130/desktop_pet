package com.desktoppet.core;

import com.desktoppet.model.Envelope;
import com.desktoppet.model.HitAction;
import com.desktoppet.model.ModelConfig;
import com.desktoppet.model.VoicePackAction;
import com.desktoppet.model.VoicePackGroup;
import com.desktoppet.model.VoicePackInfo;
import com.desktoppet.network.MessageDispatcher;
import com.desktoppet.network.PetWebSocketServer;
import com.desktoppet.network.Protocol;
import com.google.gson.JsonObject;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;

import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.nio.file.Path;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.atLeastOnce;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verify;

/**
 * Integration tests for the AppOrchestrator hit-event chain, covering the
 * Task 4 audio-only wiring: when a voice-pack group has only audio-only actions,
 * play_audio is dispatched AND the fall-through to InteractionHandler is preserved
 * (Metis W1 — action feedback must still fire).
 */
class AppOrchestratorTest {

    private AppOrchestrator orchestrator;
    private PetWebSocketServer mockServer;

    @AfterEach
    void tearDown() throws Exception {
        if (orchestrator != null) {
            Scheduler scheduler = (Scheduler) getField(orchestrator, "scheduler");
            scheduler.shutdown();
        }
    }

    @Test
    void hit_audioOnlyGroup_sendsPlayAudioAndPreservesMotionFallback() throws Exception {
        orchestrator = new AppOrchestrator();
        injectMockWebSocketServer();
        MountedBehaviorEngine engine = buildTestEngine();
        setField(orchestrator, "mountedEngine", engine);

        PetStateManager stateManager = (PetStateManager) getField(orchestrator, "stateManager");
        stateManager.setConnected(true);

        InteractionHandler interactionHandler =
                (InteractionHandler) getField(orchestrator, "interactionHandler");
        interactionHandler.setModelConfig(new ModelConfig(
                "TestModel",
                Map.of("audio_only", new HitAction("AudioFallback", 2)),
                List.of(),
                ""
        ));

        invokeRegisterEventHandlers(orchestrator);

        MessageDispatcher dispatcher = (MessageDispatcher) getField(orchestrator, "dispatcher");
        dispatcher.dispatch(hitEvent("audio_only"));

        java.util.List<String> sent = captureAllSent();
        assertFalse(sent.isEmpty(), "at least one command should be sent");

        boolean foundPlayAudio = false;
        boolean foundPlayMotion = false;
        for (String msg : sent) {
            Envelope env = Protocol.deserialize(msg)
                    .orElseThrow(() -> new AssertionError("invalid envelope: " + msg));
            switch (env.action()) {
                case "play_audio" -> foundPlayAudio = true;
                case "play_motion" -> {
                    foundPlayMotion = true;
                    assertEquals("AudioFallback", env.payload().get("group").getAsString());
                }
                default -> { /* ignore */
                }
            }
        }
        assertTrue(foundPlayAudio, "play_audio should be sent for audio-only group");
        assertTrue(foundPlayMotion,
                "play_motion fallback should still fire (Metis W1 — no early return)");
    }

    @Test
    void hit_motionGroup_sendsOnlyPlayMotionExt() throws Exception {
        orchestrator = new AppOrchestrator();
        injectMockWebSocketServer();
        MountedBehaviorEngine engine = buildTestEngine();
        setField(orchestrator, "mountedEngine", engine);

        PetStateManager stateManager = (PetStateManager) getField(orchestrator, "stateManager");
        stateManager.setConnected(true);

        invokeRegisterEventHandlers(orchestrator);

        MessageDispatcher dispatcher = (MessageDispatcher) getField(orchestrator, "dispatcher");
        dispatcher.dispatch(hitEvent("tap_head"));

        java.util.List<String> sent = captureAllSent();
        assertEquals(1, sent.size(), "only play_motion_ext should be sent for motion group");

        Envelope env = Protocol.deserialize(sent.get(0)).orElseThrow();
        assertEquals("play_motion_ext", env.action());
        assertTrue(env.payload().has("motion_path"));

        for (String msg : sent) {
            Envelope e = Protocol.deserialize(msg).orElseThrow();
            assertFalse("play_audio".equals(e.action()),
                    "play_audio must NOT be sent when motion command exists");
        }
    }

    private void injectMockWebSocketServer() throws Exception {
        mockServer = mock(PetWebSocketServer.class);
        setField(orchestrator, "wsServer", mockServer);
    }

    private java.util.List<String> captureAllSent() {
        org.mockito.ArgumentCaptor<String> captor =
                org.mockito.ArgumentCaptor.forClass(String.class);
        verify(mockServer, atLeastOnce()).sendToInstance(eq(0), captor.capture());
        return new java.util.ArrayList<>(captor.getAllValues());
    }

    private static MountedBehaviorEngine buildTestEngine() {
        Path basePath = Path.of("C:", "fake", "voice-pack");

        List<VoicePackAction> tapHeadActions = List.of(
                new VoicePackAction(1, "motions/33.motion3.json", "audios/33.ogg",
                        null, "Tap head", 1000L, 1000L)
        );

        List<VoicePackAction> audioOnlyActions = List.of(
                new VoicePackAction(2, null, "audios/voice.ogg",
                        null, "Voice only", 1000L, 1000L)
        );

        Map<String, VoicePackGroup> groups = new LinkedHashMap<>();
        groups.put("tap_head", new VoicePackGroup("tap_head", "Touch Head", 2, tapHeadActions));
        groups.put("audio_only", new VoicePackGroup("audio_only", "Audio Only", 2, audioOnlyActions));

        VoicePackInfo voicePack = new VoicePackInfo(
                "test-pack", "Test Pack", "test-code", basePath, groups, List.of());
        return new MountedBehaviorEngine(voicePack);
    }

    private static Envelope hitEvent(String areaId) {
        JsonObject payload = new JsonObject();
        payload.addProperty("area_id", areaId);
        return new Envelope("event", "hit", "evt-test", payload,
                1710000000000L, null, null, null);
    }

    private static void invokeRegisterEventHandlers(AppOrchestrator orch) throws Exception {
        Method m = AppOrchestrator.class.getDeclaredMethod("registerEventHandlers");
        m.setAccessible(true);
        m.invoke(orch);
    }

    private static void setField(Object target, String name, Object value) throws Exception {
        Field f = target.getClass().getDeclaredField(name);
        f.setAccessible(true);
        f.set(target, value);
    }

    private static Object getField(Object target, String name) throws Exception {
        Field f = target.getClass().getDeclaredField(name);
        f.setAccessible(true);
        return f.get(target);
    }
}
