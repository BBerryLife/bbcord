import bb.cascades 1.4
import QtQuick 1.0

// Fix (short-tap on a Hub item doesn't open the app): this is the
// card BlackBerry Hub launches for a short-tap on a BBCord item - see
// ApplicationUI::initCardUI()'s doc comment in applicationui.cpp/.hpp
// for the full "why a card" reasoning, and bar-descriptor.xml's
// <invoke-target id="ch.michioxd.bbcord.invoke"> (type card.previewer)
// for how Hub is told to route here.
//
// This card is NOT meant to be a real preview UI the user reads or
// interacts with - it exists purely because Hub's short-tap
// resolution requires SOME card to be shown, however briefly, before
// handing off. It shows a one-line "Opening..." message for a single
// frame, then calls back into C++ (finishCardAndHandoff(), Q_INVOKABLE
// on applicationUI) to do the actual channel-selection + cardDone()
// work that closes the card and brings BBCord's real UI to the
// foreground - replicating what long-press "Open in BBCord" already
// does successfully.
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
            // Fix (short-tap on a Hub item doesn't open the app): a
            // single-shot 0ms timer, rather than calling
            // finishCardAndHandoff() directly from
            // Component.onCompleted. Component.onCompleted fires
            // before this Page has actually been drawn/composited to
            // screen at least once - calling straight into C++ from
            // there to immediately close the card again (via
            // cardDone()) risks racing Hub's own card-open animation/
            // window setup. Deferring by one event-loop turn (interval
            // 0, non-repeating) lets Cascades finish showing this
            // frame first, same as the interval-based Timer pattern
            // already used elsewhere in this codebase (see
            // LoginPage.qml's loginProgressTimer) - just a one-shot
            // instead of repeating.
            interval: 0
            repeat: false
            running: true
            onTriggered: {
                applicationUI.finishCardAndHandoff();
            }
        }
    ]
}
