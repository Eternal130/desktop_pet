package com.desktoppet.model;

import java.util.List;

public record PanelState(
    double panelX,
    double panelY,
    double panelWidth,
    double panelHeight,
    String theme,
    List<InstanceState> instances
) {
    public static PanelState defaults() {
        return new PanelState(-1, -1, 1200, 760, "深紫梦幻", List.of());
    }
}
