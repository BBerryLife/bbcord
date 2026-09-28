import bb.cascades 1.4
import QtQuick 1.0

// Card launched by Hub for a short tap on a BBCord item (see
// ApplicationUI::initCardUI() and the invoke-target in bar-descriptor.xml).
// Not a real preview: Hub needs some card shown before handing off. It shows
// "Opening..." for a frame, then calls finishCardAndHandoff() on applicationUI, which
// selects the channel and calls cardDone() to bring the real UI forward.
Page {
    id: previewCardPage

    Container {
        horizontalAlignment: HorizontalAlignment.Center
        verticalAlignment: VerticalAlignment.Center

        Label {
            text: qsTr("Opening BBCord...")
        }
    }

    attachedObjects: [
        Timer {
            id: handoffTimer
            // Single-shot 0ms timer instead of calling finishCardAndHandoff() from
            // Component.onCompleted, which fires before the first frame and could race Hub's card
            // setup. Deferring one event-loop turn lets Cascades show the frame first
            // (like loginProgressTimer in LoginPage.qml).
            interval: 0
            repeat: false
            running: true
            onTriggered: {
                applicationUI.finishCardAndHandoff();
            }
        }
    ]
}
