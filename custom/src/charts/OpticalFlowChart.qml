import QtQuick
import QtQuick.Layouts
import QtGraphs
import QGroundControl

/// Optical flow quality + flow integral X/Y chart.
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
                text: "光流 / 室内定位"
                color: qgcPal.buttonHighlightText
                font.pixelSize: Math.round(_sp * 1.05)
                font.bold: true
            }
            Item { Layout.fillWidth: true }
            Text {
                text: "质量阈值: 100"
                color: Qt.rgba(qgcPal.buttonHighlightText.r, qgcPal.buttonHighlightText.g, qgcPal.buttonHighlightText.b, 0.45)
                font.pixelSize: Math.round(_sp * 0.75)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Math.round(_sp * 0.3)

            // Left: quality
            Item {
                Layout.fillHeight: true
                Layout.fillWidth: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: Math.round(_sp * 0.2)

                    Text {
                        text: "信号质量 (0-255)"
                        color: Qt.rgba(qgcPal.buttonHighlightText.r, qgcPal.buttonHighlightText.g, qgcPal.buttonHighlightText.b, 0.6)
                        font.pixelSize: Math.round(_sp * 0.75)
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        GraphsView {
                            id: qualChart
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
                                id: qXAxis
                                min: 0; max: 1
                                labelFormat: "%.1f"
                                visible: false
                            }

                            axisY: ValueAxis {
                                id: qYAxis
                                min: 0; max: 255
                                titleText: "质量"
                                tickInterval: 50
                            }

                            LineSeries { id: qualSeries; color: qgcPal.colorGreen; width: 1; name: "Quality" }
                            LineSeries { id: threshLine; color: qgcPal.colorOrange; width: 1; name: "阈值" }
                        }
                    }
                }
            }

            // Right: flow X/Y
            Item {
                Layout.fillHeight: true
                Layout.fillWidth: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: Math.round(_sp * 0.2)

                    Text {
                        text: "累计流量 (rad)"
                        color: Qt.rgba(qgcPal.buttonHighlightText.r, qgcPal.buttonHighlightText.g, qgcPal.buttonHighlightText.b, 0.6)
                        font.pixelSize: Math.round(_sp * 0.75)
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        GraphsView {
                            id: flowChart
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
                                id: fXAxis
                                min: 0; max: 1
                                labelFormat: "%.1f"
                            }

                            axisY: ValueAxis {
                                id: fYAxis
                                min: -1; max: 1
                                titleText: "rad"
                            }

                            LineSeries { id: flowXSeries; color: qgcPal.colorRed;   width: 1; name: "Flow X" }
                            LineSeries { id: flowYSeries; color: qgcPal.colorBlue;  width: 1; name: "Flow Y" }
                        }
                    }
                }
            }
        }
    }

    function populate(data, modes) {
        qualSeries.clear(); threshLine.clear()
        flowXSeries.clear(); flowYSeries.clear()
        if (!data || data.length === 0) return

        var tMin = data[0].t
        var tMax = data[data.length - 1].t
        qXAxis.min = tMin; qXAxis.max = tMax
        fXAxis.min = tMin; fXAxis.max = tMax

        var flowMin = 0, flowMax = 0
        for (var i = 0; i < data.length; i++) {
            var d = data[i]
            qualSeries.append(d.t, d.q)
            flowXSeries.append(d.t, d.fx)
            flowYSeries.append(d.t, d.fy)
            if (d.fx < flowMin) flowMin = d.fx
            if (d.fy < flowMin) flowMin = d.fy
            if (d.fx > flowMax) flowMax = d.fx
            if (d.fy > flowMax) flowMax = d.fy
        }

        // Quality threshold
        threshLine.append(tMin, 100); threshLine.append(tMax, 100)

        // Flow Y-axis
        var fAbs = Math.max(Math.abs(flowMin), Math.abs(flowMax))
        fYAxis.min = -Math.ceil(fAbs) - 1
        fYAxis.max = Math.ceil(fAbs) + 1

        flightModes = modes
    }
}
