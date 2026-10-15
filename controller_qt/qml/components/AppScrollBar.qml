import QtQuick
import QtQuick.Controls
import DesktopPet

// AppScrollBar — minimal Fluent-style OVERLAY scrollbar for Flickable/ListView.
// Follows the app Quick Controls style (was pinned to Basic); the contentItem
// + background overrides below keep the visual identical under FluentWinUI3.
//
// Overlay behavior (Fluent spec, replaces the old always-on thumb): with
// AsNeeded policy a bar still becomes `visible` whenever the content
// overflows — which users read as "the nav always has a scrollbar". The
// bar therefore sits at opacity 0 at rest and only fades IN (200ms) while
//   - the view is being scrolled (ScrollBar.active: drag/flick/wheel), or
//   - the cursor is over the bar itself, or
//   - the cursor is anywhere over the parent view (the Flickable the bar
//     is declaratively attached to is its `parent`), the Win11-lists
//     edge-approach reveal. An invisible bar still receives hover at the
//     pane edge, which is what makes that reveal work.
// and fades back OUT once the interaction ends. `policy` stays AsNeeded, so
// a non-overflowing view keeps the bar `visible == false` entirely — the
// hover reveal can never summon a dead bar on a short list.
ScrollBar {
    id: bar

    implicitWidth: 8

    HoverHandler { id: barHover }

    // Hover anywhere over the attached view reveals the bar (Win11 list
    // behavior). The handler is REPARENTED onto the view (`bar.parent` is
    // the Flickable/ListView the bar is declaratively attached to): a
    // reparented HoverHandler rides the view's whole subtree delivery
    // chain — hover over the content or over empty canvas both count —
    // without shadowing anything (a sibling hover area would block the
    // hover washes of the rows underneath). Note `target:` alone does NOT
    // work here: it only retargets the bounds test, not event delivery.
    HoverHandler {
        id: viewHover
        parent: bar.parent
    }

    // Gate the parent-view reveal on actual scrollability (defensive: the
    // AsNeeded `visible` already hides the bar on short content; non-
    // Flickable parents report undefined contentHeight and fall through).
    readonly property bool _viewScrollable: {
        const p = bar.parent
        if (!p || p.contentHeight === undefined)
            return true
        return p.contentHeight > p.height
    }

    // The scrollability gate wraps EVERY reveal path: `active`/`pressed`
    // cannot engage without overflow anyway, but hovering the (invisible)
    // bar strip on a short list would otherwise summon a dead full-height
    // thumb under styles that keep `visible` true regardless (Basic).
    //
    // QA seam (same spirit as PET_COLORSCHEME in main.cpp): offscreen
    // screenshot QA can neither hover nor scroll, so
    // `--qa-scrollbar-reveal` forces the revealed state — the exact paint
    // path the active/hover fade-in drives, minus the input. QA-only.
    readonly property bool _qaForceReveal:
        Qt.application.arguments.indexOf("--qa-scrollbar-reveal") >= 0

    opacity: (_viewScrollable
              && (_qaForceReveal || active || pressed || barHover.hovered
                  || viewHover.hovered)) ? 1 : 0
    Behavior on opacity {
        NumberAnimation { duration: 200; easing.type: Easing.OutCubic }
    }

    contentItem: Rectangle {
        radius: 4
        color: Qt.alpha(Theme.textColor, parent.pressed ? 0.45 : 0.25)
    }
    background: null
}
