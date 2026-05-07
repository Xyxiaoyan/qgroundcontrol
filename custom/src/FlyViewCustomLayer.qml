import QtQuick
import QGroundControl

Item {
    property var parentToolInsets
    property var totalToolInsets
    property var mapControl
    property var _agentPanel: null
    property string _agentStatus: ""

    Component.onCompleted: {
        var comp = Qt.createComponent("qrc:/Custom/qml/QGroundControl/FlyView/AgentPanel.qml")
        if (comp.status === Component.Ready) {
            _agentPanel = comp.createObject(_root)
            if (_agentPanel) _agentStatus = "OK"
            else _agentStatus = "createObject failed: " + comp.errorString()
        } else {
            _agentStatus = "Component: " + comp.errorString()
        }
    }

    Rectangle {
        anchors { top: parent.top; right: parent.right; margins: 10 }
        width: 50; height: 30; radius: 5
        color: _ma.pressed ? "#774A90D9" : "#CC1a2a4a"
        border { color: "#4A90D9"; width: 1.5 }
        Text {
            anchors.centerIn: parent
            text: "AI"
            color: "white"
            font.pixelSize: 14
            font.weight: Font.Bold
        }
        MouseArea {
            id: _ma
            anchors.fill: parent
            onClicked: {
                if (_agentPanel) {
                    _agentPanel.isExpanded ? _agentPanel.close() : _agentPanel.open()
                }
            }
        }
    }

    Text {
        anchors { top: parent.top; right: parent.right; topMargin: 44; rightMargin: 10 }
        text: _agentStatus
        color: _agentStatus === "OK" ? "#44ff44" : "#ff4444"
        font.pixelSize: 8
        visible: _agentStatus !== ""
    }
}
