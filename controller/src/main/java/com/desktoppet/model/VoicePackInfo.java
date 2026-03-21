package com.desktoppet.model;

import java.nio.file.Path;
import java.util.List;
import java.util.Map;

public record VoicePackInfo(
    String dirName,
    String displayName,
    String code,
    Path basePath,
    Map<String, VoicePackGroup> groups,
    List<VoicePackModule> modules
) {}
