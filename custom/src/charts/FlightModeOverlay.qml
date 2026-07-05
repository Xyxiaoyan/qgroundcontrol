import QtQuick
import QGroundControl

/// Renders semi-transparent flight-mode colored rectangles on top of a chart area.
/// Position: place as a sibling of GraphsView inside the same container,
/// then bind timeMin/timeMax/chartWidth from the chart's axis.
Item {
    id: overlay

    property var flightModes: []       // QJsonArray of {t, name}
    property real timeMin: 0
    property real timeMax: 1
    property real chartWidth: parent ? parent.width : 100
    property real chartHeight: parent ? parent.height : 100
    property var qgcPal: QGroundControl.globalPalette

    readonly property real _sp: _m.fontPixelSize
    Text { id: _m; text: "X"; font.pointSize: 10; readonly property real fontPixelSize: Math.max(Math.round(contentHeight), 8) }

    // Color map matching UAV-Insight-Toolkit
    readonly property var _modeColors: ({
        "Stabilize":   Qt.rgba(1.0, 0.65, 0.0, 0.08),
        "Stabilized":  Qt.rgba(1.0, 0.65, 0.0, 0.08),
        "Loiter":      Qt.rgba(0.0, 0.0, 1.0, 0.08),
        "PosHold":     Qt.rgba(0.0, 0.0, 1.0, 0.08),
        "Position":    Qt.rgba(0.0, 0.0, 1.0, 0.08),
        "AltHold":     Qt.rgba(0.0, 1.0, 1.0, 0.08),
        "Auto":        Qt.rgba(0.0, 1.0, 0.0, 0.08),
        "Mission":     Qt.rgba(0.0, 1.0, 0.0, 0.08),
        "RTL":         Qt.rgba(1.0, 0.0, 0.0, 0.10),
        "Land":        Qt.rgba(1.0, 0.0, 0.0, 0.10),
        "Guided":      Qt.rgba(1.0, 0.0, 1.0, 0.08),
        "Offboard":    Qt.rgba(1.0, 0.0, 1.0, 0.08),
        "Manual":      Qt.rgba(0.5, 0.5, 0.5, 0.08),
        "Acro":        Qt.rgba(1.0, 1.0, 0.0, 0.08),
        "Rattitude":   Qt.rgba(1.0, 0.65, 0.0, 0.08),
        "Takeoff":     Qt.rgba(1.0, 0.65, 0.0, 0.08),
        "Descend":     Qt.rgba(1.0, 0.0, 0.0, 0.12),
        "Orbit":       Qt.rgba(1.0, 0.0, 1.0, 0.08),
        "Follow":      Qt.rgba(0.0, 1.0, 1.0, 0.08),
        "Precland":    Qt.rgba(1.0, 0.0, 0.0, 0.08),
        "Smart_RTL":   Qt.rgba(1.0, 0.0, 0.0, 0.10)
    })

    function _colorForMode(name) {
        return _modeColors[name] || Qt.rgba(0.8, 0.8, 0.8, 0.08)
    }

    // Build mode segments: [{start, end, name}, ...]
    function _buildSegments() {
        if (!flightModes || flightModes.length === 0) return []
        var segs = []
        var curName = ""
        var startT = 0
        for (var i = 0; i < flightModes.length; i++) {
            var m = flightModes[i]
            var t = m.t !== undefined ? m.t : 0
            var name = m.name || "Unknown"
            if (name !== curName) {
                if (curName !== "") {
                    segs.push({"start": startT, "end": t, "name": curName})
                }
                curName = name
                startT = t
            }
        }
        if (curName !== "") {
            segs.push({"start": startT, "end": timeMax, "name": curName})
        }
        return segs
    }

    // Map time value to pixel X
    function _timeToX(t) {
        if (timeMax <= timeMin) return 0
        return (t - timeMin) / (timeMax - timeMin) * chartWidth
    }

    Repeater {
        model: _buildSegments()

        Rectangle {
            required property var modelData
            x: Math.max(0, overlay._timeToX(modelData.start))
            y: 0
            width: Math.max(1, overlay._timeToX(modelData.end) - overlay._timeToX(modelData.start))
            height: chartHeight
            color: overlay._colorForMode(modelData.name)
            visible: x < chartWidth && (x + width) > 0

            Text {
                x: Math.round(overlay._sp * 0.2)
                y: Math.round(overlay._sp * 0.15)
                text: modelData.name
                color: Qt.rgba(qgcPal.buttonHighlightText.r, qgcPal.buttonHighlightText.g, qgcPal.buttonHighlightText.b, 0.55)
                font.pixelSize: Math.round(overlay._sp * 0.65)
                font.bold: true
            }
        }
    }
}
