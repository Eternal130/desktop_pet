package com.desktoppet.model;

import java.util.List;

public record VoicePackGroup(
    String code,
    String name,
    int priority,
    List<VoicePackAction> actions
) {}
