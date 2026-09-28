import bb.cascades 1.4
import bb.system 1.2
import QtQuick 1.0

Container {
    id: root

    property string messageId: ""
    property string authorId: ""
    property string author: ""
    property string initials: ""
    property string avatarSource: ""
    property string avatarColor: "#5865F2"
    property string time: ""
    property real timestampMs: 0
    property string message: ""
    property string messageHtml: ""
    property variant emojiSegments
    // Fix: emojiOnlyRow.count read back as "[object Object]" on-device
    // (confirmed via diagnostic logging), not a number - meaning
    // Container.count either isn't a real property on this Cascades
    // version, or isn't accessible the way assumed. Every previous
    // round of this bug (icons accumulating across recycles/reloads)
    // traces back to this single fact: "while (emojiOnlyRow.count > 0)"
    // compares a number against an object, which JS resolves to NaN,
    // and "NaN > 0" is always false - so the clear-loop in
    // rebuildEmojiOnlyModel() never actually ran, on any call, ever.
    // Every rebuild only ever added more controls on top of whatever
    // was already there. Tracking the created controls in this plain
    // JS array instead sidesteps Container.count/controlAt() entirely -
    // .push()/.length/array indexing are JS fundamentals with zero
    // dependency on Cascades' Container API being what past rounds
    // assumed it was.
    property variant emojiOnlyItems: []
    property bool emojiOnly: false
    property string replyAuthor: ""
    property string replyMessage: ""
    property string replyMessageHtml: ""
    property string replyMessageId: ""
    property bool mentionsCurrentUser: false
    // Fix: transient flash state for "jump to original message" (tap
    // a reply-quote box - see replyPreviewTapped below / ChatCard.qml's
    // scrollToMessage()) - separate from mentionsCurrentUser
    // (persistent ping highlight) since this one is set briefly by the
    // parent then cleared, on ANY message regardless of whether it
    // pings anyone.
    property bool jumpHighlighted: false
    property string image: ""
    property int imageWidth: 0
    property int imageHeight: 0
    property string attachmentUrl: ""
    property string attachmentName: ""
    property bool attachmentIsImage: false
    property variant attachments
    property bool isGroupStart: true
    property bool isGroupEnd: true
    property bool showAvatar: true
    property bool showUsername: true
    property bool showTimestamp: true
    property bool compactMessage: false
    property bool pending: false
    property bool failed: false
    property bool edited: false
    property bool imageLoading: false
    property bool imageLoadFailed: false
    property bool previewVisible: false
    property bool ownMessage: false
    property bool deleteAllowed: false
    property int attachmentTotal: 0
    property int maxInlineAttachments: 6
    property real maxImageWidthDu: 50.4
    property real maxImageHeightDu: 43.2
    property real minImageWidthDu: 16.8

    signal editRequested(string messageId, string message)
    signal deleteRequested(string messageId)
    signal replyRequested(string messageId, string author, string message)
    signal replyPreviewTapped(string messageId)
    signal copyRequested(string text)
    signal attachmentOpenRequested(string url)
    signal imagePreviewRequested(string url, int imageWidth, int imageHeight)
    signal attachmentImageLoadRequested(string url)

    horizontalAlignment: HorizontalAlignment.Fill
    preferredWidth: ui.du(100.0)
    leftPadding: root.compactMessage ? ui.du(1.0) : ui.du(2.0)
    rightPadding: ui.du(2.0)
    topPadding: root.compactMessage ? ui.du(0.1) : (root.isGroupStart ? ui.du(1.5) : ui.du(0.1))
    bottomPadding: root.compactMessage ? ui.du(0.1) : (root.isGroupEnd ? ui.du(1.0) : ui.du(0.2))
    // Fix: Discord highlights a message with a soft yellow tint over
    // the dark theme background when it @mentions the current user
    // (directly, via @everyone/@here, or via a role they have - see
    // "mentionsCurrentUser" in ChatController::prepareMessageForModel())
    // - this reproduces that. jumpHighlighted (the brief flash when
    // jumping to a message via its reply-quote box) uses the exact same
    // color, matching Discord's own behavior of using one highlight
    // color for both cases. Fully transparent otherwise, so the
    // background of the chat list itself shows through as normal.
    background: (root.mentionsCurrentUser || root.jumpHighlighted) ? Color.create("#3A3427") : Color.Transparent

    onJumpHighlightedChanged: {
        if (jumpHighlighted) {
            // Fix: QtQuick 1.0's Timer (Cascades 10 / QML1) may not have
            // a restart() method (that's a QML2/Qt5 addition) - toggling
            // running off then on is the safe QML1-compatible way to
            // reset interval + re-trigger.
            jumpHighlightTimer.running = false;
            jumpHighlightTimer.running = true;
        }
    }

    layout: StackLayout {
        orientation: LayoutOrientation.LeftToRight
    }

    contextActions: [
        ActionSet {
            title: root.author
            subtitle: root.message

            actions: [
                ActionItem {
                    title: qsTr("Reply")
                    imageSource: "asset:///images/icons/ic_reply.png"

                    onTriggered: {
                        root.replyRequested(root.messageId, root.author, root.message);
                    }
                },
                ActionItem {
                    title: qsTr("Copy message")
                    imageSource: "asset:///images/icons/ic_copy.png"
                    enabled: root.message !== ""

                    onTriggered: {
                        root.copyRequested(root.message);
                    }
                },
                ActionItem {
                    title: qsTr("Open attachment")
                    imageSource: "asset:///images/icons/ic_open.png"
                    enabled: root.attachmentUrl !== ""

                    onTriggered: {
                        root.attachmentOpenRequested(root.attachmentUrl);
                    }
                },
                ActionItem {
                    title: qsTr("Edit")
                    imageSource: "asset:///images/icons/ic_edit.png"
                    enabled: root.isOwnMessage() && !root.pending && !root.failed

                    onTriggered: {
                        root.editRequested(root.messageId, root.message);
                    }
                },
                DeleteActionItem {
                    title: qsTr("Delete")
                    imageSource: "asset:///images/icons/action_delete.png"
                    enabled: root.canDeleteMessage()
                                    
                    onTriggered: {
                        deleteMessageDialog.show();
                    }
                }
            ]
        }
    ]

    attachedObjects: [
        ArrayDataModel {
            id: attachmentModel
        },
        Timer {
            id: jumpHighlightTimer
            interval: 1500
            repeat: false
            onTriggered: {
                root.jumpHighlighted = false;
            }
        },
        // Fix: rebuildEmojiOnlyModel() used to run synchronously from
        // three different triggers (root.onCreationCompleted,
        // root.onMessageIdChanged, root.onEmojiSegmentsChanged) on a
        // recycled ListView delegate (see those handlers above). QML
        // doesn't guarantee messageId and emojiSegments finish
        // updating in the same tick when Cascades rebinds a recycled
        // delegate's properties, so a trigger could fire and read
        // root.emojiSegments before it had been reassigned to the new
        // message's value yet - reading the previous message's
        // segments (or none) despite messageId already reading as the
        // new message. Routing every trigger through a zero-interval
        // singleShot Timer instead defers the actual rebuild to the
        // next event-loop tick, by which point every property
        // reassignment for this recycle has landed, AND coalesces the
        // 2-3 near-simultaneous trigger firings into a single rebuild
        // (each retrigger just restarts the timer - see
        // scheduleEmojiOnlyRebuild()).
        Timer {
            id: emojiOnlyRebuildTimer
            interval: 0
            repeat: false
            onTriggered: {
                root.rebuildEmojiOnlyModel();
            }
        },
        // Fix: this ComponentDefinition used to live in the nested
        // emojiOnlyRow Container's own attachedObjects instead of
        // root's here. On-device log showed a flood of "Error:
        // Accessing ListItem.view on a node that is not the root node
        // in a list item visual" warnings the moment the message list
        // rendered, plus stray large gaps between the emoji icon and
        // the rest of the bubble (visible in the "1 emoji" screenshot)
        // - both point at Cascades trying to resolve the ListItem
        // attached-property context for attachedObjects declared on a
        // non-root node of a ListView delegate, which this codebase's
        // own comments elsewhere (see ChatCard.qml's ListItem.view
        // notes) already flag as invalid; only the delegate's ROOT
        // node has a valid ListItem context. emojiOnlyRebuildTimer
        // above was already correctly on root's attachedObjects - this
        // ComponentDefinition just hadn't been given the same
        // treatment yet.
        ComponentDefinition {
            id: emojiOnlyItemDefinition
            // Every other ComponentDefinition in this codebase
            // (MainPage.qml, main.qml, LoginPage.qml) uses the full
            // "asset:///Name.qml" form, not a bare relative filename -
            // matching that here rather than relying on relative-path
            // resolution working the same way in this context.
            source: "asset:///EmojiOnlyItem.qml"
        },
        SystemDialog {
            id: deleteMessageDialog
            title: qsTr("Delete message")
            body: qsTr("Delete this message?")
            confirmButton.label: qsTr("Delete")
            cancelButton.label: qsTr("Cancel")

            onFinished: {
                if (result == SystemUiResult.ConfirmButtonSelection) {
                    root.deleteRequested(root.messageId);
                }
            }
        }
    ]

    onCreationCompleted: {
        root.rebuildAttachmentModel();
        root.scheduleEmojiOnlyRebuild();
    }

    onAttachmentsChanged: {
        root.rebuildAttachmentModel();
    }

    // Fix: MessageBubble is a ListView delegate (see ChatCard.qml's
    // listItemComponents), so Cascades recycles the same instance
    // across different messages as the user scrolls instead of
    // creating a fresh one each time. rebuildEmojiOnlyModel() only
    // fired off onEmojiSegmentsChanged, and QML property-changed
    // signals can go stale on a recycled delegate (observed on-device:
    // jumbo-emoji rows carrying over leftover icons from whatever
    // message previously occupied this recycled instance, on a
    // message that has NO emoji at all, with the count varying between
    // identical reloads - exactly what stale recycled state looks
    // like). messageId changes on every single recycle with no
    // exceptions, unlike emojiSegments (whose reference/contents
    // Cascades' change-detection might treat as equivalent in some
    // recycle orderings) - rebuilding on it is what actually
    // guarantees this runs for every message this delegate instance
    // ever gets bound to.
    onMessageIdChanged: {
        root.scheduleEmojiOnlyRebuild();
    }

    onEmojiSegmentsChanged: {
        root.scheduleEmojiOnlyRebuild();
    }

    Container {
        visible: !root.compactMessage
        preferredWidth: ui.du(8.0)
        verticalAlignment: VerticalAlignment.Top

        Container {
            visible: root.showAvatar && root.avatarSource === ""
            preferredWidth: ui.du(7.0)
            preferredHeight: ui.du(7.0)
            maxWidth: ui.du(7.0)
            minWidth: ui.du(7.0)
            maxHeight: ui.du(7.0)
            minHeight: ui.du(7.0)
            background: Color.create(root.avatarColor)

            layout: DockLayout {}

            Label {
                text: root.initials
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Center
                textStyle.fontSize: FontSize.Small
                textStyle.fontWeight: FontWeight.Bold
                textStyle.color: Color.White
            }
        }

        ImageView {
            visible: root.showAvatar && root.avatarSource !== ""
            imageSource: root.avatarSource
            preferredWidth: ui.du(7.0)
            preferredHeight: ui.du(7.0)
            maxWidth: ui.du(7.0)
            minWidth: ui.du(7.0)
            maxHeight: ui.du(7.0)
            minHeight: ui.du(7.0)
            scalingMethod: ScalingMethod.AspectFill
        }
    }

    Container {
        horizontalAlignment: HorizontalAlignment.Fill
        layoutProperties: StackLayoutProperties {
            spaceQuota: 1.0
        }
        leftMargin: ui.du(1.5)
        rightPadding: ui.du(0.1)

        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            visible: !root.compactMessage && (root.showUsername || root.showTimestamp)

            layout: StackLayout {
                orientation: LayoutOrientation.LeftToRight
            }

            Label {
                text: root.author
                visible: root.showUsername
                textStyle.fontSize: FontSize.Small
                textStyle.fontWeight: FontWeight.Normal
                textStyle.color: Color.create("#F2F3F5")
                verticalAlignment: VerticalAlignment.Center
            }

            Label {
                text: root.time
                visible: root.showTimestamp
                leftMargin: ui.du(0.2)
                opacity: 0.5
                textStyle.fontSize: FontSize.XXSmall
                textStyle.color: Color.create("#C9CDD3")
                verticalAlignment: VerticalAlignment.Center
            }

            Label {
                text: root.failed ? qsTr("failed") : (root.pending ? qsTr("sending") : (root.edited ? qsTr("edited") : ""))
                visible: root.failed || root.pending || root.edited
                leftMargin: ui.du(0.5)
                opacity: 0.55
                textStyle.fontSize: FontSize.XXSmall
                textStyle.color: root.failed ? Color.create("#ED4245") : Color.create("#C9CDD3")
                verticalAlignment: VerticalAlignment.Center
            }
        }

        Container {
            visible: root.replyAuthor !== ""
            horizontalAlignment: HorizontalAlignment.Fill
            topMargin: ui.du(0.2)
            bottomMargin: ui.du(0.8)

            layout: StackLayout {
                orientation: LayoutOrientation.LeftToRight
            }

            gestureHandlers: [
                TapHandler {
                    onTapped: {
                        if (root.replyMessageId !== "") {
                            root.replyPreviewTapped(root.replyMessageId);
                        }
                    }
                }
            ]

            Container {
                preferredWidth: ui.du(0.5)
                minWidth: ui.du(0.5)
                maxWidth: ui.du(0.5)
                verticalAlignment: VerticalAlignment.Fill
                background: Color.create("#5865F2")
            }

            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                leftMargin: ui.du(1.0)
                leftPadding: ui.du(1.0)
                rightPadding: ui.du(1.0)
                topPadding: ui.du(0.6)
                bottomPadding: ui.du(0.6)
                background: Color.create('#151617')
                // Fix: reply boxes longer than ~2 lines stopped
                // responding to taps - the tap handler itself was
                // fine (short replies worked), the multiline HTML
                // Label below was intercepting the touch once it grew
                // past a couple of lines instead of letting it reach
                // the TapHandler on the outer reply Container.
                // PassThrough here and on the Label makes both act as
                // transparent to touch, so the tap always reaches the
                // outer Container regardless of how many lines the
                // reply preview wraps to.
                touchPropagationMode: TouchPropagationMode.PassThrough

                Label {
                    text: root.replyAuthor
                    textStyle.fontSize: FontSize.XXSmall
                    textStyle.fontWeight: FontWeight.Normal
                    textStyle.color: Color.create("#5865F2")
                }

                Label {
                    text: root.replyMessageHtml
                    topMargin: ui.du(-0.3)
                    multiline: true
                    // Fix: Cascades' Label has no line-count-limiting
                    // property (confirmed via a real "Cannot assign to
                    // non-existent property \"maxLineCount\"" QML load
                    // error - that's a QtQuick2 Text property, not
                    // available here) - reply previews just wrap fully
                    // now, relying on touchPropagationMode below to
                    // keep the tap working regardless of how many
                    // lines that ends up being.
                    textFormat: TextFormat.Html
                    opacity: 0.85
                    textStyle.fontSize: FontSize.XXSmall
                    textStyle.color: Color.create("#B5BAC1")
                    touchPropagationMode: TouchPropagationMode.PassThrough
                }
            }
        }

        Label {
            visible: !root.emojiOnly
            text: root.compactMessage ? root.compactMessageHtml() : root.messageHtml
            horizontalAlignment: HorizontalAlignment.Fill
            topMargin: root.compactMessage ? ui.du(0.0) : (root.isGroupStart ? ui.du(-0.6) : ui.du(-0.2))
            multiline: true
            textFormat: TextFormat.Html
            textStyle.fontSize: FontSize.XSmall
            textStyle.color: Color.create("#DCDDDE")
        }

        // Fix: Discord renders a message that's ONLY custom emoji (see
        // EmojiUtils::isEmojiOnly()) as large "jumbo" emoji with no
        // surrounding bubble text - this is that path. Cascades' Label
        // can't draw <img> inline with text (see EmojiUtils.hpp's class
        // comment), so this is a plain row of ImageViews instead of
        // trying to reproduce Discord's inline-with-text emoji styling,
        // which would need rich-text-with-embedded-image support this
        // toolkit doesn't have. Mixed text+emoji messages fall back to
        // the ":name:" text rendering in messageHtml above instead of
        // showing here.
        //
        // Fix: originally used QtQuick's Repeater bound to an
        // ArrayDataModel, copying the attachment-row pattern below -
        // but Repeater isn't a Cascades type at all (Cascades has no
        // free-standing repeat-into-layout control; every other list in
        // this codebase - ChannelMemberList.qml, ServerList.qml,
        // DmList.qml - uses ListView, which is built for scrolling
        // lists, not a handful of inline icons in a flow row). That
        // caused "Repeater is not a type" at asset load and took the
        // whole ChatCard down with it. Cascades' actual answer for
        // "build N controls at runtime" is ComponentDefinition.
        // createObject(), driven from rebuildEmojiOnlyModel() below.
        //
        // Fix: this Container used to also have its own
        // onCreationCompleted: root.rebuildEmojiOnlyModel() - removed,
        // because as a child of root it necessarily finishes
        // construction BEFORE root's own onCreationCompleted fires,
        // i.e. before root.emojiSegments has had any chance to be
        // bound from ChatCard.qml's ListItemData.emojiSegments yet.
        // That early, wrong-data rebuild was itself a source of the
        // "wrong emoji count on a recycled delegate" symptom this
        // whole block of fixes addresses - root.onCreationCompleted
        // (via scheduleEmojiOnlyRebuild(), deferred a tick so property
        // binding has settled) already covers first-creation properly.
        Container {
            id: emojiOnlyRow
            visible: root.emojiOnly
            horizontalAlignment: HorizontalAlignment.Left
            topMargin: root.isGroupStart ? ui.du(0.2) : ui.du(0.1)
            // Fix: "FlowListLayout" isn't a real Cascades type (this
            // codebase's other containers only ever use StackLayout /
            // DockLayout / StackListLayout - see e.g. ChatCard.qml,
            // MainPage.qml). Referencing a nonexistent QML type doesn't
            // fail to parse; it silently becomes an untyped object, so
            // the crash only surfaced later as "Cannot assign object to
            // property" on the "layout:" assignment itself. A
            // horizontal StackLayout is Cascades' real equivalent for a
            // left-to-right row (it just won't auto-wrap to a second
            // line, unlike a true flow layout - acceptable here since
            // jumbo-emoji messages are a handful of icons, not dozens).
            layout: StackLayout {
                orientation: LayoutOrientation.LeftToRight
            }
        }

        Container {
            visible: root.attachmentTotal <= 1 && root.attachmentIsImage && !root.imageLoadFailed && root.attachmentUrl !== ""
            preferredWidth: ui.du(root.displayImageWidth())
            preferredHeight: ui.du(root.displayImageHeight())
            minWidth: ui.du(root.displayImageWidth())
            minHeight: ui.du(root.displayImageHeight())
            maxWidth: ui.du(root.maxImageWidthDu)
            maxHeight: ui.du(root.maxImageHeightDu)
            topMargin: ui.du(1.0)

            layout: DockLayout {}

            // Fix: this ImageView used to fall back to
            // root.attachmentUrl (the remote https://cdn.discordapp.com/...
            // URL) whenever root.image was still empty. Cascades'
            // ImageView cannot load https, so the log filled with
            // "Unsupported scheme (https) used in url ... Image loading
            // aborted" and the preview stayed blank, while the TapHandler
            // below (which uses attachmentUrl) still opened the preview
            // sheet normally. root.image is always either a local
            // file:/// URI from ChatController's image cache
            // (cachedImageSource()/filePreviewSource()) or empty while
            // the download is in flight, so bind only to it and show
            // the ActivityIndicator meanwhile.
            ImageView {
                visible: root.image !== ""
                imageSource: root.image
                preferredWidth: ui.du(root.displayImageWidth())
                preferredHeight: ui.du(root.displayImageHeight())
                minWidth: ui.du(root.displayImageWidth())
                minHeight: ui.du(root.displayImageHeight())
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
                scalingMethod: ScalingMethod.AspectFit
            }

            gestureHandlers: [
                TapHandler {
                    onTapped: {
                        if (root.attachmentIsImage && root.attachmentUrl !== "") {
                            root.imagePreviewRequested(root.attachmentUrl, root.imageWidth, root.imageHeight);
                        }
                    }
                }
            ]

            ActivityIndicator {
                visible: root.imageLoading && root.image === ""
                running: visible
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Center
            }
        }

        Button {
            visible: root.attachmentTotal <= 1 && root.attachmentUrl !== "" && (!root.attachmentIsImage || root.imageLoadFailed)
            text: (root.attachmentIsImage ? qsTr("Open image: ") : qsTr("Open attachment: ")) + root.attachmentName
            topMargin: ui.du(1.0)
            horizontalAlignment: HorizontalAlignment.Left

            onClicked: {
                root.attachmentOpenRequested(root.attachmentUrl);
            }
        }

        ListView {
            visible: false
            horizontalAlignment: HorizontalAlignment.Fill
            preferredHeight: ui.du(52.0)
            minHeight: ui.du(52.0)
            topMargin: ui.du(1.0)
            dataModel: attachmentModel

            layout: StackListLayout {
                orientation: LayoutOrientation.LeftToRight
            }

            listItemComponents: [
                ListItemComponent {
                    type: ""

                    Container {
                        topMargin: ui.du(0.4)
                        bottomMargin: ui.du(0.4)
                        rightMargin: ui.du(1.0)
                        horizontalAlignment: HorizontalAlignment.Left
                        preferredWidth: ui.du(ListItemData.isImage ? ListItemData.displayWidthDu : 34.0)
                        preferredHeight: ui.du(47.2)

                        Container {
                            visible: ListItemData.isImage && (ListItemData.image !== "" || ListItemData.imageLoading)
                            preferredWidth: ui.du(ListItemData.displayWidthDu)
                            preferredHeight: ui.du(ListItemData.displayHeightDu)
                            minWidth: ui.du(ListItemData.displayWidthDu)
                            minHeight: ui.du(ListItemData.displayHeightDu)
                            maxWidth: ui.du(50.4)
                            maxHeight: ui.du(43.2)

                            layout: DockLayout {}

                            ImageView {
                                visible: ListItemData.image !== ""
                                imageSource: ListItemData.image
                                preferredWidth: ui.du(ListItemData.displayWidthDu)
                                preferredHeight: ui.du(ListItemData.displayHeightDu)
                                minWidth: ui.du(ListItemData.displayWidthDu)
                                minHeight: ui.du(ListItemData.displayHeightDu)
                                horizontalAlignment: HorizontalAlignment.Fill
                                verticalAlignment: VerticalAlignment.Fill
                                scalingMethod: ScalingMethod.AspectFit
                            }

                            ActivityIndicator {
                                visible: ListItemData.imageLoading && ListItemData.image === ""
                                running: ListItemData.imageLoading && ListItemData.image === ""
                                horizontalAlignment: HorizontalAlignment.Center
                                verticalAlignment: VerticalAlignment.Center
                            }
                        }

                        Button {
                            visible: ListItemData.url !== "" && (!ListItemData.isImage || ListItemData.imageLoadFailed)
                            text: (ListItemData.isImage ? qsTr("Open image: ") : qsTr("Open attachment: ")) + ListItemData.name
                            topMargin: ListItemData.isImage ? ui.du(0.5) : ui.du(0.0)
                            horizontalAlignment: HorizontalAlignment.Left

                            onClicked: {
                                root.attachmentOpenRequested(ListItemData.url);
                            }
                        }
                    }
                }
            ]

            function itemType(data, indexPath) {
                return "";
            }
        }

        Container {
            visible: root.attachmentTotal > 1
            horizontalAlignment: HorizontalAlignment.Fill
            topMargin: ui.du(1.0)
            preferredHeight: ui.du(root.inlineAttachmentRows() * 49.0)
            minHeight: ui.du(root.inlineAttachmentRows() * 49.0)

            layout: StackLayout {
                orientation: LayoutOrientation.LeftToRight
            }

            Container {
                visible: root.inlineAttachmentVisible(0)
                rightMargin: ui.du(1.0)
                preferredWidth: ui.du(root.inlineAttachmentWidth(0))
                preferredHeight: ui.du(47.2)

                layout: DockLayout {}

                ImageView {
                    visible: root.inlineAttachmentIsImage(0) && root.inlineAttachmentImage(0) !== ""
                    imageSource: root.inlineAttachmentImage(0)
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Fill
                    scalingMethod: ScalingMethod.AspectFit
                }

                ActivityIndicator {
                    visible: root.inlineAttachmentIsImage(0) && root.inlineAttachmentLoading(0) && root.inlineAttachmentImage(0) === ""
                    running: visible
                    horizontalAlignment: HorizontalAlignment.Center
                    verticalAlignment: VerticalAlignment.Center
                }

                Button {
                    visible: root.inlineAttachmentUrl(0) !== "" && (!root.inlineAttachmentIsImage(0) || root.inlineAttachmentFailed(0))
                    text: root.inlineAttachmentButtonText(0)
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Center

                    onClicked: {
                        root.attachmentOpenRequested(root.inlineAttachmentUrl(0));
                    }
                }

                gestureHandlers: [
                    TapHandler {
                        onTapped: {
                            if (root.inlineAttachmentIsImage(0) && root.inlineAttachmentUrl(0) !== "") {
                                root.imagePreviewRequested(root.inlineAttachmentUrl(0), root.inlineAttachmentImageWidth(0), root.inlineAttachmentImageHeight(0));
                            }
                        }
                    }
                ]
            }

            Container {
                visible: root.inlineAttachmentVisible(1)
                rightMargin: ui.du(1.0)
                preferredWidth: ui.du(root.inlineAttachmentWidth(1))
                preferredHeight: ui.du(47.2)

                layout: DockLayout {}

                ImageView {
                    visible: root.inlineAttachmentIsImage(1) && root.inlineAttachmentImage(1) !== ""
                    imageSource: root.inlineAttachmentImage(1)
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Fill
                    scalingMethod: ScalingMethod.AspectFit
                }

                ActivityIndicator {
                    visible: root.inlineAttachmentIsImage(1) && root.inlineAttachmentLoading(1) && root.inlineAttachmentImage(1) === ""
                    running: visible
                    horizontalAlignment: HorizontalAlignment.Center
                    verticalAlignment: VerticalAlignment.Center
                }

                Button {
                    visible: root.inlineAttachmentUrl(1) !== "" && (!root.inlineAttachmentIsImage(1) || root.inlineAttachmentFailed(1))
                    text: root.inlineAttachmentButtonText(1)
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Center

                    onClicked: {
                        root.attachmentOpenRequested(root.inlineAttachmentUrl(1));
                    }
                }

                gestureHandlers: [
                    TapHandler {
                        onTapped: {
                            if (root.inlineAttachmentIsImage(1) && root.inlineAttachmentUrl(1) !== "") {
                                root.imagePreviewRequested(root.inlineAttachmentUrl(1), root.inlineAttachmentImageWidth(1), root.inlineAttachmentImageHeight(1));
                            }
                        }
                    }
                ]
            }

            Container {
                visible: root.inlineAttachmentVisible(2)
                rightMargin: ui.du(1.0)
                preferredWidth: ui.du(root.inlineAttachmentWidth(2))
                preferredHeight: ui.du(47.2)

                layout: DockLayout {}

                ImageView {
                    visible: root.inlineAttachmentIsImage(2) && root.inlineAttachmentImage(2) !== ""
                    imageSource: root.inlineAttachmentImage(2)
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Fill
                    scalingMethod: ScalingMethod.AspectFit
                }

                ActivityIndicator {
                    visible: root.inlineAttachmentIsImage(2) && root.inlineAttachmentLoading(2) && root.inlineAttachmentImage(2) === ""
                    running: visible
                    horizontalAlignment: HorizontalAlignment.Center
                    verticalAlignment: VerticalAlignment.Center
                }

                Button {
                    visible: root.inlineAttachmentUrl(2) !== "" && (!root.inlineAttachmentIsImage(2) || root.inlineAttachmentFailed(2))
                    text: root.inlineAttachmentButtonText(2)
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Center

                    onClicked: {
                        root.attachmentOpenRequested(root.inlineAttachmentUrl(2));
                    }
                }

                gestureHandlers: [
                    TapHandler {
                        onTapped: {
                            if (root.inlineAttachmentIsImage(2) && root.inlineAttachmentUrl(2) !== "") {
                                root.imagePreviewRequested(root.inlineAttachmentUrl(2), root.inlineAttachmentImageWidth(2), root.inlineAttachmentImageHeight(2));
                            }
                        }
                    }
                ]
            }

            Container {
                visible: root.inlineAttachmentVisible(3)
                rightMargin: ui.du(1.0)
                preferredWidth: ui.du(root.inlineAttachmentWidth(3))
                preferredHeight: ui.du(47.2)

                layout: DockLayout {}

                ImageView {
                    visible: root.inlineAttachmentIsImage(3) && root.inlineAttachmentImage(3) !== ""
                    imageSource: root.inlineAttachmentImage(3)
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Fill
                    scalingMethod: ScalingMethod.AspectFit
                }

                ActivityIndicator {
                    visible: root.inlineAttachmentIsImage(3) && root.inlineAttachmentLoading(3) && root.inlineAttachmentImage(3) === ""
                    running: visible
                    horizontalAlignment: HorizontalAlignment.Center
                    verticalAlignment: VerticalAlignment.Center
                }

                Button {
                    visible: root.inlineAttachmentUrl(3) !== "" && (!root.inlineAttachmentIsImage(3) || root.inlineAttachmentFailed(3))
                    text: root.inlineAttachmentButtonText(3)
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Center

                    onClicked: {
                        root.attachmentOpenRequested(root.inlineAttachmentUrl(3));
                    }
                }

                gestureHandlers: [
                    TapHandler {
                        onTapped: {
                            if (root.inlineAttachmentIsImage(3) && root.inlineAttachmentUrl(3) !== "") {
                                root.imagePreviewRequested(root.inlineAttachmentUrl(3), root.inlineAttachmentImageWidth(3), root.inlineAttachmentImageHeight(3));
                            }
                        }
                    }
                ]
            }

            Container {
                visible: root.inlineAttachmentVisible(4)
                rightMargin: ui.du(1.0)
                preferredWidth: ui.du(root.inlineAttachmentWidth(4))
                preferredHeight: ui.du(47.2)

                layout: DockLayout {}

                ImageView {
                    visible: root.inlineAttachmentIsImage(4) && root.inlineAttachmentImage(4) !== ""
                    imageSource: root.inlineAttachmentImage(4)
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Fill
                    scalingMethod: ScalingMethod.AspectFit
                }

                ActivityIndicator {
                    visible: root.inlineAttachmentIsImage(4) && root.inlineAttachmentLoading(4) && root.inlineAttachmentImage(4) === ""
                    running: visible
                    horizontalAlignment: HorizontalAlignment.Center
                    verticalAlignment: VerticalAlignment.Center
                }

                Button {
                    visible: root.inlineAttachmentUrl(4) !== "" && (!root.inlineAttachmentIsImage(4) || root.inlineAttachmentFailed(4))
                    text: root.inlineAttachmentButtonText(4)
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Center

                    onClicked: {
                        root.attachmentOpenRequested(root.inlineAttachmentUrl(4));
                    }
                }

                gestureHandlers: [
                    TapHandler {
                        onTapped: {
                            if (root.inlineAttachmentIsImage(4) && root.inlineAttachmentUrl(4) !== "") {
                                root.imagePreviewRequested(root.inlineAttachmentUrl(4), root.inlineAttachmentImageWidth(4), root.inlineAttachmentImageHeight(4));
                            }
                        }
                    }
                ]
            }

            Container {
                visible: root.inlineAttachmentVisible(5)
                rightMargin: ui.du(1.0)
                preferredWidth: ui.du(root.inlineAttachmentWidth(5))
                preferredHeight: ui.du(47.2)

                layout: DockLayout {}

                ImageView {
                    visible: root.inlineAttachmentIsImage(5) && root.inlineAttachmentImage(5) !== ""
                    imageSource: root.inlineAttachmentImage(5)
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Fill
                    scalingMethod: ScalingMethod.AspectFit
                }

                ActivityIndicator {
                    visible: root.inlineAttachmentIsImage(5) && root.inlineAttachmentLoading(5) && root.inlineAttachmentImage(5) === ""
                    running: visible
                    horizontalAlignment: HorizontalAlignment.Center
                    verticalAlignment: VerticalAlignment.Center
                }

                Button {
                    visible: root.inlineAttachmentUrl(5) !== "" && (!root.inlineAttachmentIsImage(5) || root.inlineAttachmentFailed(5))
                    text: root.inlineAttachmentButtonText(5)
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Center

                    onClicked: {
                        root.attachmentOpenRequested(root.inlineAttachmentUrl(5));
                    }
                }

                gestureHandlers: [
                    TapHandler {
                        onTapped: {
                            if (root.inlineAttachmentIsImage(5) && root.inlineAttachmentUrl(5) !== "") {
                                root.imagePreviewRequested(root.inlineAttachmentUrl(5), root.inlineAttachmentImageWidth(5), root.inlineAttachmentImageHeight(5));
                            }
                        }
                    }
                ]
            }
        }
    }

    function attachmentCount() {
        return attachmentModel.size();
    }

    function isOwnMessage() {
        return root.ownMessage;
    }

    function canDeleteMessage() {
        return root.isOwnMessage() || root.deleteAllowed;
    }

    function rebuildAttachmentModel() {
        attachmentModel.clear();
        root.attachmentTotal = 0;
        if (root.attachments === undefined || root.attachments === null) {
            root.appendLegacyAttachment();
            root.attachmentTotal = attachmentModel.size();
            return;
        }

        var count = root.attachments.length !== undefined ? root.attachments.length : 0;
        if (count <= 0 && root.attachments.size !== undefined) {
            count = root.attachments.size();
        }
        for (var i = 0; i < count; ++i) {
            attachmentModel.append(root.attachments[i]);
        }
        if (attachmentModel.size() <= 0) {
            root.appendLegacyAttachment();
        }
        root.attachmentTotal = attachmentModel.size();
    }

    function scheduleEmojiOnlyRebuild() {
        // Fix: avoid depending on a Timer.restart()/.start() method
        // whose existence in this Cascades version isn't confirmed
        // anywhere else in the codebase (no prior usage to copy, and
        // this build already broke twice from assuming an API existed
        // without checking - see the FlowListLayout and Repeater fixes
        // above). Toggling "running" off/on is built on the same
        // interval-restart behavior every Qt-derived Timer documents
        // (changing a running timer's properties resets its elapsed
        // time) and needs nothing beyond the "running"/"interval"
        // properties jumpHighlightTimer above already relies on.
        emojiOnlyRebuildTimer.running = false;
        emojiOnlyRebuildTimer.running = true;
    }

    function rebuildEmojiOnlyModel() {
        // Fix: was clearing/appending into an ArrayDataModel meant for
        // a Repeater that Cascades doesn't have (see the comment on
        // emojiOnlyRow above) - rebuilt to create/destroy real
        // Container controls directly via ComponentDefinition instead.
        //
        // Fix: ROOT CAUSE of the accumulating-icon bug, found via
        // diagnostic logging - emojiOnlyRow.count read back as the
        // literal string "[object Object]", not a number.
        // "while (emojiOnlyRow.count > 0)" was therefore comparing a
        // number against an object; JS coerces that comparison to
        // false unconditionally, so the clear loop never executed on
        // any call, ever, and every rebuild only ever piled more
        // controls on top of whatever was already in emojiOnlyRow.
        // That's the actual mechanism behind every symptom seen so far
        // (1 -> 2 -> 4 icons across reloads, icons on non-emoji
        // messages after scrolling). Rebuilt to track created controls
        // in root.emojiOnlyItems, a plain JS array, instead of relying
        // on Container.count/controlAt() - whether or not those are
        // real, working Cascades APIs no longer matters, since nothing
        // here depends on them anymore.
        //
        // Fix: try/catch here used to wrap the whole clear loop AND
        // implicitly gate everything below it (a caught exception hit
        // "return" before the rebuild half of this function ever ran).
        // On-device: emoji showed correctly on first open, but
        // vanished entirely (not duplicated - genuinely gone) after
        // leaving the chat and scrolling back. That matches this
        // exactly: back-navigation left a stale, half-torn-down
        // control in emojiOnlyItems from the PREVIOUS message this
        // recycled delegate displayed; destroying that one stale
        // control threw "UIObjectPrivate::notifyMessage: Unable to set
        // property" (a real, expected error for a half-destroyed
        // native object - see the original note below), and the catch
        // block's "return" then skipped rebuilding the CURRENT
        // message's icons too, even though nothing was wrong with
        // this message's own data. Each removal is now guarded
        // individually so one bad leftover control can't take out the
        // rebuild for the message that owns this call.
        for (var j = 0; j < root.emojiOnlyItems.length; ++j) {
            try {
                emojiOnlyRow.remove(root.emojiOnlyItems[j]);
                root.emojiOnlyItems[j].destroy();
            } catch (err) {
                // A control Cascades already tore down itself (see
                // above) - nothing to clean up for this one entry,
                // move on to the rest.
            }
        }
        root.emojiOnlyItems = [];

        if (root.emojiSegments === undefined || root.emojiSegments === null) {
            return;
        }

        var count = root.emojiSegments.length !== undefined ? root.emojiSegments.length : 0;
        if (count <= 0 && root.emojiSegments.size !== undefined) {
            count = root.emojiSegments.size();
        }
        var newItems = [];
        for (var i = 0; i < count; ++i) {
            var segment = root.emojiSegments[i];
            var item = emojiOnlyItemDefinition.createObject();
            item.emojiUrl = segment.url;
            emojiOnlyRow.add(item);
            newItems.push(item);
        }
        root.emojiOnlyItems = newItems;
    }

    function appendLegacyAttachment() {
        if (root.attachmentUrl === "") {
            return;
        }
        attachmentModel.append({
                "url": root.attachmentUrl,
                "name": root.attachmentName,
                "isImage": root.attachmentIsImage,
                "image": root.image,
                "imageLoading": root.imageLoading,
                "imageLoadFailed": root.imageLoadFailed,
                "displayWidthDu": root.displayImageWidth(),
                "displayHeightDu": root.displayImageHeight()
            });
    }

    function attachmentsHeight() {
        var count = attachmentModel.size();
        if (count <= 0) {
            return 0;
        }
        return root.maxImageHeightDu + 8.0;
    }

    function inlineAttachmentRows() {
        return root.attachmentTotal > 1 ? 1 : 0;
    }

    function compactMessageHtml() {
        var prefix = "<span style=\"color:#666A70\">" + root.escapeHtml(root.time) + " &lt;</span>" +
                "<span style=\"color:#F2F3F5\">" + root.escapeHtml(root.author) + "</span>" +
            "<span style=\"color:#666A70\">&gt; </span>";
        var body = root.messageHtml;
        if (body === "") {
            body = root.escapeHtml(root.message);
        }
        return prefix + body;
    }

    function escapeHtml(value) {
        return String(value).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/\"/g, "&quot;");
    }

    function inlineAttachmentAt(index) {
        if (root.attachments === undefined || root.attachments === null) {
            return null;
        }
        if (index < 0 || index >= root.attachmentTotal || index >= root.maxInlineAttachments) {
            return null;
        }
        var item = root.attachments[index];
        return item === undefined ? null : item;
    }

    function inlineAttachmentVisible(index) {
        return root.inlineAttachmentAt(index) !== null;
    }

    function inlineAttachmentUrl(index) {
        var item = root.inlineAttachmentAt(index);
        return item === null || item.url === undefined ? "" : item.url;
    }

    function inlineAttachmentName(index) {
        var item = root.inlineAttachmentAt(index);
        return item === null || item.name === undefined ? "" : item.name;
    }

    function inlineAttachmentIsImage(index) {
        var item = root.inlineAttachmentAt(index);
        return item !== null && item.isImage === true;
    }

    function inlineAttachmentImage(index) {
        var item = root.inlineAttachmentAt(index);
        return item === null || item.image === undefined ? "" : item.image;
    }

    function inlineAttachmentImageWidth(index) {
        var item = root.inlineAttachmentAt(index);
        return item === null || item.width === undefined ? 0 : item.width;
    }

    function inlineAttachmentImageHeight(index) {
        var item = root.inlineAttachmentAt(index);
        return item === null || item.height === undefined ? 0 : item.height;
    }

    function inlineAttachmentLoading(index) {
        var item = root.inlineAttachmentAt(index);
        return item !== null && item.imageLoading === true;
    }

    function inlineAttachmentFailed(index) {
        var item = root.inlineAttachmentAt(index);
        return item !== null && item.imageLoadFailed === true;
    }

    function inlineAttachmentWidth(index) {
        var item = root.inlineAttachmentAt(index);
        if (item === null) {
            return 0;
        }
        if (item.isImage !== true) {
            return 34.0;
        }
        return item.displayWidthDu !== undefined && item.displayWidthDu > 0 ? item.displayWidthDu : root.maxImageWidthDu;
    }

    function inlineAttachmentButtonText(index) {
        return (root.inlineAttachmentIsImage(index) ? qsTr("Open image: ") : qsTr("Open attachment: ")) + root.inlineAttachmentName(index);
    }

    function imageAspectRatio() {
        if (root.imageWidth > 0 && root.imageHeight > 0) {
            return root.imageWidth / root.imageHeight;
        }

        return 16.0 / 9.0;
    }

    function displayImageWidth() {
        var ratio = root.imageAspectRatio();
        var width = root.maxImageWidthDu;
        var height = width / ratio;

        if (height > root.maxImageHeightDu) {
            height = root.maxImageHeightDu;
            width = height * ratio;
        }

        if (width < root.minImageWidthDu) {
            width = root.minImageWidthDu;
        }

        return width;
    }

    function displayImageHeight() {
        var ratio = root.imageAspectRatio();
        var height = root.displayImageWidth() / ratio;

        if (height > root.maxImageHeightDu) {
            height = root.maxImageHeightDu;
        }

        return height;
    }
}
