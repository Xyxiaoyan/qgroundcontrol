import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QGroundControl
import QGroundControl.Controls

// DJI Fly Style Bottom Control Bar
Rectangle {
    id: root
    height: 110
    color: "#00000000"
    property var vehicle
    property bool _isArmed: vehicle ? vehicle.armed : false

    // Takeoff/Land/RTH actions via Guided Actions (QGC's built-in)
    Row {
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 24 }
        spacing: 16

        // RTH button
        Rectangle {
            width: 48; height: 48; radius: 24
            anchors.verticalCenter: parent.verticalCenter
            color: "#22ffffff"; border { color: "#ffaa00"; width: 1.5 }
            visible: root._isArmed
            QGCLabel { anchors.centerIn: parent; text: "RTH"; font.pointSize: 8; color: "#ffaa00" }
            MouseArea {
                anchors.fill: parent
                onClicked: {
                    if (vehicle) {
                        vehicle.guidedModeRTL()
                    }
                }
            }
        }

        // Takeoff / Land
        Rectangle {
            width: 60; height: 60; radius: 30
            color: root._isArmed ? "#33ff4444" : "#3344ff44"
            border { color: root._isArmed ? "#ff4444" : "#44ff44"; width: 2 }
            QGCLabel {
                anchors.centerIn: parent
                text: root._isArmed ? "LAND" : "TAKE OFF"
                font.pointSize: 10; font.bold: true
                color: root._isArmed ? "#ff4444" : "#44ff44"
            }
            MouseArea {
                anchors.fill: parent
                onClicked: {
                    if (!vehicle) return
                    if (root._isArmed) {
                        vehicle.guidedModeLand()
                    } else {
                        vehicle.guidedModeTakeoff()
                    }
                }
            }
        }

        // Camera/Record
        Rectangle {
            width: 48; height: 48; radius: 24
            anchors.verticalCenter: parent.verticalCenter
            color: "#33ffffff"; border { color: "#ff4444"; width: 1.5 }
            Rectangle {
                anchors.centerIn: parent
                width: 16; height: 16; radius: 3
                color: "#ff4444"
            }
            MouseArea {
                anchors.fill: parent
                onClicked: {
                    if (vehicle) vehicle.videoStreamManager().toggleVideoRecord()
                }
            }
        }
    }

    // Camera mode selector
    Row {
        anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: 4 }
        spacing: 20
        QGCLabel { text: "VIDEO"; font.pointSize: 8; color: "#44ff44" }
        QGCLabel { text: "PHOTO"; font.pointSize: 8; color: "#66ffffff" }
    }
}
