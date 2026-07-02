import QtQuick
import QtQuick.Layouts
import QtGraphs
import QGroundControl

/// PID rate controller chart: Desired vs Actual overlay per axis.
/// Includes axis selector (Roll/Pitch/Yaw) and MAE display.
Rectangle {
    id: root
    color: qgcPal.window
    radius: Math.round(_sp * 0.4)

    property var timeSeries: []
    property var flightModes: []
    property var qgcPal: QGroundControl.globalPalette
    readonly property real _sp: _m.fontPixelSize
    Text { id: _m; text: "X"; font.pointSize: 10; readonly property real fontPixelSize: Math.max(Math.round(contentHeight), 8) }

    property string _axis: "roll"  // "roll", "pitch", "yaw"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Math.round(_sp * 0.5)
        spacing: Math.round(_sp * 0.3)

        RowLayout {
            Layout.fillWidth: true
            spacing: Math.round(_sp * 0.4)

            Text {
                text: "PID 角速度响应"
                color: qgcPal.buttonHighlightText
                font.pixelSize: Math.round(_sp * 1.05)
                font.bold: true
            }

            Item { Layout.fillWidth: true }

            // Axis selector
            RowLayout {
                spacing: Math.round(_sp * 0.25)
                Repeater {
                    model: [
                        {label: "Roll",  key: "roll"},
                        {label: "Pitch", key: "pitch"},
                        {label: "Yaw",   key: "yaw"}
                    ]
                    delegate: Rectangle {
                        width: Math.round(_sp * 3.5)
                        height: Math.round(_sp * 1.6)
                        radius: Math.round(_sp * 0.2)
                        color: _axis === modelData.key
                            ? Qt.rgba(qgcPal.primaryButton.r, qgcPal.primaryButton.g, qgcPal.primaryButton.b, 0.12)
                            : "transparent"
                        border.color: _axis === modelData.key ? qgcPal.primaryButton : qgcPal.groupBorder
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: modelData.label
                            color: _axis === modelData.key ? qgcPal.primaryButton : Qt.rgba(qgcPal.buttonHighlightText.r, qgcPal.buttonHighlightText.g, qgcPal.buttonHighlightText.b, 0.5)
                            font.pixelSize: Math.round(_sp * 0.75)
                            font.bold: _axis === modelData.key
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                _axis = modelData.key
                                root.populate(root.timeSeries, root.flightModes)
                            }
                        }
                    }
                }
            }
        }

        // MAE metric
        RowLayout {
            Layout.fillWidth: true
            Text {
                id: maeText
                text: ""
                color: qgcPal.buttonHighlightText
                font.pixelSize: Math.round(_sp * 0.85)
            }
            Item { Layout.fillWidth: true }
            // Legend
            Rectangle { width: Math.round(_sp * 0.8); height: Math.round(_sp * 0.3); color: qgcPal.colorRed;  radius: 2 }
            Text { text: "Desired"; color: qgcPal.buttonHighlightText; font.pixelSize: Math.round(_sp * 0.7) }
            Rectangle { width: Math.round(_sp * 0.8); height: Math.round(_sp * 0.3); color: qgcPal.colorBlue; radius: 2 }
            Text { text: "Actual";  color: qgcPal.buttonHighlightText; font.pixelSize: Math.round(_sp * 0.7) }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            GraphsView {
                id: pidChart
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
                    min: -1; max: 1
                    titleText: "deg/s"
                    tickInterval: 20
                }

                LineSeries { id: desSeries; color: qgcPal.colorRed;  width: 1; name: "Desired" }
                LineSeries { id: actSeries; color: qgcPal.colorBlue; width: 1; name: "Actual"  }
            }

            FlightModeOverlay {
                anchors.fill: pidChart
                flightModes: root.flightModes
                timeMin: xAxis.min
                timeMax: xAxis.max
                chartWidth: pidChart.width
                chartHeight: pidChart.height
            }
        }
    }

    function populate(data, modes) {
        desSeries.clear(); actSeries.clear()
        maeText.text = ""
        if (!data || data.length === 0) return

        var tMin = data[0].t
        var tMax = data[data.length - 1].t
        xAxis.min = tMin; xAxis.max = tMax

        var desKey = _axis + "Des"
        var actKey = _axis + "Act"
        var yMin = 0, yMax = 0
        var errSum = 0

        for (var i = 0; i < data.length; i++) {
            var d = data[i]
            var des = d[desKey] !== undefined ? d[desKey] : 0
            var act = d[actKey] !== undefined ? d[actKey] : 0
            desSeries.append(d.t, des)
            actSeries.append(d.t, act)
            if (des < yMin) yMin = des
            if (act < yMin) yMin = act
            if (des > yMax) yMax = des
            if (act > yMax) yMax = act
            errSum += Math.abs(des - act)
        }

        var mae = data.length > 0 ? (errSum / data.length) : 0
        var axisLabel = _axis.charAt(0).toUpperCase() + _axis.slice(1)
        maeText.text = axisLabel + " MAE: " + mae.toFixed(2) + " deg/s"

        var yAbs = Math.max(Math.abs(yMin), Math.abs(yMax))
        yAxis.min = -Math.ceil(yAbs / 20) * 20 - 10
        yAxis.max = Math.ceil(yAbs / 20) * 20 + 10
        yAxis.tickInterval = Math.max(5, Math.round(yAbs / 5))

        flightModes = modes
    }
}
