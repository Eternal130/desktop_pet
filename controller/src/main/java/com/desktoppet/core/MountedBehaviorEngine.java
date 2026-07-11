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
import java.util.Set;

public class MountedBehaviorEngine {

    private static final Logger log = LoggerFactory.getLogger(MountedBehaviorEngine.class);

    public record BehaviorResult(
        String commandJson,
        String subtitleText,
        long audioDurationMs
    ) {}

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
        BehaviorResult result = buildBehaviorCommand(areaId);
        return result != null ? result.commandJson() : null;
    }

    public BehaviorResult buildBehaviorCommand(String areaId) {
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
        log.info("buildBehaviorCommand areaId={} motion={} audio={} lipSync={}",
                areaId, chosen.motionPath(), chosen.audioPath(), chosen.lipSyncPath());

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

        if (chosen.lipSyncPath() != null && !chosen.lipSyncPath().isEmpty()) {
            payload.addProperty("lip_sync_path", basePath.resolve(chosen.lipSyncPath()).toString());
        }

        Envelope command = Protocol.createCommand("play_motion_ext", payload);
        String commandJson = Protocol.serialize(command);

        String subtitleText = chosen.doc();

        long audioDurationMs = 5000L;
        if (chosen.audioPath() != null && !chosen.audioPath().isEmpty()) {
            audioDurationMs = estimateOggDurationMs(basePath.resolve(chosen.audioPath()));
        }

        return new BehaviorResult(commandJson, subtitleText, audioDurationMs);
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

    /** Returns the set of voice pack group codes (area IDs). Empty if no voice pack loaded. */
    public Set<String> groupNames() {
        if (voicePack == null || voicePack.groups() == null) {
            return Set.of();
        }
        return voicePack.groups().keySet();
    }

    private static long estimateOggDurationMs(Path audioPath) {
        if (audioPath == null) return 5000L;
        java.io.File file = audioPath.toFile();
        if (!file.exists() || file.length() < 85) return 5000L;

        try (var raf = new java.io.RandomAccessFile(file, "r")) {
            int headerLen = (int) Math.min(85, raf.length());
            byte[] header = new byte[headerLen];
            raf.seek(0);
            raf.readFully(header);

            int vorbisOff = -1;
            for (int i = 0; i < header.length - 7; i++) {
                if (header[i] == 0x01 && header[i+1] == 'v' && header[i+2] == 'o'
                    && header[i+3] == 'r' && header[i+4] == 'b' && header[i+5] == 'i'
                    && header[i+6] == 's') {
                    vorbisOff = i;
                    break;
                }
            }
            if (vorbisOff < 0 || vorbisOff + 16 > header.length) return 5000L;

            int sampleRate = (header[vorbisOff + 12] & 0xFF)
                           | ((header[vorbisOff + 13] & 0xFF) << 8)
                           | ((header[vorbisOff + 14] & 0xFF) << 16)
                           | ((header[vorbisOff + 15] & 0xFF) << 24);
            if (sampleRate <= 0) return 5000L;

            long fileLen = raf.length();
            int tailSize = (int) Math.min(65536, fileLen);
            byte[] tail = new byte[tailSize];
            raf.seek(fileLen - tailSize);
            raf.readFully(tail);

            for (int i = tail.length - 27; i >= 0; i--) {
                if (tail[i] == 'O' && i + 14 <= tail.length
                    && tail[i+1] == 'g' && tail[i+2] == 'g' && tail[i+3] == 'S') {
                    long granule = 0;
                    for (int j = 0; j < 8; j++) {
                        granule |= ((long)(tail[i + 6 + j] & 0xFF)) << (j * 8);
                    }
                    if (granule > 0) {
                        return (granule * 1000L) / sampleRate;
                    }
                }
            }
            return 5000L;
        } catch (Exception e) {
            return 5000L;
        }
    }
}
