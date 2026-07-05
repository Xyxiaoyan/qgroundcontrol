import QtQuick
import QtQuick.Layouts
import QtGraphs
import QGroundControl

/// Vibration diagnostics chart: accelerometer (m/s², left Y-axis) +
/// gyroscope (rad/s, right Y-axis) with threshold lines.
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
                text: "震动诊断 (加速度计 + 陀螺仪)"
                color: qgcPal.buttonHighlightText
                font.pixelSize: Math.round(_sp * 1.05)
                font.bold: true
            }
            Item { Layout.fillWidth: true }
            Text {
                text: "⚠ 15 m/s²   ❌ 30 m/s²"
                color: Qt.rgba(qgcPal.buttonHighlightText.r, qgcPal.buttonHighlightText.g, qgcPal.buttonHighlightText.b, 0.45)
                font.pixelSize: Math.round(_sp * 0.75)
            }
        }

        // Legend
        RowLayout {
            Layout.fillWidth: true
            spacing: Math.round(_sp * 0.4)
            // Accel legend
            Text {
                text: "加速度计 (m/s²)"
                color: Qt.rgba(qgcPal.buttonHighlightText.r, qgcPal.buttonHighlightText.g, qgcPal.buttonHighlightText.b, 0.6)
                font.pixelSize: Math.round(_sp * 0.7)
            }
            Rectangle { width: Math.round(_sp * 0.8); height: Math.round(_sp * 0.3); color: qgcPal.colorRed;   radius: 2 }
            Text { text: "X"; color: qgcPal.buttonHighlightText; font.pixelSize: Math.round(_sp * 0.7) }
            Rectangle { width: Math.round(_sp * 0.8); height: Math.round(_sp * 0.3); color: qgcPal.colorGreen; radius: 2 }
            Text { text: "Y"; color: qgcPal.buttonHighlightText; font.pixelSize: Math.round(_sp * 0.7) }
            Rectangle { width: Math.round(_sp * 0.8); height: Math.round(_sp * 0.3); color: qgcPal.colorBlue;  radius: 2 }
            Text { text: "Z"; color: qgcPal.buttonHighlightText; font.pixelSize: Math.round(_sp * 0.7) }
            Item { Layout.fillWidth: true }
            // Gyro legend
            Text {
                text: "陀螺仪 (rad/s)"
                color: Qt.rgba(qgcPal.buttonHighlightText.r, qgcPal.buttonHighlightText.g, qgcPal.buttonHighlightText.b, 0.6)
                font.pixelSize: Math.round(_sp * 0.7)
            }
            Rectangle { width: Math.round(_sp * 0.8); height: Math.round(_sp * 0.3); color: qgcPal.brandingPurple;  radius: 2 }
            Text { text: "X"; color: qgcPal.buttonHighlightText; font.pixelSize: Math.round(_sp * 0.7) }
            Rectangle { width: Math.round(_sp * 0.8); height: Math.round(_sp * 0.3); color: qgcPal.colorYellow;     radius: 2 }
            Text { text: "Y"; color: qgcPal.buttonHighlightText; font.pixelSize: Math.round(_sp * 0.7) }
            Rectangle { width: Math.round(_sp * 0.8); height: Math.round(_sp * 0.3); color: qgcPal.colorYellowGreen; radius: 2 }
            Text { text: "Z"; color: qgcPal.buttonHighlightText; font.pixelSize: Math.round(_sp * 0.7) }
        }

        // Chart
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            GraphsView {
                id: vibChart
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

                // Left Y-axis: accelerometer (m/s²)
                axisY: ValueAxis {
                    id: accelYAxis
                    min: -5; max: 35
                    titleText: "m/s²"
                    tickInterval: 5
                }

                // Accel series (use primary axisY)
                LineSeries { id: seriesX; color: qgcPal.colorRed;    width: 1; name: "Accel X" }
                LineSeries { id: seriesY; color: qgcPal.colorGreen;  width: 1; name: "Accel Y" }
                LineSeries { id: seriesZ; color: qgcPal.colorBlue;   width: 1; name: "Accel Z" }
                LineSeries { id: warnLine;   color: qgcPal.colorOrange; width: 1; name: "警告" }
                LineSeries { id: dangerLine; color: qgcPal.colorRed;    width: 1; name: "危险" }

                // Gyro series — each with its own right-aligned Y-axis
                // Note: Qt Graphs does NOT allow sharing an axis across multiple
                // series — doing so triggers "axis already associated" + segfault.
                // Each series gets its own axis; extras are hidden to avoid clutter.
                LineSeries {
                    id: gyroSeriesX
                    color: qgcPal.brandingPurple
                    width: 1
                    name: "Gyro X"
                    axisY: ValueAxis {
                        id: gyroYAxis
                        min: -5; max: 5
                        titleText: "rad/s"
                        tickInterval: 1
                        alignment: Qt.AlignRight
                    }
                }
                LineSeries {
                    id: gyroSeriesY
                    color: qgcPal.colorYellow
                    width: 1
                    name: "Gyro Y"
                    axisY: ValueAxis {
                        min: -5; max: 5
                        tickInterval: 1
                        alignment: Qt.AlignRight
                        visible: false
                    }
                }
                LineSeries {
                    id: gyroSeriesZ
                    color: qgcPal.colorYellowGreen
                    width: 1
                    name: "Gyro Z"
                    axisY: ValueAxis {
                        min: -5; max: 5
                        tickInterval: 1
                        alignment: Qt.AlignRight
                        visible: false
                    }
                }
            }

            FlightModeOverlay {
                anchors.fill: vibChart
                flightModes: root.flightModes
                timeMin: xAxis.min
                timeMax: xAxis.max
                chartWidth: vibChart.width
                chartHeight: vibChart.height
            }
        }
    }

    function populate(accelData, gyroData, modes) {
        seriesX.clear(); seriesY.clear(); seriesZ.clear()
        warnLine.clear(); dangerLine.clear()
        gyroSeriesX.clear(); gyroSeriesY.clear(); gyroSeriesZ.clear()
        if (!accelData || accelData.length === 0) return

        var tMin = accelData[0].t
        var tMax = accelData[accelData.length - 1].t
        xAxis.min = tMin; xAxis.max = tMax

        // --- Accel ---
        var aMin = 0, aMax = 0
        for (var i = 0; i < accelData.length; i++) {
            var d = accelData[i]
            seriesX.append(d.t, d.x)
            seriesY.append(d.t, d.y)
            seriesZ.append(d.t, d.z)
            if (d.x < aMin) aMin = d.x
            if (d.y < aMin) aMin = d.y
            if (d.z < aMin) aMin = d.z
            if (d.x > aMax) aMax = d.x
            if (d.y > aMax) aMax = d.y
            if (d.z > aMax) aMax = d.z
        }

        // Threshold lines
        warnLine.append(tMin, 15);   warnLine.append(tMax, 15)
        dangerLine.append(tMin, 30); dangerLine.append(tMax, 30)

        // Accel Y-axis: auto-scale to data, but always include the danger threshold (30)
        if (aMax < 30) aMax = 30
        if (aMin > 0) aMin = 0
        var aPad = Math.max(1, (aMax - aMin) * 0.1)
        accelYAxis.min = Math.floor(aMin) - aPad
        accelYAxis.max = Math.ceil(aMax) + aPad
        accelYAxis.tickInterval = Math.max(1, Math.round((accelYAxis.max - accelYAxis.min) / 6))

        // --- Gyro ---
        var gMin = 0, gMax = 0
        if (gyroData && gyroData.length > 0) {
            for (var j = 0; j < gyroData.length; j++) {
                var g = gyroData[j]
                gyroSeriesX.append(g.t, g.x)
                gyroSeriesY.append(g.t, g.y)
                gyroSeriesZ.append(g.t, g.z)
                if (g.x < gMin) gMin = g.x
                if (g.y < gMin) gMin = g.y
                if (g.z < gMin) gMin = g.z
                if (g.x > gMax) gMax = g.x
                if (g.y > gMax) gMax = g.y
                if (g.z > gMax) gMax = g.z
            }

            // Gyro Y-axis: auto-scale
            var gAbs = Math.max(Math.abs(gMin), Math.abs(gMax))
            if (gAbs < 0.1) gAbs = 0.1
            var gPad = gAbs * 0.1
            gyroYAxis.min = -(gAbs + gPad)
            gyroYAxis.max = gAbs + gPad
            gyroYAxis.tickInterval = Math.max(0.1, Math.round((gyroYAxis.max - gyroYAxis.min) / 6 * 10) / 10)
        }

        flightModes = modes
    }
}
