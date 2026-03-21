package com.desktoppet.core;

import com.desktoppet.bundle.Action;
import com.desktoppet.bundle.ActionGroup;
import com.desktoppet.bundle.Bundle;
import com.desktoppet.model.VoicePackAction;
import com.desktoppet.model.VoicePackGroup;
import com.desktoppet.model.VoicePackInfo;
import com.desktoppet.model.VoicePackModule;
import com.google.protobuf.InvalidProtocolBufferException;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;

public final class MetaMkoParser {

    private static final Logger log = LoggerFactory.getLogger(MetaMkoParser.class);

    private MetaMkoParser() {}

    /**
     * Parses the meta.mko file in {@code voicePackDir} and returns a {@link VoicePackInfo}.
     * Returns null if the file cannot be read or parsed.
     *
     * @param voicePackDir directory containing meta.mko
     * @return parsed VoicePackInfo, or null on failure
     */
    public static VoicePackInfo parse(Path voicePackDir) {
        Path mkoPath = voicePackDir.resolve("meta.mko");
        byte[] bytes;
        try {
            bytes = Files.readAllBytes(mkoPath);
        } catch (IOException e) {
            log.warn("Cannot read meta.mko at {}: {}", mkoPath, e.getMessage());
            return null;
        }

        Bundle bundle;
        try {
            bundle = Bundle.parseFrom(bytes);
        } catch (InvalidProtocolBufferException e) {
            log.warn("Cannot parse meta.mko at {}: {}", mkoPath, e.getMessage());
            return null;
        }

        String dirName = voicePackDir.getFileName().toString();
        String displayName = bundle.hasMeta() ? bundle.getMeta().getName() : dirName;
        String code = bundle.hasMeta() ? bundle.getMeta().getCode() : "";

        // Collect actions per group code first (avoids mutation through record accessor)
        Map<String, List<VoicePackAction>> actionsByGroup = new HashMap<>();
        for (Action a : bundle.getActionsList()) {
            String lipSync = a.hasLipSync() ? a.getLipSync() : null;
            actionsByGroup.computeIfAbsent(a.getGroup(), k -> new ArrayList<>()).add(
                new VoicePackAction(
                    a.getId(),
                    a.getMotion().isEmpty() ? null : a.getMotion(),
                    a.getAudio().isEmpty() ? null : a.getAudio(),
                    lipSync,
                    a.getDoc(),
                    a.getFadeIn(),
                    a.getFadeOut()
                )
            );
        }

        Map<String, VoicePackGroup> groups = new LinkedHashMap<>();
        for (ActionGroup ag : bundle.getGroupsList()) {
            List<VoicePackAction> actions = actionsByGroup.getOrDefault(ag.getCode(), List.of());
            groups.put(ag.getCode(), new VoicePackGroup(
                ag.getCode(), ag.getName(), ag.getPriority(), actions
            ));
        }

        List<VoicePackModule> modules = bundle.getModulesList().stream()
            .map(m -> new VoicePackModule(m.getKey(), m.getPriority(), m.getFile()))
            .collect(Collectors.toList());

        return new VoicePackInfo(dirName, displayName, code, voicePackDir, groups, modules);
    }
}
