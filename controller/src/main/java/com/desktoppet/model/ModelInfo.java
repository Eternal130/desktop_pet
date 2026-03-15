package com.desktoppet.model;

import java.util.List;
import java.util.Map;

/** Parsed from model3.json. motionGroups: key=group name, value=count */
public record ModelInfo(
    Map<String, Integer> motionGroups,
    List<String> expressions,
    List<String> hitAreas
) {}
