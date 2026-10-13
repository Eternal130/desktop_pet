import QtQuick
import DesktopPet

// StatusPill — rounded status badge with a leading dot.
// running → green · connecting/pending → amber · error → red · else gray.
Rectangle {
    id: root

    property string status: "stopped"
    property string label: ""       // override text; defaults to mapped `status`

    // Built-in Chinese mapping for the known statuses; unknown statuses
    // still fall back to the raw string. One change covers every call
    // site — explicit `label` overrides keep winning.
    readonly property string _mappedLabel: {
        if (status === "running")    return qsTr("运行中")
        if (status === "connecting") return qsTr("连接中")
        if (status === "pending")    return qsTr("启动中")
        if (status === "error")      return qsTr("异常")
        if (status === "stopped")    return qsTr("已停止")
        return status
    }

    readonly property color _c: {
        if (status === "running")                            return Theme.successColor
        if (status === "connecting" || status === "pending") return Theme.warningColor
        if (status === "error")                              return Theme.errorColor
        return Theme.text2Color
    }

    height: 22
    width: row.implicitWidth + 18
    radius: height / 2
    color: Qt.rgba(_c.r, _c.g, _c.b, 0.16)

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 6
        Rectangle {
            width: 7; height: 7; radius: 4
            anchors.verticalCenter: parent.verticalCenter
            color: root._c
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.label.length > 0 ? root.label : root._mappedLabel
            color: root._c
            font.pixelSize: 11
            font.capitalization: Font.AllUppercase
            font.weight: Font.DemiBold
        }
    }
}
