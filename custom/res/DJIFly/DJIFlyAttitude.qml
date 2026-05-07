import QtQuick
import QtQuick.Controls
import QGroundControl
import QGroundControl.Controls
import QGroundControl.ScreenTools

// DJI Fly Style Attitude Indicator
Item {
    id: root
    width: 180; height: 180
    property var vehicle
    property real _roll:  vehicle ? (isNaN(vehicle.roll.rawValue)  ? 0 : vehicle.roll.rawValue)  : 0
    property real _pitch: vehicle ? (isNaN(vehicle.pitch.rawValue) ? 0 : vehicle.pitch.rawValue) : 0

    Canvas {
        id: horizonCanvas
        anchors.fill: parent
        property real rollValue: root._roll
        property real pitchValue: root._pitch

        onRollValueChanged: requestPaint()
        onPitchValueChanged: requestPaint()

        onPaint: {
            var ctx = getContext("2d")
            var cx = width / 2; var cy = height / 2
            ctx.reset()

            // Clip circle
            ctx.save()
            ctx.beginPath(); ctx.arc(cx, cy, 80, 0, Math.PI * 2); ctx.clip()

            // Sky/ground
            var grad = ctx.createLinearGradient(0, 0, 0, height)
            grad.addColorStop(0, "#2244aa"); grad.addColorStop(0.45, "#3355cc")
            grad.addColorStop(0.5, "#cc8833"); grad.addColorStop(0.55, "#996622")
            grad.addColorStop(1, "#553311")
            ctx.fillStyle = grad; ctx.fillRect(0, 0, width, height)

            // Pitch ladder
            ctx.save(); ctx.translate(cx, cy); ctx.rotate(root._roll * Math.PI / 180)
            var offset = root._pitch * 4
            ctx.strokeStyle = "#ffffff"; ctx.lineWidth = 1.5
            for (var i = -60; i <= 60; i += 5) {
                var y = i * 4 - offset
                if (y > -75 && y < 75) {
                    var w = i % 10 === 0 ? 28 : 15
                    ctx.beginPath(); ctx.moveTo(-w, y); ctx.lineTo(w, y); ctx.stroke()
                    if (i % 10 === 0 && i !== 0) {
                        ctx.font = "8px sans-serif"; ctx.fillStyle = "#ffffff"
                        ctx.fillText(Math.abs(i) + "°", w + 3, y + 3)
                    }
                }
            }

            // Aircraft symbol
            ctx.fillStyle = "#ffcc00"; ctx.strokeStyle = "#ffcc00"; ctx.lineWidth = 2
            ctx.beginPath(); ctx.moveTo(-35, 0); ctx.lineTo(-10, 0); ctx.stroke()
            ctx.beginPath(); ctx.moveTo(10, 0); ctx.lineTo(35, 0); ctx.stroke()
            ctx.beginPath(); ctx.arc(0, 0, 4, 0, Math.PI * 2); ctx.fill()
            ctx.restore()

            // Outer ring
            ctx.strokeStyle = "#55ffffff"; ctx.lineWidth = 2
            ctx.beginPath(); ctx.arc(cx, cy, 80, 0, Math.PI * 2); ctx.stroke()

            // Roll scale
            ctx.save(); ctx.translate(cx, cy)
            ctx.strokeStyle = "#ffffff"; ctx.lineWidth = 1
            for (var j = -50; j <= 50; j += 10) {
                var a = (j - root._roll) * Math.PI / 180 - Math.PI / 2
                var inner = j % 30 === 0 ? 65 : 70
                ctx.beginPath()
                ctx.moveTo(80 * Math.cos(a), 80 * Math.sin(a))
                ctx.lineTo(inner * Math.cos(a), inner * Math.sin(a))
                ctx.stroke()
            }
            // Roll pointer
            ctx.strokeStyle = "#ffcc00"; ctx.lineWidth = 2
            ctx.beginPath(); ctx.moveTo(0, -80); ctx.lineTo(0, -72); ctx.stroke()
            ctx.restore()

            // Heading at bottom
            var hdg = vehicle ? (isNaN(vehicle.heading.rawValue) ? 0 : vehicle.heading.rawValue.toFixed(0)) : "---"
            ctx.font = "bold 11px sans-serif"; ctx.fillStyle = "#ffffff"; ctx.textAlign = "center"
            ctx.fillText(hdg + "°", cx, cy + 82)
        }
    }
}
