import QtQuick
import QtQuick.Layouts
import QGroundControl

/// Top-level log analysis visualization dashboard.
/// Shows a tab bar for switching between chart types and renders the active chart.
Rectangle {
    id: root

    property var logModel: null  // LogAnalysisModel instance
    property var qgcPal: QGroundControl.globalPalette
    readonly property real _sp: _m.fontPixelSize
    Text { id: _m; text: "X"; font.pointSize: 10; readonly property real fontPixelSize: Math.max(Math.round(contentHeight), 8) }

    color: qgcPal.window

    readonly property var _chartTabs: [
        {label: "震动",   key: "vibration"},
        {label: "GPS",    key: "gps"},
        {label: "电池",   key: "battery"},
        {label: "EKF",    key: "ekf"},
        {label: "PID",    key: "pid"},
        {label: "光流",   key: "opticalflow"}
    ]

    property string currentChart: "vibration"

    ColumnLayout {
        anchors.fill: parent
        spacing: Math.round(_sp * 0.3)

        // ── Chart type selector ──
        RowLayout {
            Layout.fillWidth: true
            spacing: Math.round(_sp * 0.15)

            Repeater {
                model: _chartTabs

                Rectangle {
                    Layout.fillWidth: true
                    height: Math.round(_sp * 2.2)
                    radius: Math.round(_sp * 0.25)
                    color: currentChart === modelData.key
                        ? Qt.rgba(qgcPal.primaryButton.r, qgcPal.primaryButton.g, qgcPal.primaryButton.b, 0.12)
                        : "transparent"
                    border.color: currentChart === modelData.key ? qgcPal.primaryButton : qgcPal.groupBorder
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: modelData.label
                        color: currentChart === modelData.key ? qgcPal.primaryButton : Qt.rgba(qgcPal.buttonHighlightText.r, qgcPal.buttonHighlightText.g, qgcPal.buttonHighlightText.b, 0.5)
                        font.pixelSize: Math.round(_sp * 0.8)
                        font.bold: currentChart === modelData.key
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: currentChart = modelData.key
                    }
                }
            }
        }

        // ── Summary metrics bar ──
        Rectangle {
            Layout.fillWidth: true
            height: Math.round(_sp * 2.2)
            color: qgcPal.windowShade
            radius: Math.round(_sp * 0.25)

            RowLayout {
                anchors.fill: parent
                anchors.margins: Math.round(_sp * 0.3)
                spacing: Math.round(_sp * 0.8)

                Text {
                    text: logModel && logModel.hasData ? ("📁 " + _baseName(logModel.filePath)) : "无数据"
                    color: qgcPal.buttonHighlightText
                    font.pixelSize: Math.round(_sp * 0.8)
                    elide: Text.ElideMiddle
                    Layout.maximumWidth: parent.width * 0.4
                }

                Text {
                    visible: logModel && logModel.hasData && _vibRms
                    text: _vibRms ? ("📊 RMS: " + _vibRms) : ""
                    color: Qt.rgba(qgcPal.buttonHighlightText.r, qgcPal.buttonHighlightText.g, qgcPal.buttonHighlightText.b, 0.6)
                    font.pixelSize: Math.round(_sp * 0.75)
                }

                Text {
                    visible: logModel && logModel.hasData && _batSag
                    text: _batSag ? ("🔋 压降: " + _batSag + "V") : ""
                    color: Qt.rgba(qgcPal.buttonHighlightText.r, qgcPal.buttonHighlightText.g, qgcPal.buttonHighlightText.b, 0.6)
                    font.pixelSize: Math.round(_sp * 0.75)
                }

                Text {
                    visible: logModel && logModel.hasData && _ekfScore
                    text: _ekfScore ? ("🧠 EKF: " + _ekfScore + "%") : ""
                    color: Qt.rgba(qgcPal.buttonHighlightText.r, qgcPal.buttonHighlightText.g, qgcPal.buttonHighlightText.b, 0.6)
                    font.pixelSize: Math.round(_sp * 0.75)
                }

                Item { Layout.fillWidth: true }
            }
        }

        // ── Chart content area ──
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: {
                var keys = ["vibration", "gps", "battery", "ekf", "pid", "opticalflow"]
                return Math.max(0, keys.indexOf(currentChart))
            }

            VibrationChart {
                id: vibChart
                property bool _loaded: false
                onVisibleChanged: {
                    if (visible && logModel && logModel.hasData && !_loaded) {
                        populate(logModel.subsample(logModel.accelTimeSeries, 800),
                                 logModel.subsample(logModel.gyroTimeSeries, 800),
                                 logModel.flightModes)
                        _loaded = true
                    }
                }
            }

            GpsChart {
                id: gpsChart
                property bool _loaded: false
                onVisibleChanged: {
                    if (visible && logModel && logModel.hasData && !_loaded) {
                        populate(logModel.subsample(logModel.gpsTimeSeries, 800), logModel.flightModes)
                        _loaded = true
                    }
                }
            }

            BatteryChart {
                id: batChart
                property bool _loaded: false
                onVisibleChanged: {
                    if (visible && logModel && logModel.hasData && !_loaded) {
                        populate(logModel.subsample(logModel.batteryTimeSeries, 800), logModel.flightModes)
                        _loaded = true
                    }
                }
            }

            EkfChart {
                id: ekfChart
                property bool _loaded: false
                onVisibleChanged: {
                    if (visible && logModel && logModel.hasData && !_loaded) {
                        populate(logModel.subsample(logModel.ekfTimeSeries, 800), logModel.flightModes)
                        _loaded = true
                    }
                }
            }

            PidChart {
                id: pidChart
                property bool _loaded: false
                onVisibleChanged: {
                    if (visible && logModel && logModel.hasData && !_loaded) {
                        populate(logModel.subsample(logModel.pidTimeSeries, 800), logModel.flightModes)
                        _loaded = true
                    }
                }
            }

            OpticalFlowChart {
                id: flowChart
                property bool _loaded: false
                onVisibleChanged: {
                    if (visible && logModel && logModel.hasData && !_loaded) {
                        populate(logModel.subsample(logModel.opticalFlowTimeSeries, 800), logModel.flightModes)
                        _loaded = true
                    }
                }
            }
        }
    }

    // ── Empty state ──
    Column {
        anchors.centerIn: parent
        visible: !logModel || !logModel.hasData
        spacing: Math.round(_sp * 0.5)

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "📊"
            font.pixelSize: Math.round(_sp * 3)
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "拖拽日志文件到对话标签页进行分析"
            color: Qt.rgba(qgcPal.buttonHighlightText.r, qgcPal.buttonHighlightText.g, qgcPal.buttonHighlightText.b, 0.5)
            font.pixelSize: Math.round(_sp * 0.9)
        }
    }

    // ── Refresh when data changes ──
    Connections {
        target: logModel
        function onDataChanged() {
            // Reset loaded flags so charts repopulate on next show
            vibChart._loaded = false
            gpsChart._loaded = false
            batChart._loaded = false
            ekfChart._loaded = false
            pidChart._loaded = false
            flowChart._loaded = false

            // Force-populate the currently visible chart
            if (!logModel || !logModel.hasData) return
            _populateCurrent()
        }
    }

    onCurrentChartChanged: _populateCurrent()

    function _populateCurrent() {
        if (!logModel || !logModel.hasData) return
        switch (currentChart) {
        case "vibration":
            if (!vibChart._loaded) {
                vibChart.populate(logModel.subsample(logModel.accelTimeSeries, 800),
                                   logModel.subsample(logModel.gyroTimeSeries, 800),
                                   logModel.flightModes)
                vibChart._loaded = true
            }
            break
        case "gps":
            if (!gpsChart._loaded) {
                gpsChart.populate(logModel.subsample(logModel.gpsTimeSeries, 800), logModel.flightModes)
                gpsChart._loaded = true
            }
            break
        case "battery":
            if (!batChart._loaded) {
                batChart.populate(logModel.subsample(logModel.batteryTimeSeries, 800), logModel.flightModes)
                batChart._loaded = true
            }
            break
        case "ekf":
            if (!ekfChart._loaded) {
                ekfChart.populate(logModel.subsample(logModel.ekfTimeSeries, 800), logModel.flightModes)
                ekfChart._loaded = true
            }
            break
        case "pid":
            if (!pidChart._loaded) {
                pidChart.populate(logModel.subsample(logModel.pidTimeSeries, 800), logModel.flightModes)
                pidChart._loaded = true
            }
            break
        case "opticalflow":
            if (!flowChart._loaded) {
                flowChart.populate(logModel.subsample(logModel.opticalFlowTimeSeries, 800), logModel.flightModes)
                flowChart._loaded = true
            }
            break
        }
    }

    // ── Helpers ──
    function _baseName(path) {
        if (!path) return ""
        var parts = path.split("/")
        return parts[parts.length - 1] || path
    }

    // Extracted summary metrics
    readonly property string _vibRms: {
        if (!logModel || !logModel.hasData) return ""
        var rms = logModel.vibration.rms
        if (!rms) return ""
        var z = rms.z !== undefined ? rms.z.toFixed(2) : ""
        return z ? (z + " m/s²") : ""
    }

    readonly property string _batSag: {
        if (!logModel || !logModel.hasData) return ""
        var sag = logModel.battery.voltageSag
        return sag !== undefined ? sag.toFixed(2) : ""
    }

    readonly property string _ekfScore: {
        if (!logModel || !logModel.hasData) return ""
        var score = logModel.ekf.healthScore
        return score !== undefined ? score.toFixed(0) : ""
    }
}
