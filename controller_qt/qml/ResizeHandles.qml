import QtQuick

// Eight-direction edge resize handles for a frameless window.
//
// Adds four edge strips (N/S/E/W) and four corner squares (NW/NE/SW/SE).
// Each MouseArea calls QWindow::startSystemResize(Qt::Edges) on press:
//   * edges pass a single Qt.<Edge>
//   * corners pass two OR'd edges (e.g. Qt.TopEdge | Qt.LeftEdge)
//
// Qt::Edges is a flag-combination type; startSystemResize accepts the whole
// combination in one call (the spec's two-call variant is the wrong Qt 6 API).
//
// Corner squares have higher z than edge strips so a press that lands in the
// overlap zone is handled as a corner resize, not an edge resize.
//
// Parent must be an ApplicationWindow. The containing window is resolved via
// the attached property `Window.window`.
Item {
    id: root
    anchors.fill: parent

    // Tunables. Edges are narrow (6px) so they stay invisible; corners are
    // larger (12px) because grabbing a precise corner needs a bigger hit area.
    readonly property int edgeSize: 6
    readonly property int cornerSize: 12

    // ── Edges (z = 1) ─────────────────────────────────────────────────────

    // Top (N)
    MouseArea {
        z: 1
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: root.edgeSize
        cursorShape: Qt.SizeVerCursor
        onPressed: root.Window.window.startSystemResize(Qt.TopEdge)
    }

    // Bottom (S)
    MouseArea {
        z: 1
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: root.edgeSize
        cursorShape: Qt.SizeVerCursor
        onPressed: root.Window.window.startSystemResize(Qt.BottomEdge)
    }

    // Left (W)
    MouseArea {
        z: 1
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: root.edgeSize
        cursorShape: Qt.SizeHorCursor
        onPressed: root.Window.window.startSystemResize(Qt.LeftEdge)
    }

    // Right (E)
    MouseArea {
        z: 1
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: root.edgeSize
        cursorShape: Qt.SizeHorCursor
        onPressed: root.Window.window.startSystemResize(Qt.RightEdge)
    }

    // ── Corners (z = 2, above edges) ──────────────────────────────────────
    // Cursor convention:
    //   SizeFDiagCursor ( \ ) → TopLeft & BottomRight
    //   SizeBDiagCursor ( / ) → TopRight & BottomLeft

    // Top-Left (NW)
    MouseArea {
        z: 2
        anchors.top: parent.top
        anchors.left: parent.left
        width: root.cornerSize
        height: root.cornerSize
        cursorShape: Qt.SizeFDiagCursor
        onPressed: root.Window.window.startSystemResize(Qt.TopEdge | Qt.LeftEdge)
    }

    // Top-Right (NE)
    MouseArea {
        z: 2
        anchors.top: parent.top
        anchors.right: parent.right
        width: root.cornerSize
        height: root.cornerSize
        cursorShape: Qt.SizeBDiagCursor
        onPressed: root.Window.window.startSystemResize(Qt.TopEdge | Qt.RightEdge)
    }

    // Bottom-Left (SW)
    MouseArea {
        z: 2
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        width: root.cornerSize
        height: root.cornerSize
        cursorShape: Qt.SizeBDiagCursor
        onPressed: root.Window.window.startSystemResize(Qt.BottomEdge | Qt.LeftEdge)
    }

    // Bottom-Right (SE)
    MouseArea {
        z: 2
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: root.cornerSize
        height: root.cornerSize
        cursorShape: Qt.SizeFDiagCursor
        onPressed: root.Window.window.startSystemResize(Qt.BottomEdge | Qt.RightEdge)
    }
}
