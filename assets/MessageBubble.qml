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
    }

    onAttachmentsChanged: {
        root.rebuildAttachmentModel();
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
                    textFormat: TextFormat.Html
                    opacity: 0.85
                    textStyle.fontSize: FontSize.XXSmall
                    textStyle.color: Color.create("#B5BAC1")
                }
            }
        }

        Label {
            text: root.compactMessage ? root.compactMessageHtml() : root.messageHtml
            horizontalAlignment: HorizontalAlignment.Fill
            topMargin: root.compactMessage ? ui.du(0.0) : (root.isGroupStart ? ui.du(-0.6) : ui.du(-0.2))
            multiline: true
            textFormat: TextFormat.Html
            textStyle.fontSize: FontSize.XSmall
            textStyle.color: Color.create("#DCDDDE")
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

            ImageView {
                visible: root.image !== "" || root.attachmentUrl !== ""
                imageSource: root.image !== "" ? root.image : root.attachmentUrl
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
                visible: root.imageLoading && root.image === "" && root.attachmentUrl === ""
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
