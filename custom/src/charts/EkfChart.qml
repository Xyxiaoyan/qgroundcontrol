import QtQuick
import QtQuick.Layouts
import QtGraphs
import QGroundControl

/// EKF innovation test ratios over time with fault threshold.
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
                text: "EKF 滤波器健康"
                color: qgcPal.buttonHighlightText
                font.pixelSize: Math.round(_sp * 1.05)
                font.bold: true
            }
            Item { Layout.fillWidth: true }
            Text {
                text: "异常阈值: 0.5"
                color: Qt.rgba(qgcPal.buttonHighlightText.r, qgcPal.buttonHighlightText.g, qgcPal.buttonHighlightText.b, 0.45)
                font.pixelSize: Math.round(_sp * 0.75)
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            GraphsView {
                id: ekfChart
                anchors.fill: parent

                theme: GraphsTheme {
                    colorScheme: qgcPal.globalTheme === 1
                        ? GraphsTheme.ColorScheme.Dark
                        : GraphsTheme.ColorScheme.Light
                    plotAreaBackgroundColor: qgcPal.window
                    grid.mainColor: Qt.rgba(qgcPal.buttonHighlightText.r, qgcPal.buttonHighlightText.g, qgcPal.buttonHighlightText.b, 0.2)
                    grid.subColor: Qt.rgba(qgcPal.buttonHighlightText.r, qgcPal.buttonHighlightText.g, qgcPal.buttonHighlightText.b, 0.1)
                    labelBackgroundVisible: false
                    labelTextColor: qgcPal.buttonHighlightText
                }

                axisX: ValueAxis {
                    id: xAxis
                    min: 0; max: 1
                    labelFormat: "%.1f"
                    titleText: "秒"
                }

                axisY: ValueAxis {
                    id: yAxis
                    min: 0; max: 1
                    titleText: "Test Ratio"
                    tickInterval: 0.2
                }

                LineSeries { id: velSeries; color: qgcPal.colorRed;    width: 1; name: "速度" }
                LineSeries { id: posSeries; color: qgcPal.colorGreen;  width: 1; name: "位置" }
                LineSeries { id: hgtSeries; color: qgcPal.colorBlue;   width: 1; name: "高度" }
                LineSeries { id: magSeries; color: qgcPal.colorOrange; width: 1; name: "磁力" }
                LineSeries { id: faultLine; color: qgcPal.colorRed;    width: 1; name: "阈值" }
            }

            FlightModeOverlay {
                anchors.fill: ekfChart
                flightModes: root.flightModes
                timeMin: xAxis.min
                timeMax: xAxis.max
                chartWidth: ekfChart.width
                chartHeight: ekfChart.height
            }
        }
    }

    function populate(data, modes) {
        velSeries.clear(); posSeries.clear(); hgtSeries.clear(); magSeries.clear(); faultLine.clear()
        if (!data || data.length === 0) return

        var tMin = data[0].t
        var tMax = data[data.length - 1].t
        xAxis.min = tMin; xAxis.max = tMax

        var yMax = 0.5
        for (var i = 0; i < data.length; i++) {
            var d = data[i]
            velSeries.append(d.t, d.vel)
            posSeries.append(d.t, d.pos)
            hgtSeries.append(d.t, d.hgt)
            magSeries.append(d.t, d.mag)
            if (d.vel > yMax) yMax = d.vel
            if (d.pos > yMax) yMax = d.pos
            if (d.hgt > yMax) yMax = d.hgt
            if (d.mag > yMax) yMax = d.mag
        }

        // Fault threshold line
        faultLine.append(tMin, 0.5); faultLine.append(tMax, 0.5)

        yAxis.min = 0
        yAxis.max = Math.ceil(yMax * 5) / 5 + 0.2

        flightModes = modes
    }
}
