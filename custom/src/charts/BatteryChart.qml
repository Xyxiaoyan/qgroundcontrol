import QtQuick
import QtQuick.Layouts
import QtGraphs
import QGroundControl

/// Battery voltage / current / remaining % time-series chart.
/// Three independent sub-charts stacked vertically — each with its own Y axis.
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

        // Header
        RowLayout {
            Layout.fillWidth: true
            spacing: Math.round(_sp * 0.4)
            Text {
                text: "电池性能"
                color: qgcPal.buttonHighlightText
                font.pixelSize: Math.round(_sp * 1.05)
                font.bold: true
            }
            Item { Layout.fillWidth: true }
            // Legend
            Rectangle { width: Math.round(_sp * 0.8); height: Math.round(_sp * 0.3); color: qgcPal.colorGreen; radius: 2 }
            Text { text: "电压"; color: qgcPal.buttonHighlightText; font.pixelSize: Math.round(_sp * 0.7) }
            Rectangle { width: Math.round(_sp * 0.8); height: Math.round(_sp * 0.3); color: qgcPal.colorOrange; radius: 2 }
            Text { text: "电流"; color: qgcPal.buttonHighlightText; font.pixelSize: Math.round(_sp * 0.7) }
            Rectangle { width: Math.round(_sp * 0.8); height: Math.round(_sp * 0.3); color: qgcPal.colorBlue; radius: 2 }
            Text { text: "电量%"; color: qgcPal.buttonHighlightText; font.pixelSize: Math.round(_sp * 0.7) }
        }

        // ── Voltage ──
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            GraphsView {
                id: voltChart
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
                    id: vXAxis
                    min: 0; max: 1
                    labelFormat: "%.1f"
                    titleText: "秒"
                }

                axisY: ValueAxis {
                    id: vYAxis
                    min: 0; max: 25
                    titleText: "电压 (V)"
                    tickInterval: 2
                }

                LineSeries { id: voltSeries; color: qgcPal.colorGreen; width: 1; name: "电压" }
            }

            FlightModeOverlay {
                anchors.fill: voltChart
                flightModes: root.flightModes
                timeMin: vXAxis.min
                timeMax: vXAxis.max
                chartWidth: voltChart.width
                chartHeight: voltChart.height
            }
        }

        // ── Current ──
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            GraphsView {
                id: currChart
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
                    id: cXAxis
                    min: 0; max: 1
                    labelFormat: "%.1f"
                    titleText: "秒"
                }

                axisY: ValueAxis {
                    id: cYAxis
                    min: 0; max: 30
                    titleText: "电流 (A)"
                    tickInterval: 5
                }

                LineSeries { id: currSeries; color: qgcPal.colorOrange; width: 1; name: "电流" }
            }

            FlightModeOverlay {
                anchors.fill: currChart
                flightModes: root.flightModes
                timeMin: cXAxis.min
                timeMax: cXAxis.max
                chartWidth: currChart.width
                chartHeight: currChart.height
            }
        }

        // ── Remaining % ──
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            GraphsView {
                id: pctChart
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
                    id: pXAxis
                    min: 0; max: 1
                    labelFormat: "%.1f"
                    titleText: "秒"
                }

                axisY: ValueAxis {
                    id: pYAxis
                    min: 0; max: 100
                    titleText: "电量 (%)"
                    tickInterval: 20
                }

                LineSeries { id: pctSeries; color: qgcPal.colorBlue; width: 1; name: "电量" }
            }

            FlightModeOverlay {
                anchors.fill: pctChart
                flightModes: root.flightModes
                timeMin: pXAxis.min
                timeMax: pXAxis.max
                chartWidth: pctChart.width
                chartHeight: pctChart.height
            }
        }
    }

    function populate(data, modes) {
        voltSeries.clear(); currSeries.clear(); pctSeries.clear()
        if (!data || data.length === 0) return

        var tMin = data[0].t
        var tMax = data[data.length - 1].t
        vXAxis.min = tMin; vXAxis.max = tMax
        cXAxis.min = tMin; cXAxis.max = tMax
        pXAxis.min = tMin; pXAxis.max = tMax

        var vMin = 99, vMax = 0, aMax = 0
        for (var i = 0; i < data.length; i++) {
            var d = data[i]
            voltSeries.append(d.t, d.v)
            currSeries.append(d.t, d.a)
            pctSeries.append(d.t, d.pct * 100)
            if (d.v < vMin) vMin = d.v
            if (d.v > vMax) vMax = d.v
            if (d.a > aMax) aMax = d.a
        }

        // Voltage axis
        vYAxis.min = Math.floor(vMin) - 1
        if (vYAxis.min < 0) vYAxis.min = 0
        vYAxis.max = Math.ceil(vMax) + 1
        vYAxis.tickInterval = Math.max(1, Math.round((vYAxis.max - vYAxis.min) / 5))

        // Current axis
        cYAxis.min = 0
        cYAxis.max = Math.ceil(aMax / 5) * 5 + 5

        flightModes = modes
    }
}
