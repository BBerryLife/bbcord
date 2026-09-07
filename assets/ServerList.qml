import bb.cascades 1.4
import bb.system 1.2

Container {
    id: serverList

    property string serverName: "lmao server"
    property string serverId: ""

    signal channelSelected(string channelId, string guildId, string channelName)
    // Forum/Media channels have no messages of their own to open via
    // ChatCard - each forum "post" IS a thread (parentId points back to
    // this channel). Fires a separate signal so MainPage.qml opens
    // ThreadList.qml (showing the post/thread list) instead of trying
    // ChatCard like a regular text channel.
    signal forumChannelSelected(string channelId, string guildId, string channelName)

    horizontalAlignment: HorizontalAlignment.Fill
    verticalAlignment: VerticalAlignment.Fill

    layout: StackLayout {}

    Container {
        preferredHeight: ui.du(10)
        leftPadding: ui.du(2)
        rightPadding: ui.du(2)
        background: Color.create("#111111")
        horizontalAlignment: HorizontalAlignment.Fill

        layout: StackLayout {
            orientation: LayoutOrientation.LeftToRight
        }

        Label {
            text: serverList.serverName
            verticalAlignment: VerticalAlignment.Center
            textStyle.fontSize: FontSize.Medium
            textStyle.fontWeight: FontWeight.Normal
            horizontalAlignment: HorizontalAlignment.Fill
        }
    }

    ListView {
        dataModel: serverListController.channelDataModel
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill
        scrollRole: ScrollRole.Main

        onTriggered: {
            var item = serverListController.channelDataModel.data(indexPath);

            if (item.type == "channel") {
                if (item.isForumLike == true) {
                    serverList.forumChannelSelected(item.id, serverList.serverId, item.name);
                    return;
                }
                serverList.channelSelected(item.id, serverList.serverId, item.name);
            }
        }

        listItemComponents: [
            ListItemComponent {
                type: "loading"

                Container {
                    preferredHeight: ui.du(7.0)
                    preferredWidth: ui.du(200)
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Fill
                    layout: DockLayout {}

                    ActivityIndicator {
                        running: true
                        horizontalAlignment: HorizontalAlignment.Center
                        verticalAlignment: VerticalAlignment.Center
                    }
                }
            },
            ListItemComponent {
                type: "category"

                Container {
                    preferredHeight: 2.0
                    preferredWidth: ui.du(200)
                    horizontalAlignment: HorizontalAlignment.Fill
                    leftPadding: ui.du(2)
                    rightPadding: ui.du(2)

                    layout: DockLayout {}

                    topPadding: ui.du(2.0)
                    Label {
                        text: ListItemData.name
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment: VerticalAlignment.Center
                        opacity: 0.45
                        textStyle.fontSize: FontSize.XSmall
                    }
                }
            },
            ListItemComponent {
                type: "channel"

                Container {
                    preferredHeight: ui.du(7.0)
                    preferredWidth: ui.du(200)
                    horizontalAlignment: HorizontalAlignment.Fill
                    leftPadding: ui.du(2)
                    rightPadding: ui.du(2)

                    layout: DockLayout {}

                    Container {
                        layout: StackLayout {
                            orientation: LayoutOrientation.LeftToRight
                        }

                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment: VerticalAlignment.Center

                        ImageView {
                            imageSource: ListItemData.icon

                            preferredWidth: ui.du(4)
                            preferredHeight: ui.du(4)

                            verticalAlignment: VerticalAlignment.Bottom
                            opacity: ListItemData.unread ? 1.0 : 0.45

                            scalingMethod: ScalingMethod.AspectFit
                        }

                        Label {
                            text: ListItemData.name
                            horizontalAlignment: HorizontalAlignment.Fill
                            textStyle.fontSize: FontSize.Medium
                            opacity: (ListItemData.unread || ListItemData.mentionCount > 0) ? 1.0 : 0.45
                        }

                        Container {
                            visible: ListItemData.mentionCount > 0
                            preferredWidth: ui.du(4)
                            preferredHeight: ui.du(4)
                            verticalAlignment: VerticalAlignment.Center
                            background: Color.create("#DA373C")
                            layout: DockLayout {}

                            Label {
                                text: ListItemData.mentionCount > 99 ? "99+" : ListItemData.mentionCount
                                horizontalAlignment: HorizontalAlignment.Center
                                verticalAlignment: VerticalAlignment.Center
                                textStyle.fontSize: FontSize.XXSmall
                                textStyle.color: Color.White
                            }
                        }
                    }
                }
            }
        ]

        function itemType(data, indexPath) {
            return data.type;
        }

    }

    attachedObjects: [
        SystemToast {
            id: unsupportedChannelToast
        }
    ]
}
