import bb.cascades 1.4

Container {
    id: emojiItem

    // emojiUrl is a property alias to the ImageView's imageSource: a binding through
    // "parent.emojiUrl" did not re-evaluate (non-NOTIFYable parent), so setting it after
    // createObject() in MessageBubble.qml had no effect.
    property alias emojiUrl: image.imageSource
    // Edge length in du: 6.0 for the jumbo emoji-only row, set smaller by
    // MessageBubble.qml when the emoji sits inside a line of text.
    property real sizeDu: 6.0

    rightMargin: ui.du(0.6)
    bottomMargin: ui.du(0.6)
    preferredWidth: ui.du(emojiItem.sizeDu)
    preferredHeight: ui.du(emojiItem.sizeDu)

    ImageView {
        id: image
        preferredWidth: ui.du(emojiItem.sizeDu)
        preferredHeight: ui.du(emojiItem.sizeDu)
        scalingMethod: ScalingMethod.AspectFit
    }
}
