import QtQuick
import QtQuick.Controls
import QGroundControl
import QGroundControl.Controls
import QGroundControl.ScreenTools

// DJI Fly Style Left Telemetry
Item {
    id: root
    width: 130; height: childrenRect.height + 20
    property var vehicle

    Column {
        anchors { left: parent.left; verticalCenter: parent.verticalCenter }
        spacing: 12

        // Altitude (large)
        Column { spacing: 0
            QGCLabel {
                text: vehicle ? (isNaN(vehicle.altitudeRelative.rawValue) ? "0.0" : vehicle.altitudeRelative.rawValue.toFixed(1)) : "0.0"
                font.pointSize: 22; font.bold: true; color: "#ffffff"
            }
            QGCLabel { text: "H  m"; font.pointSize: 8; color: "#88aaff" }
        }

        // Distance
        Column { spacing: 0
            QGCLabel {
                text: vehicle ? (isNaN(vehicle.distanceToHome.rawValue) ? "0" : vehicle.distanceToHome.rawValue.toFixed(0)) : "0"
                font.pointSize: 18; font.bold: true; color: "#ffffff"
            }
            QGCLabel { text: "D  m"; font.pointSize: 8; color: "#88aaff" }
        }

        // Horizontal Speed
        Column { spacing: 0
            QGCLabel {
                text: vehicle ? (isNaN(vehicle.groundSpeed.rawValue) ? "0.0" : vehicle.groundSpeed.rawValue.toFixed(1)) : "0.0"
                font.pointSize: 16; font.bold: true; color: "#ffffff"
            }
            QGCLabel { text: "H.S  m/s"; font.pointSize: 8; color: "#88aaff" }
        }

        // Vertical Speed
        Column { spacing: 0
            QGCLabel {
                text: {
                    var vs = vehicle ? (isNaN(vehicle.climbRate.rawValue) ? 0 : vehicle.climbRate.rawValue) : 0
                    return (vs >= 0 ? "+" : "") + vs.toFixed(1)
                }
                font.pointSize: 16; font.bold: true; color: "#ffffff"
            }
            QGCLabel { text: "V.S  m/s"; font.pointSize: 8; color: "#88aaff" }
        }
    }
}
