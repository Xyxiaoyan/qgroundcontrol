import QtQuick
import QtQuick.Controls
import QGroundControl
import QGroundControl.Controls

// DJI Fly Style Mini Map Placeholder
Rectangle {
    id: root
    color: "#cc1a1a2e"
    radius: 8
    border { color: "#33ffffff"; width: 0.5 }

    QGCLabel {
        anchors.centerIn: parent
        text: "MAP"
        color: "#445566"
        font.pointSize: 10
    }

    // North indicator
    QGCLabel {
        anchors { top: parent.top; topMargin: 3; horizontalCenter: parent.horizontalCenter }
        text: "N"
        color: "#ff4444"
        font { pointSize: 7; bold: true }
    }
}
