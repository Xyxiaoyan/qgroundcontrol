import QtQuick
import QtQuick.Controls
import QGroundControl
import QGroundControl.Controls

// DJI Fly Style Top Status Bar
Rectangle {
    id: root
    height: 40
    color: "#00000000"
    property var vehicle

    Row {
        anchors { fill: parent; leftMargin: 16; rightMargin: 16; topMargin: 4 }
        spacing: 12

        // Flight Mode
        Rectangle {
            width: 72; height: 28; radius: 14
            anchors.verticalCenter: parent.verticalCenter
            color: "#33ffffff"; border { color: "#55ffffff"; width: 0.5 }
            QGCLabel {
                anchors.centerIn: parent
                text: vehicle ? vehicle.flightMode.value : "P-GPS"
                font.pointSize: 8
                color: "#ffffff"
            }
        }

        // GPS Status
        Row {
            anchors.verticalCenter: parent.verticalCenter; spacing: 3
            QGCLabel { text: "GPS"; font.pointSize: 8; color: "#ffffff" }
            QGCLabel {
                font.pointSize: 8; font.bold: true
                color: vehicle && vehicle.gps.count.rawValue > 0 ? "#00ff44" : "#ff4444"
                text: vehicle ? vehicle.gps.count.valueString : "0"
            }
        }

        Item { Layout.fillWidth: true }

        // Signal Strength
        Row {
            anchors.verticalCenter: parent.verticalCenter; spacing: 2
            Repeater {
                model: 4
                Rectangle {
                    width: 3; height: 5 + index * 2; radius: 1
                    anchors.verticalCenter: parent.verticalCenter
                    color: vehicle && vehicle.gps.count.rawValue > 5 * (index + 1) ? "#00ff44" : "#33ffffff"
                }
            }
        }

        // Battery
        Row {
            anchors.verticalCenter: parent.verticalCenter; spacing: 3
            Rectangle {
                width: 20; height: 10; radius: 1; color: "transparent"
                border { color: "#ffffff"; width: 0.8 }
                anchors.verticalCenter: parent.verticalCenter
                Rectangle {
                    anchors { left: parent.left; top: parent.top; bottom: parent.bottom; margins: 1 }
                    width: (parent.width - 2) * (vehicle && vehicle.battery.percent ? vehicle.battery.percent.value / 100 : 0.85)
                    radius: 1
                    color: vehicle && vehicle.battery.percent ? (vehicle.battery.percent.value > 30 ? "#00ff44" : (vehicle.battery.percent.value > 15 ? "#ffaa00" : "#ff4444")) : "#00ff44"
                }
            }
            QGCLabel {
                text: vehicle && vehicle.battery.percent ? vehicle.battery.percent.valueString + "%" : "85%"
                font.pointSize: 8; color: "#ffffff"
            }
        }

        // Settings
        QGCLabel { text: "\u2699"; font.pointSize: 12; color: "#ffffff" }
    }
}
