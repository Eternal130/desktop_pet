package com.desktoppet.model;

import java.util.List;

public record PanelConfig(
    double panelX,
    double panelY,
    double panelWidth,
    double panelHeight,
    String theme,
    List<String> instanceIds
) {
    public static PanelConfig defaults() {
        return new PanelConfig(-1, -1, 1200, 760, "深紫梦幻", List.of());
    }
}
