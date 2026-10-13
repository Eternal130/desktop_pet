import QtQuick
import QtQuick.Controls
import DesktopPet

// Settings page (design doc §5). Anchor nav left + SettingRow cards right.
// Behavior fields unchanged (panelConfig / autoLaunch). Three-way
// closeAction (exit / minimize / ask) + accent swatches + theme mode.
Rectangle {
    id: root
    color: "transparent"

    readonly property color _mutedColor: Theme.text2Color

    function _scrollTo(section) {
        const map = { "behavior": behaviorCard, "startup": startupCard,
                      "appearance": appearanceCard, "plugins": pluginsCard,
                      "about": aboutCard }
        const item = map[section]
        if (item && flick.contentHeight > flick.height)
            flick.contentY = item.mapToItem(contentCol, 0, 0).y - 16
        activeSection = section
    }
    property string activeSection: "behavior"

    // Dev/QA seam (set by Main.qml on behalf of ScreenshotRunner): scroll
    // to a section on creation so below-the-fold cards (e.g. the
    // plugin-management card) can be captured. Empty for normal runs.
    property string initialAnchor: ""
    Component.onCompleted: if (initialAnchor !== "") _scrollTo(initialAnchor)

    Row {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacePage
        anchors.rightMargin: Theme.spacePage
        anchors.topMargin: Theme.spacePage
        spacing: 24

        // ── Left anchor nav ────────────────────────────────────────────
        Column {
            width: 150
            spacing: 2

            Text {
                text: qsTr("设置")
                color: Theme.textColor
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                bottomPadding: 16
            }
            AnchorNavItem { iconName: "shuffle"; text: qsTr("行为"); section: "behavior" }
            AnchorNavItem { iconName: "rocket"; text: qsTr("启动"); section: "startup" }
            AnchorNavItem { iconName: "palette"; text: qsTr("外观"); section: "appearance" }
            AnchorNavItem { iconName: "puzzle"; text: qsTr("插件"); section: "plugins" }
            AnchorNavItem { iconName: "info"; text: qsTr("关于"); section: "about" }
        }

        // ── Right settings column ──────────────────────────────────────
        Flickable {
            id: flick
            width: root.width - 2 * Theme.spacePage - 150 - 24
            height: parent.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            contentWidth: width
            contentHeight: contentCol.implicitHeight + 48
            ScrollBar.vertical: AppScrollBar {}

            Column {
                id: contentCol
                width: flick.width
                spacing: Theme.spaceGroup

                Card {
                    id: behaviorCard
                    width: parent.width
                    title: qsTr("关闭按钮行为")

                    Segmented {
                        options: [ { label: qsTr("退出程序"), value: "exit" },
                                   { label: qsTr("最小化到托盘"), value: "minimize" },
                                   { label: qsTr("每次询问"), value: "ask" } ]
                        currentValue: panelConfig.closeAction
                        onSelected: (v) => { panelConfig.closeAction = v }
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: qsTr("选择「每次询问」时将弹出确认对话框。")
                        color: root._mutedColor
                        font.pixelSize: 11
                    }
                }

                Card {
                    width: parent.width
                    title: qsTr("退出")

                    SettingRow {
                        width: parent.width
                        title: qsTr("退出前确认")
                        desc: qsTr("点击关闭按钮时弹出确认对话框")
                        ToggleSwitch {
                            anchors.verticalCenter: parent.verticalCenter
                            checked: panelConfig.confirmOnExit
                            onToggled: panelConfig.confirmOnExit = checked
                        }
                    }
                }

                Card {
                    id: startupCard
                    width: parent.width
                    title: qsTr("启动")

                    SettingRow {
                        width: parent.width
                        title: qsTr("开机自启动")
                        desc: qsTr("Windows 注册表 / Linux .desktop")
                        ToggleSwitch {
                            anchors.verticalCenter: parent.verticalCenter
                            property bool _syncing: true
                            Component.onCompleted: {
                                checked = autoLaunch.isEnabled()
                                _syncing = false
                            }
                            onToggled: {
                                if (_syncing) return
                                if (checked) {
                                    autoLaunch.enable()
                                    panelConfig.autoLaunchSystem = true
                                } else {
                                    autoLaunch.disable()
                                    panelConfig.autoLaunchSystem = false
                                }
                            }
                        }
                    }
                    SettingRow {
                        width: parent.width
                        title: qsTr("启动时最小化到托盘")
                        desc: qsTr("程序启动时窗口直接隐藏")
                        ToggleSwitch {
                            anchors.verticalCenter: parent.verticalCenter
                            checked: panelConfig.startMinimized
                            onToggled: panelConfig.startMinimized = checked
                        }
                    }
                }

                Card {
                    id: appearanceCard
                    width: parent.width
                    title: qsTr("外观")
                    hint: qsTr("深浅色与强调色跟随系统设置")

                    // palette-migration note: the in-app theme toggle and the
                    // accent swatches were removed (2026-10-13) — light/dark
                    // now follows the system color scheme and the accent
                    // follows the style/system default. The Logo section below
                    // is what remains under 外观.
                }

                // ── Logo 替换 (design §7 — lives in 外观, not its own nav) ──
                Card {
                    id: logoCard
                    width: parent.width
                    title: qsTr("Logo 替换")
                    hint: qsTr("控制面板标题栏与导航头像的图标 · 实例图标请在实例详情页更换")

                    property int selectedAssetId: assetManager.logoAssetId()

                    Row {
                        width: parent.width
                        spacing: Theme.spaceGroup

                        // left: live preview
                        Column {
                            width: (parent.width - Theme.spaceGroup) * 0.45
                            spacing: 10

                            Rectangle {
                                width: 96; height: 96
                                radius: 20
                                anchors.horizontalCenter: parent.horizontalCenter
                                // Neutral surface behind the image: user PNGs
                                // can have transparency — an accent gradient
                                // here tinted transparent logos indigo.
                                color: Theme.offBgColor
                                border.width: 1
                                border.color: Theme.borderColor
                                Image {
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    source: logoCard.selectedAssetId >= 0
                                            ? assetManager.assetInfo(
                                                  logoCard.selectedAssetId).fileUrl ?? ""
                                            : ""
                                    fillMode: Image.PreserveAspectFit
                                    visible: logoCard.selectedAssetId >= 0
                                }
                                // vector paw (was 🐾) on the neutral
                                // preview surface
                                AppIcon {
                                    anchors.centerIn: parent
                                    visible: logoCard.selectedAssetId < 0
                                    name: "paw"
                                    size: 48
                                    color: Theme.text3Color
                                }
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: logoCard.selectedAssetId >= 0
                                      ? (assetManager.assetInfo(
                                             logoCard.selectedAssetId).originalName ?? "")
                                      : qsTr("默认")
                                color: Theme.textColor
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                            }
                            Row {
                                anchors.horizontalCenter: parent.horizontalCenter
                                spacing: 8
                                AppButton {
                                    style: "primary"
                                    text: qsTr("✓ 应用 Logo")
                                    enabled: logoCard.selectedAssetId >= 0
                                             && logoCard.selectedAssetId
                                                !== assetManager.logoAssetId()
                                    onClicked: assetManager.setLogo(
                                        logoCard.selectedAssetId)
                                }
                                AppButton {
                                    style: "subtle"
                                    text: qsTr("↺ 恢复默认")
                                    onClicked: {
                                        assetManager.resetLogo()
                                        logoCard.selectedAssetId = -1
                                    }
                                }
                            }
                        }

                        // right: source grid
                        Column {
                            width: (parent.width - Theme.spaceGroup) * 0.55
                            spacing: 10

                            Flow {
                                width: parent.width
                                spacing: 10

                                // default tile
                                Rectangle {
                                    width: 72; height: 92
                                    radius: Theme.radiusMd
                                    color: logoCard.selectedAssetId < 0
                                           ? Qt.alpha(Theme.accentColor, 0.14)
                                           : Theme.surfaceColor
                                    border.width: logoCard.selectedAssetId < 0 ? 2 : 1
                                    border.color: logoCard.selectedAssetId < 0
                                                  ? Theme.accentColor : Theme.borderColor
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: logoCard.selectedAssetId = -1
                                    }
                                    Column {
                                        anchors.centerIn: parent
                                        spacing: 6
                                        Rectangle {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            width: 44; height: 44
                                            radius: 10
                                            gradient: Gradient {
                                                GradientStop { position: 0; color: "#8b8bf0" }
                                                GradientStop { position: 1; color: Theme.accentColor }
                                            }
                                            // vector paw (was 🐾) on the
                                            // accent-gradient default tile
                                            AppIcon {
                                                anchors.centerIn: parent
                                                name: "paw"
                                                size: 22
                                                color: Theme.accentTextColor
                                            }
                                        }
                                        Text {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            text: qsTr("默认")
                                            color: Theme.text2Color
                                            font.pixelSize: 11
                                        }
                                    }
                                }

                                Repeater {
                                    model: assetManager.assets()

                                    delegate: Rectangle {
                                        id: srcTile
                                        required property var modelData
                                        width: 72; height: 92
                                        radius: Theme.radiusMd
                                        color: logoCard.selectedAssetId
                                               === srcTile.modelData.id
                                               ? Qt.alpha(Theme.accentColor, 0.14)
                                               : Theme.surfaceColor
                                        border.width: logoCard.selectedAssetId
                                                      === srcTile.modelData.id ? 2 : 1
                                        border.color: logoCard.selectedAssetId
                                                      === srcTile.modelData.id
                                                      ? Theme.accentColor : Theme.borderColor
                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                if (logoCard.selectedAssetId
                                                        === srcTile.modelData.id)
                                                    logoCard.selectedAssetId = -1
                                                else
                                                    logoCard.selectedAssetId =
                                                        srcTile.modelData.id
                                            }
                                        }
                                        Column {
                                            anchors.centerIn: parent
                                            spacing: 6
                                            Rectangle {
                                                anchors.horizontalCenter: parent.horizontalCenter
                                                width: 44; height: 44
                                                radius: 10
                                                color: Theme.offBgColor
                                                clip: true
                                                Image {
                                                    anchors.fill: parent
                                                    source: srcTile.modelData.fileUrl
                                                    fillMode: Image.PreserveAspectCrop
                                                    asynchronous: true
                                                }
                                            }
                                            Text {
                                                anchors.horizontalCenter: parent.horizontalCenter
                                                text: srcTile.modelData.name
                                                color: Theme.text2Color
                                                font.pixelSize: 11
                                                elide: Text.ElideMiddle
                                            }
                                        }
                                    }
                                }
                            }
                            Text {
                                width: parent.width
                                wrapMode: Text.WordWrap
                                text: qsTr("选中即预览（未应用），点「应用 Logo」持久化；再点一次取消选中。")
                                color: root._mutedColor
                                font.pixelSize: 11
                            }
                        }
                    }

                    SettingRow {
                        width: parent.width
                        title: qsTr("同步系统托盘图标")
                        desc: qsTr("应用 Logo 时一并替换托盘图标")
                        ToggleSwitch {
                            anchors.verticalCenter: parent.verticalCenter
                            checked: assetManager.logoSyncTray
                            onToggled: assetManager.logoSyncTray = checked
                        }
                    }
                }

                // ── 插件管理 (P4, §B.6) ─────────────────────────────────
                // Functional-first card: per-plugin row with status pill,
                // error text, enable toggle. Enable/disable = config bit +
                // restart-to-apply (hot unload forbidden, §A.4) — the hint
                // line + restartPending badge say so explicitly.
                Card {
                    id: pluginsCard
                    width: parent.width
                    title: qsTr("插件管理")
                    hint: qsTr("启用/禁用为配置位，重启面板后生效（不支持热卸载）")

                    // S6 (v1.3): host-global plugin WRITE kill-switch —
                    // the plugin_write_enabled kv row the host consults
                    // LIVE on every queryApi. Flipping it revokes plugin
                    // write access (instance lifecycle / tuning / settings)
                    // without a restart. Host setting by design — never
                    // routed through the plugin settings API itself.
                    SettingRow {
                        width: parent.width
                        title: qsTr("允许插件修改面板")
                        desc: qsTr("关闭后立即吊销插件的写权限（无需重启）")
                        ToggleSwitch {
                            anchors.verticalCenter: parent.verticalCenter
                            checked: panelConfig.pluginWriteEnabled
                            onToggled: panelConfig.pluginWriteEnabled = checked
                        }
                    }

                    // empty state (fresh tree, zero plugins)
                    Text {
                        visible: pluginManager.count === 0
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: qsTr("暂无插件。拷贝 plugins/sdk-template/ 开始编写第一个插件。")
                        color: root._mutedColor
                        font.pixelSize: 12
                    }

                    Repeater {
                        model: pluginManager

                        delegate: Rectangle {
                            id: pluginRow
                            required property string pluginId
                            required property string title
                            required property string version
                            required property string status
                            required property string errorMessage
                            required property bool enabled
                            width: parent.width
                            height: col.implicitHeight + 20
                            radius: Theme.radiusMd
                            color: Theme.surfaceColor
                            border.width: 1
                            border.color: Theme.borderColor

                            Column {
                                id: col
                                x: 14; y: 10
                                width: parent.width - 28
                                spacing: 6

                                Row {
                                    spacing: 10
                                    Text {
                                        text: pluginRow.title
                                        color: Theme.textColor
                                        font.pixelSize: 13
                                        font.weight: Font.DemiBold
                                    }
                                    Text {
                                        text: "v" + pluginRow.version
                                        color: root._mutedColor
                                        font.pixelSize: 11
                                    }
                                    StatusPill {
                                        status: pluginRow.status === "started"
                                                ? "running"
                                                : (pluginRow.status === "failed"
                                                   ? "error" : "pending")
                                        label: pluginRow.status
                                    }
                                    // restart-pending badge: the persisted
                                    // bit disagrees with the runtime state
                                    Rectangle {
                                        visible: pluginManager.restartPending(
                                            pluginRow.pluginId)
                                        width: pendingText.implicitWidth + 14
                                        height: 18; radius: 9
                                        color: Qt.alpha(Theme.accentColor, 0.14)
                                        Text {
                                            id: pendingText
                                            anchors.centerIn: parent
                                            text: qsTr("重启后生效")
                                            color: Theme.accentColor
                                            font.pixelSize: 10
                                        }
                                    }
                                }
                                Text {
                                    text: pluginRow.pluginId
                                    color: root._mutedColor
                                    font.pixelSize: 11
                                }
                                Text {
                                    visible: pluginRow.errorMessage.length > 0
                                    width: parent.width
                                    wrapMode: Text.WordWrap
                                    text: qsTr("错误") + ": "
                                          + pluginRow.errorMessage
                                    color: Theme.errorColor
                                    font.pixelSize: 11
                                }
                                Row {
                                    spacing: 10
                                    Text {
                                        text: qsTr("启用")
                                        color: Theme.text2Color
                                        font.pixelSize: 12
                                    }
                                    ToggleSwitch {
                                        checked: pluginRow.enabled
                                        onToggled: pluginManager.setEnabled(
                                            pluginRow.pluginId, checked)
                                    }
                                }
                            }
                        }
                    }
                }

                Card {
                    id: aboutCard
                    width: parent.width
                    title: qsTr("关于")

                    SettingRow {
                        width: parent.width
                        title: qsTr("Desktop Pet Controller (Qt)")
                        desc: qsTr("v2.0 · Qt %1 · 原生 QML Fluent 风格").arg(
                            envChecker.qtVersion)
                    }
                    SettingRow {
                        width: parent.width
                        title: qsTr("协议覆盖")
                        desc: qsTr("25 命令 · 14 事件 · WS 127.0.0.1:9001")
                    }
                    SettingRow {
                        width: parent.width
                        title: qsTr("日志与配置")
                        desc: qsTr("~/.config/desktop-pet/ · spdlog 轮转日志")
                        AppButton {
                            style: "subtle"
                            text: qsTr("打开目录")
                            implicitHeight: 26; fontSize: 12
                            anchors.verticalCenter: parent.verticalCenter
                            onClicked: Qt.openUrlExternally(
                                QUrl.fromLocalFile(envChecker.configDirectory))
                        }
                    }
                }
            }
        }
    }

    component AnchorNavItem : Rectangle {
        id: navBtn
        property string iconName: ""
        property string text: ""
        property string section: ""
        width: 130
        height: 34
        radius: Theme.radiusMd
        readonly property bool sel: root.activeSection === section
        color: sel ? Qt.alpha(Theme.accentColor, 0.12)
            : (_hover.hovered ? Qt.alpha(Theme.textColor, 0.05) : "transparent")

        Rectangle {
            visible: navBtn.sel
            x: -8; y: parent.height * 0.25
            width: 3; height: parent.height * 0.5
            radius: 2
            color: Theme.accentColor
        }
        Row {
            anchors.centerIn: parent
            spacing: 8
            // vector anchor icon (was emoji prefixes — tofu without an
            // emoji font); tint follows the selection
            AppIcon {
                name: navBtn.iconName
                size: 15
                color: navBtn.sel ? Theme.accentColor : Theme.text2Color
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: navBtn.text
                color: navBtn.sel ? Theme.accentColor : Theme.textColor
                font.pixelSize: 13
                font.weight: navBtn.sel ? Font.DemiBold : Font.Normal
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        HoverHandler { id: _hover }
        TapHandler { onTapped: root._scrollTo(navBtn.section) }
    }
}
