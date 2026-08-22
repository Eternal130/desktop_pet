import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import DesktopPet

// 资源管理 page (design doc §6). Image library: upload / delete / preview.
// Images are content-hash deduped PNGs under assets/uploads/; metadata +
// references live in app.db. Deleting an instance never deletes its image.
Rectangle {
    id: root
    color: "transparent"

    signal requestSwitchPage(string name)

    readonly property color _mutedColor: Theme.text2Color
    readonly property color _faintColor: Theme.text3Color

    property int filterMode: 0        // 0=全部 1=已使用 2=未使用
    property var assetList: []        // refreshed via assetsChanged
    property var previewAsset: ({})   // assetInfo map for the preview dialog
    property var pendingDelete: ({})  // asset map awaiting confirm

    function refresh() {
        assetList = assetManager.assets()
    }

    function visibleAssets() {
        const out = []
        for (let i = 0; i < assetList.length; ++i) {
            const a = assetList[i]
            if (filterMode === 1 && !a.inUse) continue
            if (filterMode === 2 && a.inUse) continue
            out.push(a)
        }
        return out
    }

    function fmtSize(bytes) {
        if (bytes >= 1048576)
            return (bytes / 1048576).toFixed(1) + " MB"
        return Math.max(1, Math.round(bytes / 1024)) + " KB"
    }

    function fmtDate(iso) {
        return String(iso).substring(0, 10)
    }

    function importUrls(urls) {
        let dup = 0, bad = 0
        for (let i = 0; i < urls.length; ++i) {
            const r = assetManager.importImage(urls[i])
            if (r === "duplicate") ++dup
            else if (r !== "ok") ++bad
        }
        if (bad > 0)
            toast.text = qsTr("%1 个文件导入失败（无法解码）").arg(bad)
        else if (dup > 0)
            toast.text = qsTr("%1 个图片已存在，已跳过").arg(dup)
        else
            toast.text = qsTr("导入成功")
        toast.visible = true
        toastTimer.restart()
    }

    Component.onCompleted: refresh()
    Connections {
        target: assetManager
        function onAssetsChanged() { root.refresh() }
    }

    FileDialog {
        id: uploadDialog
        title: qsTr("上传图片")
        fileMode: FileDialog.OpenFiles
        nameFilters: [ qsTr("图片文件 (*.png *.jpg *.jpeg *.webp *.ico)") ]
        onAccepted: root.importUrls(selectedFiles)
    }

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: col.implicitHeight + 48
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar {}

        Column {
            id: col
            x: Theme.spacePage
            y: 24
            width: root.width - 2 * Theme.spacePage
            spacing: Theme.spaceGroup

            // ── Title row ─────────────────────────────────────────────
            Item {
                width: parent.width
                height: 36
                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("资源管理")
                    color: Theme.textColor
                    font.pixelSize: 22
                    font.weight: Font.DemiBold
                }
                Row {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8
                    AppButton {
                        style: "subtle"
                        text: qsTr("📂 打开资源目录")
                        onClicked: assetManager.openAssetsDir()
                    }
                    AppButton {
                        style: "subtle"
                        text: qsTr("🖌 去设置换 Logo")
                        onClicked: root.requestSwitchPage("settings")
                    }
                    AppButton {
                        style: "primary"
                        text: qsTr("⬆ 上传图片")
                        onClicked: uploadDialog.open()
                    }
                }
            }

            Text {
                width: parent.width
                text: qsTr("支持 PNG / JPG / WEBP / ICO · 导入时统一转存 PNG 并按内容哈希去重 · 总量 %1 张").arg(
                    assetList.length)
                color: _faintColor
                font.pixelSize: 12
            }

            // ── Filter row ────────────────────────────────────────────
            Row {
                spacing: 12
                Segmented {
                    options: [ { label: qsTr("全部"), value: "all" },
                               { label: qsTr("已使用"), value: "used" },
                               { label: qsTr("未使用"), value: "unused" } ]
                    currentValue: ["all", "used", "unused"][root.filterMode]
                    onSelected: (v) => {
                        root.filterMode = v === "used" ? 1
                                      : v === "unused" ? 2 : 0
                    }
                }
            }

            // ── Asset grid ────────────────────────────────────────────
            GridLayout {
                width: parent.width
                columns: Math.max(2, Math.floor(width / 220))
                columnSpacing: Theme.spaceGroup
                rowSpacing: Theme.spaceGroup

                Repeater {
                    model: root.visibleAssets()

                    delegate: Rectangle {
                        id: card
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        height: 252
                        radius: Theme.radiusLg
                        color: Theme.surfaceColor
                        border.width: 1
                        border.color: Theme.dark ? "#ffffff14" : Theme.hairlineColor

                        Column {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 8

                            Rectangle {
                                width: parent.width
                                height: 110
                                radius: Theme.radiusMd
                                color: Theme.offBgColor
                                Image {
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    source: card.modelData.fileUrl
                                    fillMode: Image.PreserveAspectFit
                                    asynchronous: true
                                }
                            }
                            Text {
                                width: parent.width
                                elide: Text.ElideMiddle
                                text: card.modelData.name
                                color: Theme.textColor
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                            }
                            Text {
                                width: parent.width
                                text: qsTr("%1×%2 · %3 · %4").arg(
                                    card.modelData.width).arg(
                                    card.modelData.height).arg(
                                    root.fmtSize(card.modelData.sizeBytes)).arg(
                                    root.fmtDate(card.modelData.uploadedAt))
                                color: _mutedColor
                                font.pixelSize: 11
                            }
                            StatusPill {
                                status: card.modelData.inUse ? "running" : "stopped"
                                label: card.modelData.inUse
                                       ? (card.modelData.refsLabel === "logo"
                                          ? qsTr("面板 Logo")
                                          : qsTr("实例图标"))
                                       : qsTr("未使用")
                            }
                            Item {
                                width: parent.width
                                height: 30
                                Row {
                                    anchors.right: parent.right
                                    spacing: 8
                                    AppButton {
                                        style: "subtle"
                                        text: qsTr("👁 预览")
                                        implicitHeight: 26; fontSize: 12
                                        onClicked: {
                                            root.previewAsset =
                                                assetManager.assetInfo(
                                                    card.modelData.id)
                                            previewDialog.open()
                                        }
                                    }
                                    AppButton {
                                        style: "danger"
                                        text: qsTr("🗑")
                                        implicitHeight: 26; fontSize: 12
                                        onClicked: {
                                            if (card.modelData.inUse) {
                                                blockDialog.message = qsTr(
                                                    "该图片正在使用中（%1），请先解除引用后再删除。").arg(
                                                    card.modelData.refsLabel === "logo"
                                                    ? qsTr("面板 Logo")
                                                    : card.modelData.refsLabel)
                                                blockDialog.open()
                                            } else {
                                                root.pendingDelete = card.modelData
                                                deleteDialog.open()
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // dashed upload tile
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    height: 252
                    radius: Theme.radiusLg
                    color: "transparent"
                    border.width: 1
                    border.color: Theme.borderColor
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: uploadDialog.open()
                    }
                    Column {
                        anchors.centerIn: parent
                        spacing: 6
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "＋"
                            color: _faintColor
                            font.pixelSize: 26
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: qsTr("点击上传图片")
                            color: _faintColor
                            font.pixelSize: 12
                        }
                    }
                }
            }
        }
    }

    // ── Preview dialog ────────────────────────────────────────────────
    AppDialog {
        id: previewDialog
        showNegative: false
        positiveText: qsTr("关闭")
        title: root.previewAsset.name ?? ""
        message: root.previewAsset.sha256 !== undefined
            ? qsTr("%1×%2 · %3 · 上传于 %4\nSHA-256：%5\n引用：%6").arg(
                root.previewAsset.width).arg(root.previewAsset.height).arg(
                root.fmtSize(root.previewAsset.sizeBytes)).arg(
                root.previewAsset.uploadedAt).arg(
                root.previewAsset.sha256).arg(
                root.previewAsset.inUse ? root.previewAsset.refs.join(", ")
                                        : qsTr("无"))
            : ""
    }

    // ── Delete confirm ────────────────────────────────────────────────
    AppDialog {
        id: deleteDialog
        title: qsTr("删除图片")
        message: qsTr("删除「%1」？文件与记录将一并移除，此操作不可撤销。").arg(
            root.pendingDelete.name ?? "")
        positiveText: qsTr("删除")
        onPositiveClicked: assetManager.deleteAsset(root.pendingDelete.id)
    }

    // ── In-use block message ──────────────────────────────────────────
    AppDialog {
        id: blockDialog
        showNegative: false
        positiveText: qsTr("知道了")
        title: qsTr("无法删除")
    }

    // ── Toast ─────────────────────────────────────────────────────────
    Rectangle {
        id: toast
        property string text: ""
        visible: false
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 28
        width: toastText.implicitWidth + 32
        height: 34
        radius: Theme.radiusMd
        color: Theme.dark ? "#2b2b2b" : "#ffffff"
        border.width: 1
        border.color: Theme.borderColor
        Text {
            id: toastText
            anchors.centerIn: parent
            text: toast.text
            color: Theme.textColor
            font.pixelSize: 12
        }
        Timer {
            id: toastTimer
            interval: 2200
            onTriggered: toast.visible = false
        }
    }
}
