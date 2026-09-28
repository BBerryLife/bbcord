import bb.cascades 1.4

Container {
    id: emojiItem

    // emojiUrl is a property alias to the ImageView's imageSource: a binding through
    // "parent.emojiUrl" did not re-evaluate (non-NOTIFYable parent), so setting it after
    // createObject() in MessageBubble.qml had no effect.
    property alias emojiUrl: image.imageSource

    rightMargin: ui.du(0.6)
    bottomMargin: ui.du(0.6)
    preferredWidth: ui.du(6.0)
    preferredHeight: ui.du(6.0)

    ImageView {
        id: image
        preferredWidth: ui.du(6.0)
        preferredHeight: ui.du(6.0)
        scalingMethod: ScalingMethod.AspectFit
    }
}
