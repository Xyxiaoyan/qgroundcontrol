import QtQuick
import QGroundControl

Item {
    id: _root
    property var parentToolInsets
    property var totalToolInsets
    property var mapControl
    property var videoControl
    property var _agentPanel: null
    property string _agentStatus: ""
    readonly property real _highZ: QGroundControl.zOrderTopMost
    readonly property var qgcPal: QGroundControl.globalPalette
    // ScreenTools unavailable in QRC context — measure font height for scale
    readonly property real _sp: _measureText.fontPixelSize
    Text {
        id: _measureText
        text: "X"
        font.pointSize: 10
        readonly property real fontPixelSize: Math.max(Math.round(contentHeight), 8)
    }

    Component.onCompleted: {
        var comp = Qt.createComponent("qrc:/Custom/qml/QGroundControl/FlyView/AgentPanel.qml")
        if (comp.status !== Component.Ready) {
            _agentStatus = "Component: " + comp.errorString()
            return
        }
        var props = {"z": _highZ}
        if (typeof AgentManager !== "undefined" && AgentManager !== null) {
            props._agentMgr = AgentManager
        }
        _agentPanel = comp.createObject(_root, props)
        if (_agentPanel) {
            _agentStatus = "OK"
        } else {
            _agentStatus = "createObject failed: " + comp.errorString()
        }
    }

    // ═══ AI Assistant Button ═══
    // Matches QGC ToolStripHoverButton visual style
    Rectangle {
        id: _btn
        z: _highZ
        anchors { top: parent.top; right: parent.right; margins: _sp }
        width: _sp * 4.6; height: width
        radius: Math.round(_sp * 0.4)
        color: _ma.pressed ? qgcPal.buttonHighlight
             : _hoverArea.hovered ? Qt.rgba(qgcPal.buttonHighlight.r, qgcPal.buttonHighlight.g, qgcPal.buttonHighlight.b, 0.2)
             : qgcPal.windowTransparent
        border {
            color: qgcPal.buttonHighlight
            width: 1.5
        }

        // Icon (text-based since QRC can't use QGCColoredImage)
        Text {
            anchors.centerIn: parent
            text: "AI"
            color: _ma.pressed || _hoverArea.hovered
                   ? qgcPal.buttonHighlightText : qgcPal.buttonText
            font.pixelSize: Math.round(_sp * 1.3)
            font.weight: Font.Bold
        }

        MouseArea {
            id: _ma
            anchors.fill: parent
            hoverEnabled: true
            onClicked: {
                if (_agentPanel) {
                    _agentPanel.isExpanded ? _agentPanel.close() : _agentPanel.open()
                }
            }
        }

        HoverHandler { id: _hoverArea }

        // Tooltip
        QGToolTip {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.bottom
            anchors.topMargin: Math.round(_sp * 0.3)
            text: "AI 调试助手"
            visible: _hoverArea.hovered && _agentStatus === "OK"
        }
    }

    // ═══ Vision Capture Button ═══
    Rectangle {
        id: _camBtn
        z: _highZ
        anchors { top: _btn.bottom; right: parent.right; topMargin: Math.round(_sp * 0.4); rightMargin: _sp }
        width: _sp * 4; height: width
        radius: Math.round(_sp * 0.4)
        color: _camMa.pressed ? Qt.rgba(qgcPal.colorGreen.r, qgcPal.colorGreen.g, qgcPal.colorGreen.b, 0.4)
             : _camHover.hovered ? Qt.rgba(qgcPal.colorGreen.r, qgcPal.colorGreen.g, qgcPal.colorGreen.b, 0.2)
             : qgcPal.windowTransparent
        border {
            color: qgcPal.colorGreen
            width: 1.5
        }
        visible: true

        Text {
            anchors.centerIn: parent
            text: "📷"
            color: qgcPal.buttonText
            font.pixelSize: Math.round(_sp * 1.2)
        }

        MouseArea {
            id: _camMa
            anchors.fill: parent
            hoverEnabled: true
            onClicked: {
                if (!_agentPanel) return
                var target = (videoControl && QGroundControl.videoManager && QGroundControl.videoManager.hasVideo)
                    ? videoControl : _root
                try {
                    target.grabToImage(function(result) {
                        if (result) {
                            var base64 = _imageToBase64(result)
                            if (_agentPanel._onVisionCapture) {
                                _agentPanel._onVisionCapture(base64)
                            }
                        }
                    }, Qt.size(512, 288))
                } catch(e) {
                    _agentStatus = "capture err: " + e
                }
            }
        }

        HoverHandler { id: _camHover }

        QGToolTip {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.bottom
            anchors.topMargin: Math.round(_sp * 0.3)
            text: "截图分析"
            visible: _camHover.hovered && _agentStatus === "OK"
        }
    }

    // ═══ Simple Tooltip ═══
    component QGToolTip : Rectangle {
        property alias text: tipText.text
        property real _spVal: _sp
        width: tipText.implicitWidth + 12
        height: tipText.implicitHeight + 8
        radius: Math.round(_spVal * 0.3)
        color: qgcPal.windowShade
        border { color: qgcPal.groupBorder; width: 1 }
        z: _highZ + 1
        Text {
            id: tipText
            anchors.centerIn: parent
            color: qgcPal.text
            font.pixelSize: Math.round(_spVal * 0.9)
        }
    }

    // ═══ Canvas → Base64 (vision capture utility) ═══
    function _imageToBase64(img) {
        var canvas = Qt.createQmlObject('import QtQuick; Canvas { visible: false }', _root, "tmpCanvas")
        try {
            canvas.width = img.width
            canvas.height = img.height
            var ctx = canvas.getContext("2d")
            ctx.drawImage(img, 0, 0)
            var dataUrl = canvas.toDataURL("image/jpeg", 0.7)
            return dataUrl.split(",")[1] || ""
        } finally {
            canvas.destroy()
        }
    }

    // ═══ Status indicator ═══
    Text {
        z: _highZ
        anchors { top: _camBtn.bottom; right: parent.right; topMargin: Math.round(_sp * 0.4); rightMargin: _sp }
        text: _agentStatus
        color: _agentStatus === "OK" ? qgcPal.colorGreen : qgcPal.colorRed
        font.pixelSize: Math.round(_sp * 0.75)
        visible: _agentStatus !== ""
    }
}
