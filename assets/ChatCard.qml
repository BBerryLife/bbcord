import bb.cascades 1.4
import bb.cascades.pickers 1.0
import "media"

Page {
    id: chatPage

    property string channelName: "general"
    property alias title: titleBar.title

    property string replyMessageId: ""
    property string replyAuthor: ""
    property string replyMessage: ""
    property string editingMessageId: ""
    property bool active: true
    property bool olderLoadRequested: false
    property bool olderScrollReady: false
    property bool compactMessageEnabled: false
    property bool hasComposedText: false

    signal backRequested
    signal memberListRequested

    actionBarVisibility: ChromeVisibility.Hidden

    titleBar: TitleBar {
        id: titleBar
        title: chatPage.channelName
        visibility: ChromeVisibility.Visible

        dismissAction: ActionItem {
            imageSource: "asset:///images/icons/accent/caret-left.png"

            onTriggered: {
                chatPage.deactivatePage();
                chatPage.backRequested();
            }
        }

        acceptAction: ActionItem {
            imageSource: "asset:///images/icons/accent/users.png"

            onTriggered: {
                chatPage.memberListRequested();
            }
        }
    }

    Container {
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill

        layout: StackLayout {}

        Container {
            id: chatArea

            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill

            layout: DockLayout {}

            ListView {
                id: messageList
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
                dataModel: chatController.chatDataModel
                stickToEdgePolicy: ListViewStickToEdgePolicy.End
                scrollRole: ScrollRole.Main
                property bool compactMessageEnabled: chatPage.compactMessageEnabled

                listItemComponents: [
                    ListItemComponent {
                        type: ""

                        MessageBubble {
                            horizontalAlignment: HorizontalAlignment.Fill
                            preferredWidth: ListItem.view.width
                            messageId: ListItemData.id
                            authorId: ListItemData.authorId
                            author: ListItemData.author
                            initials: ListItemData.initials
                            avatarSource: ListItemData.avatarSource
                            avatarColor: ListItemData.avatarColor
                            time: ListItemData.time
                            timestampMs: ListItemData.timestampMs
                            message: ListItemData.message
                            messageHtml: ListItemData.messageHtml
                            replyAuthor: ListItemData.replyAuthor
                            replyMessage: ListItemData.replyMessage
                            replyMessageHtml: ListItemData.replyMessageHtml
                            image: ListItemData.image
                            imageLoading: ListItemData.imageLoading
                            imageLoadFailed: ListItemData.imageLoadFailed
                            imageWidth: ListItemData.imageWidth
                            imageHeight: ListItemData.imageHeight
                            attachmentUrl: ListItemData.attachmentUrl
                            attachmentName: ListItemData.attachmentName
                            attachmentIsImage: ListItemData.attachmentIsImage
                            attachments: ListItemData.attachments
                            isGroupStart: ListItemData.isGroupStart
                            isGroupEnd: ListItemData.isGroupEnd
                            showAvatar: ListItemData.showAvatar
                            showUsername: ListItemData.showUsername
                            showTimestamp: ListItemData.showTimestamp
                            compactMessage: ListItem.view.compactMessageEnabled
                            pending: ListItemData.pending
                            failed: ListItemData.failed
                            edited: ListItemData.edited
                            ownMessage: ListItemData.ownMessage === true
                            deleteAllowed: ListItemData.deleteAllowed === true

                            onEditRequested: {
                                ListItem.view.startEdit(messageId, message);
                            }

                            onDeleteRequested: {
                                ListItem.view.deleteMessage(messageId);
                            }

                            onReplyRequested: {
                                ListItem.view.setReply(messageId, author, message);
                            }

                            onCopyRequested: {
                                ListItem.view.copyMessage(text);
                            }

                            onAttachmentOpenRequested: {
                                ListItem.view.openAttachment(url);
                            }

                            onImagePreviewRequested: {
                                ListItem.view.previewImage(url, imageWidth, imageHeight);
                            }

                            onAttachmentImageLoadRequested: {
                                ListItem.view.loadAttachmentImage(url);
                            }
                        }
                    }
                ]

                function itemType(data, indexPath) {
                    return "";
                }

                function startEdit(messageId, message) {
                    chatPage.startEdit(messageId, message);
                }

                function setReply(messageId, author, message) {
                    chatPage.setReply(messageId, author, message);
                }

                function loadAttachmentImage(url) {
                    chatController.requestCachedImage(url);
                }

                function deleteMessage(messageId) {
                    chatController.deleteMessage(messageId);
                }

                function copyMessage(text) {
                    chatController.copyText(text);
                }

                function openAttachment(url) {
                    chatController.openAttachment(url);
                }

                function previewImage(url, imageWidth, imageHeight) {
                    chatPage.openImagePreview(url, imageWidth, imageHeight);
                }

                onDataModelChanged: {
                    chatPage.scrollToBottom();
                }

                attachedObjects: [
                    ListScrollStateHandler {
                        onAtBeginningChanged: {
                            if (!atBeginning) {
                                chatPage.olderScrollReady = true;
                            } else if (chatPage.olderScrollReady) {
                                chatPage.requestOlderFromScroll();
                            }
                        }
                    }
                ]
            }

            Container {
                visible: appStore.chatLoadingBefore
                preferredHeight: ui.du(6.0)
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Top
                background: Color.create("#8518191c")
                
                Container {
                    preferredHeight: ui.du(6.0)
                    verticalAlignment: VerticalAlignment.Center
                    horizontalAlignment: HorizontalAlignment.Center
                    
                    layout: StackLayout {
                        orientation: LayoutOrientation.LeftToRight
                    }

                    ActivityIndicator {
                        running: appStore.chatLoadingBefore
                        horizontalAlignment: HorizontalAlignment.Center
                        verticalAlignment: VerticalAlignment.Center
                    }
                    
                    Label {
                        text: qsTr("Loading more messages...")
                        textStyle.fontSize: FontSize.XSmall
                        opacity: 0.6
                        horizontalAlignment: HorizontalAlignment.Center
                        verticalAlignment: VerticalAlignment.Center
                    }
                }

                
            }
        }

        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            background: Color.create("#18191C")

            layout: StackLayout {
            }

            topPadding: ui.du(1.5)
            bottomPadding: ui.du(1.5)
            leftPadding: ui.du(1.5)
            rightPadding: ui.du(1.5)

            Container {
                visible: chatPage.replyAuthor !== ""
                horizontalAlignment: HorizontalAlignment.Fill
                bottomMargin: ui.du(1.0)

                layout: StackLayout {
                    orientation: LayoutOrientation.LeftToRight
                }

                ImageButton {
                    preferredWidth: ui.du(3.0)
                    preferredHeight: ui.du(3.0)
                    verticalAlignment: VerticalAlignment.Center
                    defaultImageSource: "asset:///images/icons/x.png"

                    onClicked: {
                        chatPage.clearReply();
                    }
                    pressedImageSource: "asset:///images/icons/x-hold.png"
                    disabledImageSource: "asset:///images/icons/x-disabled.png"
                    maxWidth: ui.du(3.0)
                    maxHeight: ui.du(3.0)
                    minWidth: ui.du(3.0)
                    minHeight: ui.du(3.0)
                }

                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Center

                    Label {
                        text: qsTr("Replying to ") + chatPage.replyAuthor
                        textStyle.fontSize: FontSize.XXSmall
                        textStyle.fontWeight: FontWeight.Bold
                        textStyle.color: Color.create("#F2F3F5")
                    }

                    Label {
                        text: chatPage.replyMessage
                        topMargin: ui.du(-0.3)
                        textStyle.fontSize: FontSize.XXSmall
                        textStyle.color: Color.create("#B5BAC1")
                    }
                }
            }

            Container {
                visible: chatController.hasPendingAttachment
                horizontalAlignment: HorizontalAlignment.Fill
                bottomMargin: ui.du(1.0)

                layout: StackLayout {
                }

                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    bottomMargin: ui.du(0.8)

                    layout: StackLayout {
                        orientation: LayoutOrientation.LeftToRight
                    }

                    Label {
                        text: qsTr("Attachments")
                        horizontalAlignment: HorizontalAlignment.Fill
                        textStyle.fontSize: FontSize.XXSmall
                        textStyle.fontWeight: FontWeight.Bold
                        textStyle.color: Color.create("#F2F3F5")
                        rightMargin: ui.du(0.0)
                    }

                    ImageButton {
                        preferredWidth: ui.du(3.0)
                        preferredHeight: ui.du(3.0)
                        defaultImageSource: "asset:///images/icons/x.png"
                        pressedImageSource: "asset:///images/icons/x-hold.png"
                        disabledImageSource: "asset:///images/icons/x-disabled.png"

                        onClicked: {
                            chatController.clearPendingAttachment();
                        }
                        leftMargin: ui.du(0.5)
                    }
                }

                ListView {
                    id: pendingAttachmentList
                    horizontalAlignment: HorizontalAlignment.Fill
                    preferredHeight: ui.du(15.0)
                    dataModel: chatController.pendingAttachmentsModel

                    layout: StackListLayout {
                        orientation: LayoutOrientation.LeftToRight
                    }

                    listItemComponents: [
                        ListItemComponent {
                            type: ""

                            Container {
                                id: pendingAttachmentItem
                                preferredWidth: ui.du(18.0)
                                preferredHeight: ui.du(14.0)
                                rightMargin: ui.du(1.0)
                                background: Color.create("#2B2D31")

                                layout: DockLayout {
                                }

                                Container {
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    verticalAlignment: VerticalAlignment.Fill
                                    topPadding: ui.du(1.0)
                                    bottomPadding: ui.du(1.0)
                                    leftPadding: ui.du(1.0)
                                    rightPadding: ui.du(1.0)

                                    layout: StackLayout {
                                    }

                                    ImageView {
                                        visible: ListItemData.isImage
                                        imageSource: ListItemData.preview
                                        horizontalAlignment: HorizontalAlignment.Fill
                                        preferredHeight: ui.du(8.0)
                                        scalingMethod: ScalingMethod.AspectFit
                                    }

                                    Label {
                                        visible: ! ListItemData.isImage
                                        text: qsTr("File")
                                        horizontalAlignment: HorizontalAlignment.Center
                                        preferredHeight: ui.du(8.0)
                                        verticalAlignment: VerticalAlignment.Center
                                        textStyle.fontSize: FontSize.Small
                                        textStyle.color: Color.create("#F2F3F5")
                                    }

                                    Label {
                                        text: ListItemData.name
                                        multiline: false
                                        topMargin: ui.du(0.5)
                                        textStyle.fontSize: FontSize.XXSmall
                                        textStyle.color: Color.create("#B5BAC1")
                                    }
                                }

                                ImageButton {
                                    horizontalAlignment: HorizontalAlignment.Right
                                    verticalAlignment: VerticalAlignment.Top
                                    preferredWidth: ui.du(5.0)
                                    preferredHeight: ui.du(5.0)
                                    defaultImageSource: "asset:///images/icons/x.png"
                                    pressedImageSource: "asset:///images/icons/x-hold.png"
                                    disabledImageSource: "asset:///images/icons/x-disabled.png"

                                    onClicked: {
                                        pendingAttachmentItem.ListItem.view.removePendingAttachment(pendingAttachmentItem.ListItem.indexPath[0]);
                                    }
                                }
                            }
                        }
                    ]

                    function itemType(data, indexPath) {
                        return "";
                    }

                    function removePendingAttachment(index) {
                        chatController.removePendingAttachment(index);
                    }
                }
            }

            Label {
                visible: chatController.pendingAttachmentError !== ""
                text: chatController.pendingAttachmentError
                bottomMargin: ui.du(1.0)
                textStyle.fontSize: FontSize.XXSmall
                textStyle.color: Color.create("#ED4245")
            }

            Container {
                horizontalAlignment: HorizontalAlignment.Fill

                layout: StackLayout {
                    orientation: LayoutOrientation.LeftToRight
                }

                ImageButton {
                    verticalAlignment: VerticalAlignment.Center
                    preferredWidth: ui.du(7.0)
                    preferredHeight: ui.du(7.0)

                    onClicked: {
                        attachmentPicker.open();
                    }
                    defaultImageSource: "asset:///images/icons/paperclip.png"
                    pressedImageSource: "asset:///images/icons/paperclip-hold.png"
                    disabledImageSource: "asset:///images/icons/paperclip-disabled.png"
                }

                TextArea {
                    id: inputMessage

                    hintText: qsTr("Message ") + chatPage.channelName
                    inputMode: TextAreaInputMode.Text
                    textFormat: TextFormat.Plain

                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Center

                    input {
                        submitKey: SubmitKey.Send

                        onSubmitted: {
                            chatPage.sendCurrentMessage();
                        }
                    }

                    // Cascades không luôn re-evaluate property binding
                    // của sendButton.enabled ngay khi gõ (chỉ refresh khi
                    // trang được tạo/kích hoạt lại). Cập nhật tường minh
                    // qua signal để nút Send bật/tắt đúng theo từng ký tự.
                    onTextChanging: {
                        chatPage.hasComposedText = text.length > 0;
                    }
                }

                ImageButton {
                    id: sendButton
                    verticalAlignment: VerticalAlignment.Center
                    preferredWidth: ui.du(7.0)
                    preferredHeight: ui.du(7.0)

                    enabled: chatPage.hasComposedText || chatController.hasPendingAttachment
                    opacity: enabled ? 1.0 : 0.4

                    defaultImageSource: "asset:///images/icons/send.png"
                    pressedImageSource: "asset:///images/icons/send.png"
                    disabledImageSource: "asset:///images/icons/send.png"

                    onClicked: {
                        chatPage.sendCurrentMessage();
                    }
                }
            }

            Container {
                visible: chatPage.editingMessageId !== ""
                horizontalAlignment: HorizontalAlignment.Fill
                topMargin: ui.du(0.8)

                layout: StackLayout {
                    orientation: LayoutOrientation.LeftToRight
                }

                Label {
                    text: qsTr("Editing message")
                    horizontalAlignment: HorizontalAlignment.Fill
                    textStyle.fontSize: FontSize.XXSmall
                    textStyle.color: Color.create("#B5BAC1")
                    verticalAlignment: VerticalAlignment.Center
                }

                ImageButton {
                    preferredWidth: ui.du(3.0)
                    preferredHeight: ui.du(3.0)
                    defaultImageSource: "asset:///images/icons/x.png"
                    pressedImageSource: "asset:///images/icons/x-hold.png"
                    disabledImageSource: "asset:///images/icons/x-disabled.png"

                    onClicked: {
                        chatPage.cancelEdit();
                    }
                    verticalAlignment: VerticalAlignment.Center
                }
            }
        }
    }

    attachedObjects: [
        FilePicker {
            id: attachmentPicker
            title: qsTr("Attach files")
            mode: FilePickerMode.PickerMultiple
            type: FileType.Other
            viewMode: FilePickerViewMode.Default

            onFileSelected: {
                chatController.addPendingAttachments(selectedFiles);
            }
        },
        ImagePreview {
            id: imagePreviewSheet
        }
    ]

    function setReply(messageId, author, message) {
        replyMessageId = messageId;
        replyAuthor = author;
        replyMessage = message;
        cancelEdit();
        inputMessage.requestFocus();
    }

    function clearReply() {
        replyMessageId = "";
        replyAuthor = "";
        replyMessage = "";
    }

    function startEdit(messageId, message) {
        editingMessageId = messageId;
        inputMessage.text = message;
        chatPage.hasComposedText = inputMessage.text.length > 0;
        clearReply();
        inputMessage.requestFocus();
    }

    function cancelEdit() {
        editingMessageId = "";
    }

    function sendCurrentMessage() {
        var text = inputMessage.text.replace(/^\s+|\s+$/g, "");

        if (text.length == 0 && !chatController.hasPendingAttachment) {
            return;
        }

        if (editingMessageId !== "") {
            if (chatController.hasPendingAttachment) {
                chatController.clearPendingAttachment();
            }
            chatController.editMessage(editingMessageId, text);
            cancelEdit();
        } else {
            chatController.sendMessage(text, replyMessageId, replyAuthor, replyMessage);
            clearReply();
        }

        inputMessage.text = "";
        chatPage.hasComposedText = false;
    }

    function requestOlderFromScroll() {
        if (!active) {
            return;
        }

        if (olderLoadRequested || chatController.isLoadingBefore() || !chatController.hasMoreBefore()) {
            return;
        }

        olderLoadRequested = true;
        chatController.requestOlderMessages();
    }

    function scrollToBottom() {
        var count = chatController.chatDataModel.size();
        if (count <= 0) {
            return;
        }

        messageList.scrollToItem([ count - 1 ], ScrollAnimation.Default);
    }

    function clearForChannelSwitch() {
        replyMessageId = "";
        replyAuthor = "";
        replyMessage = "";
        editingMessageId = "";
        olderLoadRequested = false;
        olderScrollReady = false;
    }

    function deactivatePage() {
        active = false;
    }

    function openImagePreview(url, imageWidth, imageHeight) {
        imagePreviewSheet.openUrl(url, imageWidth, imageHeight);
    }

    function reactivatePage() {
        active = true;
    }

    onCreationCompleted: {
        active = true;
        channelName = chatController.currentChannelName;
        chatController.currentChannelChanged.connect(function () {
            chatPage.channelName = chatController.currentChannelName;
            chatPage.clearForChannelSwitch();
        });
        appStore.chatMessagesPrepended.connect(function (channelId, messages) {
            if (channelId == chatController.currentChannelId) {
                chatPage.olderLoadRequested = false;
            }
        });
        appStore.chatMessagesReset.connect(function (channelId, messages) {
            if (channelId == chatController.currentChannelId) {
                chatPage.olderLoadRequested = false;
                chatPage.scrollToBottom();
            }
        });
        appStore.chatMessagesBatched.connect(function (channelId, messages) {
            if (channelId == chatController.currentChannelId) {
                chatPage.scrollToBottom();
            }
        });
        appStore.chatMessageAdded.connect(function (channelId, message) {
            if (channelId == chatController.currentChannelId) {
                chatPage.scrollToBottom();
            }
        });
    }
}
