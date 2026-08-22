import QtQuick
import DesktopPet

// Sparkline — design-mock .spark: polyline over the last N samples.
// Canvas redraw on series change; empty series renders the faint tint only.
Canvas {
    id: root

    property var series: []
    property color lineColor: Theme.accentColor
    property real lineWidth: 2

    height: 44

    onSeriesChanged: requestPaint()
    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        ctx.clearRect(0, 0, width, height)
        const n = series.length
        if (n < 2) return
        let max = -Infinity
        for (let i = 0; i < n; ++i)
            if (series[i] > max) max = series[i]
        if (max <= 0) max = 1
        ctx.strokeStyle = lineColor
        ctx.lineWidth = lineWidth
        ctx.beginPath()
        for (let i = 0; i < n; ++i) {
            const x = (i / (n - 1)) * width
            const y = height - 3 - (series[i] / max) * (height - 6)
            if (i === 0) ctx.moveTo(x, y)
            else ctx.lineTo(x, y)
        }
        ctx.stroke()
    }
}
