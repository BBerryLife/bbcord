import bb.cascades 1.4

Container {
    id: emojiItem

    // Fix: was "property string emojiUrl" on the root Container, with
    // the ImageView below binding "imageSource: parent.emojiUrl" - that
    // binding never re-evaluated when emojiUrl was set after creation
    // (see on-device log: "Expression ... depends on non-NOTIFYable
    // properties: bb::cascades::ImageView::parent"), so setting
    // item.emojiUrl from rebuildEmojiOnlyModel() in MessageBubble.qml
    // after createObject() had no visible effect. Property alias
    // straight to the ImageView's own imageSource sidesteps the
    // parent-binding path entirely - assigning emojiUrl now assigns
    // imageSource directly, which IS NOTIFYable on ImageView itself.
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
