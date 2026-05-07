import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.ScreenTools

// ============================================================================
// DJI Fly Tool Strip - Vertical action buttons on the left
// ============================================================================

Item {
    anchors.fill: parent

    // We override the default tool strip via FlyViewToolStripActionList
    // The action list is defined in FlyViewToolStripActionList.qml
    Component.onCompleted: {
        console.log("DJI Fly ToolStrip loaded")
    }
}
