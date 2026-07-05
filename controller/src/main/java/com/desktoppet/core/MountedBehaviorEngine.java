package com.desktoppet.core;

import com.desktoppet.model.Envelope;
import com.desktoppet.model.VoicePackAction;
import com.desktoppet.model.VoicePackGroup;
import com.desktoppet.model.VoicePackInfo;
import com.desktoppet.network.Protocol;
import com.google.gson.JsonObject;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Random;

public class MountedBehaviorEngine {

    private static final Logger log = LoggerFactory.getLogger(MountedBehaviorEngine.class);

    private final VoicePackInfo voicePack;
    private final Random random = new Random();

    public MountedBehaviorEngine(VoicePackInfo voicePack) {
        this.voicePack = voicePack;
    }

    public boolean hasGroupForArea(String areaId) {
        Map<String, VoicePackGroup> groups = voicePack == null ? null : voicePack.groups();
        return groups != null && groups.containsKey(areaId);
    }

    public String buildMotionCommand(String areaId) {
        if (voicePack == null || voicePack.groups() == null) {
            return null;
        }

        VoicePackGroup group = voicePack.groups().get(areaId);
        if (group == null) {
            log.debug("No voice pack group for area: {}", areaId);
            return null;
        }

        List<VoicePackAction> motionActions = new ArrayList<>();
        if (group.actions() != null) {
            for (VoicePackAction action : group.actions()) {
                if (action != null && action.motionPath() != null && !action.motionPath().isEmpty()) {
                    motionActions.add(action);
                }
            }
        }

        if (motionActions.isEmpty()) {
            log.debug("No motion actions for area: {}", areaId);
            return null;
        }

        VoicePackAction chosen = motionActions.get(random.nextInt(motionActions.size()));

        Path basePath = voicePack.basePath();
        if (basePath == null) {
            return null;
        }
        String absolutePath = basePath.resolve(chosen.motionPath()).toString();

        float fadeIn = chosen.fadeInMs() / 1000.0f;
        float fadeOut = chosen.fadeOutMs() / 1000.0f;

        JsonObject payload = new JsonObject();
        payload.addProperty("motion_path", absolutePath);
        payload.addProperty("priority", group.priority());
        payload.addProperty("fade_in", fadeIn);
        payload.addProperty("fade_out", fadeOut);

        if (chosen.audioPath() != null && !chosen.audioPath().isEmpty()) {
            String audioAbsPath = basePath.resolve(chosen.audioPath()).toString();
            payload.addProperty("audio_path", audioAbsPath);
        }

        Envelope command = Protocol.createCommand("play_motion_ext", payload);
        return Protocol.serialize(command);
    }

    public String buildAudioOnlyCommand(String areaId) {
        if (voicePack == null || voicePack.groups() == null) {
            return null;
        }

        VoicePackGroup group = voicePack.groups().get(areaId);
        if (group == null) {
            log.debug("No voice pack group for area: {}", areaId);
            return null;
        }

        List<VoicePackAction> audioOnlyActions = new ArrayList<>();
        if (group.actions() != null) {
            for (VoicePackAction action : group.actions()) {
                if (action == null) {
                    continue;
                }
                boolean hasAudio = action.audioPath() != null && !action.audioPath().isEmpty();
                boolean noMotion = action.motionPath() == null || action.motionPath().isEmpty();
                if (hasAudio && noMotion) {
                    audioOnlyActions.add(action);
                }
            }
        }

        if (audioOnlyActions.isEmpty()) {
            log.debug("No audio-only actions for area: {}", areaId);
            return null;
        }

        VoicePackAction chosen = audioOnlyActions.get(random.nextInt(audioOnlyActions.size()));

        Path basePath = voicePack.basePath();
        if (basePath == null) {
            return null;
        }
        String absolutePath = basePath.resolve(chosen.audioPath()).toString();

        JsonObject payload = new JsonObject();
        payload.addProperty("audio_path", absolutePath);
        payload.addProperty("volume", 1.0);

        Envelope command = Protocol.createCommand("play_audio", payload);
        return Protocol.serialize(command);
    }

    public boolean isIdleMotionPath(String absolutePath) {
        if (voicePack == null || voicePack.groups() == null || voicePack.basePath() == null) {
            return false;
        }

        VoicePackGroup group = voicePack.groups().get("idle");
        if (group == null) {
            group = voicePack.groups().get("Idle");
        }
        if (group == null || group.actions() == null) {
            return false;
        }

        String normalizedInput = absolutePath.replace('/', '\\');
        for (VoicePackAction action : group.actions()) {
            if (action != null && action.motionPath() != null) {
                String resolved = voicePack.basePath().resolve(action.motionPath()).toString().replace('/', '\\');
                if (normalizedInput.equals(resolved)) {
                    return true;
                }
            }
        }
        return false;
    }
}
