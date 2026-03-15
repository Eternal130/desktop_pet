package com.desktoppet.model;

import java.util.List;
import java.util.Map;

public record ModelConfig(
    String modelPath,
    Map<String, HitAction> hitActions,
    List<String> idleMotions,
    String defaultExpression
) {}
