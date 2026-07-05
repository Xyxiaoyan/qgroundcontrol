import QtQuick
import QtQuick.Layouts
import QtGraphs
import QGroundControl

/// GPS health chart: HDOP + satellite count as separate sub-charts.
Rectangle {
    id: root
    color: qgcPal.window
    radius: Math.round(_sp * 0.4)

    property var timeSeries: []
    property var flightModes: []
    property var qgcPal: QGroundControl.globalPalette
    readonly property real _sp: _m.fontPixelSize
    Text { id: _m; text: "X"; font.pointSize: 10; readonly property real fontPixelSize: Math.max(Math.round(contentHeight), 8) }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Math.round(_sp * 0.5)
        spacing: Math.round(_sp * 0.3)

        RowLayout {
            Layout.fillWidth: true
            spacing: Math.round(_sp * 0.4)
            Text {
                text: "GPS 健康"
                color: qgcPal.buttonHighlightText
                font.pixelSize: Math.round(_sp * 1.05)
                font.bold: true
            }
            Item { Layout.fillWidth: true }
            Rectangle { width: Math.round(_sp * 0.8); height: Math.round(_sp * 0.3); color: qgcPal.colorRed; radius: 2 }
            Text { text: "HDOP"; color: qgcPal.buttonHighlightText; font.pixelSize: Math.round(_sp * 0.7) }
            Rectangle { width: Math.round(_sp * 0.8); height: Math.round(_sp * 0.3); color: qgcPal.colorGreen; radius: 2 }
            Text { text: "卫星数"; color: qgcPal.buttonHighlightText; font.pixelSize: Math.round(_sp * 0.7) }
        }

        // ── HDOP ──
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            GraphsView {
                id: hdopChart
                anchors.fill: parent

                theme: GraphsTheme {
                    colorScheme: qgcPal.globalTheme === 1
                        ? GraphsTheme.ColorScheme.Dark
                        : GraphsTheme.ColorScheme.Light
                    plotAreaBackgroundColor: qgcPal.window
                    grid.mainColor: Qt.rgba(qgcPal.buttonHighlightText.r, qgcPal.buttonHighlightText.g, qgcPal.buttonHighlightText.b, 0.15)
                    grid.subColor: Qt.rgba(qgcPal.buttonHighlightText.r, qgcPal.buttonHighlightText.g, qgcPal.buttonHighlightText.b, 0.08)
                    labelBackgroundVisible: false
                    labelTextColor: qgcPal.buttonHighlightText
                }

                axisX: ValueAxis {
                    id: hXAxis
                    min: 0; max: 1
                    labelFormat: "%.1f"
                    titleText: "秒"
                }

                axisY: ValueAxis {
                    id: hYAxis
                    min: 0; max: 5
                    titleText: "HDOP"
                    tickInterval: 1
                }

                LineSeries { id: hdopSeries; color: qgcPal.colorRed; width: 1; name: "HDOP" }
                LineSeries { id: goodHdopLine; color: qgcPal.colorGreen; width: 1; name: "HDOP=1.5" }
            }

            FlightModeOverlay {
                anchors.fill: hdopChart
                flightModes: root.flightModes
                timeMin: hXAxis.min
                timeMax: hXAxis.max
                chartWidth: hdopChart.width
                chartHeight: hdopChart.height
            }
        }

        // ── Satellite count ──
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            GraphsView {
                id: satChart
                anchors.fill: parent

                theme: GraphsTheme {
                    colorScheme: qgcPal.globalTheme === 1
                        ? GraphsTheme.ColorScheme.Dark
                        : GraphsTheme.ColorScheme.Light
                    plotAreaBackgroundColor: qgcPal.window
                    grid.mainColor: Qt.rgba(qgcPal.buttonHighlightText.r, qgcPal.buttonHighlightText.g, qgcPal.buttonHighlightText.b, 0.15)
                    grid.subColor: Qt.rgba(qgcPal.buttonHighlightText.r, qgcPal.buttonHighlightText.g, qgcPal.buttonHighlightText.b, 0.08)
                    labelBackgroundVisible: false
                    labelTextColor: qgcPal.buttonHighlightText
                }

                axisX: ValueAxis {
                    id: sXAxis
                    min: 0; max: 1
                    labelFormat: "%.1f"
                    titleText: "秒"
                }

                axisY: ValueAxis {
                    id: sYAxis
                    min: 0; max: 20
                    titleText: "卫星数"
                    tickInterval: 5
                }

                LineSeries { id: satSeries; color: qgcPal.colorGreen; width: 1; name: "卫星" }
            }

            FlightModeOverlay {
                anchors.fill: satChart
                flightModes: root.flightModes
                timeMin: sXAxis.min
                timeMax: sXAxis.max
                chartWidth: satChart.width
                chartHeight: satChart.height
            }
        }
    }

    function populate(data, modes) {
        hdopSeries.clear(); goodHdopLine.clear()
        satSeries.clear()
        if (!data || data.length === 0) return

        var tMin = data[0].t
        var tMax = data[data.length - 1].t
        hXAxis.min = tMin; hXAxis.max = tMax
        sXAxis.min = tMin; sXAxis.max = tMax

        var hdopMax = 0, satMax = 0
        for (var i = 0; i < data.length; i++) {
            var d = data[i]
            if (d.hdop > 0 && d.hdop < 99) {
                hdopSeries.append(d.t, d.hdop)
                if (d.hdop > hdopMax) hdopMax = d.hdop
            }
            satSeries.append(d.t, d.sat)
            if (d.sat > satMax) satMax = d.sat
        }

        // Good HDOP threshold
        goodHdopLine.append(tMin, 1.5); goodHdopLine.append(tMax, 1.5)

        hYAxis.min = 0
        hYAxis.max = Math.max(Math.ceil(hdopMax) + 1, 3)

        sYAxis.min = 0
        sYAxis.max = Math.ceil(satMax / 5) * 5 + 5

        flightModes = modes
    }
}
