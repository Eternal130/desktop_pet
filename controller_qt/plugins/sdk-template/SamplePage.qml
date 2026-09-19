import QtQuick
import QtQuick.Controls
import DesktopPet

// SamplePage — sdk-template's demo page. Shows the canonical plugin-page
// skeleton: Theme singleton (inherited through the host engine, §B.5),
// FluFrame-free plain layout (explicit heights — the project's GridLayout
// pitfall), and the per-plugin bridge access pattern.
//
// §B.5 stage-1 exposure: the host loads this file through a Loader inside
// a Repeater delegate, so the creation context chain carries the delegate's
// `model` — including the `bridge` role (PluginBridge: pluginId/title/
// notify/log). KNOWN DEBT (documented, stage-2 item): the ideal per-page
// child QQmlContext would expose ONLY "plugin"; today the context chain
// still exposes the root context properties (first-party trust accepted).
Rectangle {
    id: root
    color: "transparent"

    readonly property var plugin: (typeof model !== "undefined" && model)
                                  ? model.bridge : null

    Column {
        x: 28; y: 24
        width: root.width - 56
        spacing: 14

        Text {
            text: plugin ? plugin.title : qsTr("示例插件")
            color: Theme.textColor
            font.pixelSize: 22
            font.weight: Font.DemiBold
        }
        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("这是 SDK 模板（plugins/sdk-template）的示例页面。拷贝该目录并修改 "
                     + "plugin.json 的 id 即可开始编写你自己的插件。页面通过宿主引擎加载，"
                     + "主题单例自动可用。")
            color: Theme.text2Color
            font.pixelSize: 13
        }

        // Identity card — proves the bridge wiring end-to-end.
        Rectangle {
            width: Math.min(parent.width, 520)
            height: infoCol.implicitHeight + 28
            radius: Theme.radiusMd
            color: Theme.surfaceColor
            border.width: 1
            border.color: Theme.borderColor

            Column {
                id: infoCol
                x: 16; y: 14
                width: parent.width - 32
                spacing: 6

                Text {
                    text: qsTr("插件 ID") + ": " + (plugin ? plugin.pluginId : "?")
                    color: Theme.textColor
                    font.pixelSize: 13
                }
                Text {
                    text: qsTr("宿主 API") + ": "
                          + (plugin ? plugin.apiVersion : "?")
                    color: Theme.text2Color
                    font.pixelSize: 12
                }
            }
        }

        AppButton {
            style: "subtle"
            text: qsTr("发一个气泡试试")
            onClicked: if (plugin) plugin.notify(qsTr("来自示例插件的气泡 ✨"))
        }
    }
}
