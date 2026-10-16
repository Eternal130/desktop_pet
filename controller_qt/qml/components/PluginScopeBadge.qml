import QtQuick
import QtQuick.Controls
import DesktopPet

// PluginScopeBadge — "打标记不隐藏" roster marker for plugin-summoned
// instances (v1.13 read-only `pluginScope` role on both roster models).
// A compact accent-tinted pill with the puzzle glyph — the same icon the
// 插件 nav items and 插件管理 badge use — so plugin-domain pets stay fully
// visible and functional while distinguishable from host-created pets at
// a glance.
//
// Renders NOTHING when pluginId is empty: host-created rows keep their
// exact current geometry (positioner children are skipped while
// invisible, and non-positioner use sites bind to visible anyway).
//
// Hover (350ms delay — the PluginManagerPage ToolTip convention) and the
// accessible name reveal the owning plugin id in plain wording:
// "插件实例：<pluginId>".
Rectangle {
    id: root

    // Plugin id that summoned the instance ("" = host-created → hidden)
    property string pluginId: ""
    // Show the "插件" caption next to the glyph. Off for dense surfaces
    // (collapsed-rail rows show no badge at all), on for roomier name
    // rows (welcome cards).
    property bool showText: false
    // Extra visibility gate for consumers whose CONTEXT hides the marker
    // (the sidebar's 52px icon rail) — kept separate so the internal
    // empty-scope binding below is never clobbered by a use site.
    property bool active: true

    visible: root.pluginId.length > 0 && root.active
    height: 16
    width: _contentRow.implicitWidth + 12
    radius: height / 2
    // Exact accent-tint recipe of the nav count badges (Main.qml NavItem):
    // plugin identity reads through the same "accent = plugin" association.
    color: Qt.alpha(Theme.accentColor, Theme.dark ? 0.30 : 0.16)

    // Matching foreground per scheme (same factors as the nav badge text)
    readonly property color _fg: Theme.dark
        ? Qt.lighter(Theme.accentColor, 1.35)
        : Qt.darker(Theme.accentColor, 1.2)

    Row {
        id: _contentRow
        anchors.centerIn: parent
        spacing: 4

        AppIcon {
            name: "puzzle"
            size: 10
            // 10px display of a 24-grid glyph: default 1.7 stroke scales
            // to ~0.7px (vanishes at 100% DPI) — same compensation the
            // window caption glyphs use.
            strokeWidth: 2.2
            color: root._fg
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            visible: root.showText
            text: qsTr("插件")
            color: root._fg
            font.pixelSize: 10
            font.weight: Font.DemiBold
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    // Hover tooltip revealing the owning plugin id. Pure hover monitoring:
    // acceptedButtons: NoButton keeps card taps / row clicks passing
    // through untouched.
    MouseArea {
        id: _hover
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        hoverEnabled: true
    }
    ToolTip.visible: _hover.containsMouse && root.visible
                     && root.pluginId.length > 0
    ToolTip.delay: 350
    ToolTip.text: qsTr("插件实例：") + root.pluginId

    // Screen readers get the same information the tooltip carries
    Accessible.role: Accessible.Graphic
    Accessible.name: root.pluginId.length > 0
                     ? qsTr("插件实例：") + root.pluginId : ""
}
