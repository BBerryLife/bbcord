import bb.cascades 1.4

Page {
    id: mainPage

    property variant navigationPane: null
    property variant activeContent: null
    property string activeContentType: ""
    property string activeServerId: ""

    // Tracks the chat page currently on top so main.qml's onGuildChannelsChanged()
    // can re-resolve a name that came back empty from a Hub-invoked open. Cleared on
    // backRequested; not used for the normal open path (name already known).
    property string activeChatChannelId: ""
    property string activeChatChannelName: ""
    property variant activeChatPage: null

    function updateActiveChatChannelName(newName) {
        activeChatChannelName = newName;
        if (activeChatPage) {
            // ChatCard.qml's titleBar.title is bound to channelName (title: chatPage.channelName), so this alone updates it.
            activeChatPage.channelName = newName;
        }
    }

    Container {
        layout: StackLayout {
            orientation: LayoutOrientation.LeftToRight
        }

        Container {
            preferredWidth: ui.du(12)
            verticalAlignment: VerticalAlignment.Fill
            background: Color.create("#111111")

            ListView {
                id: guildListView
                dataModel: mainPageController.serverDataModel
                verticalAlignment: VerticalAlignment.Fill

                function loadVisibleGuildIcon(guildId) {
                    mainPageController.loadGuildIcon(guildId);
                }

                onTriggered: {
                    var item = mainPageController.serverDataModel.data(indexPath);

                    if (item.type == "dm") {
                        mainPage.loadDmList();
                        mainPageController.selectHome();
                    } else if (item.type == "folder") {
                        mainPageController.toggleGuildFolder(item.id);
                    } else if (item.type == "server") {
                        mainPageController.selectGuild(item.id);
                        mainPage.loadServerList(item.id, item.name);
                    }
                }

                listItemComponents: [
                    ListItemComponent {
                        type: ""

                        Container {
                            preferredWidth: ui.du(10)
                            preferredHeight: ui.du(10)
                            property string lastIconRequestId: ""
                            property bool selectedItem: ListItemData.active == true
                            property bool selectedServer: selectedItem && ListItemData.type == "server"
                            property bool folderItem: ListItemData.type == "folder"
                            property variant folderGuilds: folderItem ? ListItemData.guilds : []

                            topMargin: ui.du(1)
                            bottomMargin: ui.du(1)

                            background: selectedItem ? Color.create("#5865F2") : (folderItem ? Color.create("#2B2D31") : Color.create("#333333"))

                            layout: DockLayout {}

                            function requestIcon() {
                                if (lastIconRequestId == ListItemData.id) {
                                    return;
                                }
                                if (ListItemData.type == "server" && ListItemData.icon === "" && ListItemData.iconHash !== "") {
                                    lastIconRequestId = ListItemData.id;
                                    ListItem.view.loadVisibleGuildIcon(ListItemData.id);
                                } else if (ListItemData.type == "folder") {
                                    var guilds = ListItemData.guilds;
                                    if (!guilds) {
                                        return;
                                    }
                                    for (var i = 0; i < guilds.length && i < 4; ++i) {
                                        var guild = guilds[i];
                                        if (guild.icon === "" && guild.iconHash !== "") {
                                            ListItem.view.loadVisibleGuildIcon(guild.id);
                                        }
                                    }
                                }
                            }

                            onCreationCompleted: {
                                requestIcon();
                            }

                            ListItem.onDataChanged: {
                                if (lastIconRequestId != ListItemData.id) {
                                    requestIcon();
                                }
                            }

                            Container {
                                visible: ListItemData.type != "folder" && ListItemData.icon !== ""
                                horizontalAlignment: HorizontalAlignment.Fill
                                verticalAlignment: VerticalAlignment.Fill
                                topPadding: selectedServer ? ui.du(0.5) : 0
                                bottomPadding: selectedServer ? ui.du(0.5) : 0
                                leftPadding: selectedServer ? ui.du(0.5) : 0
                                rightPadding: selectedServer ? ui.du(0.5) : 0

                                layout: DockLayout {}

                                ImageView {
                                    imageSource: ListItemData.icon
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    verticalAlignment: VerticalAlignment.Fill
                                    scalingMethod: ScalingMethod.AspectFit
                                }
                            }

                            Label {
                                text: ListItemData.initials

                                visible: ListItemData.type != "folder" && ListItemData.icon === ""

                                horizontalAlignment: HorizontalAlignment.Center
                                verticalAlignment: VerticalAlignment.Center

                                textStyle.fontWeight: FontWeight.Bold
                                textStyle.fontSize: FontSize.Large
                            }

                            Container {
                                visible: ListItemData.type == "folder"
                                horizontalAlignment: HorizontalAlignment.Fill
                                verticalAlignment: VerticalAlignment.Fill
                                topPadding: ui.du(0.5)
                                bottomPadding: ui.du(0.5)
                                leftPadding: ui.du(0.5)
                                rightPadding: ui.du(0.5)

                                layout: DockLayout {}

                                Container {
                                    visible: ListItemData.expanded == true
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    verticalAlignment: VerticalAlignment.Fill

                                    layout: DockLayout {}

                                    ImageView {
                                        imageSource: "asset:///images/icons/ic_folder_color.png"
                                        horizontalAlignment: HorizontalAlignment.Fill
                                        verticalAlignment: VerticalAlignment.Fill
                                        scalingMethod: ScalingMethod.AspectFit
                                        preferredWidth: ui.du(8)
                                        preferredHeight: ui.du(8)
                                    }
                                }

                                Container {
                                    visible: ListItemData.expanded != true
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    verticalAlignment: VerticalAlignment.Fill

                                    layout: StackLayout {
                                        orientation: LayoutOrientation.TopToBottom
                                    }

                                    Container {
                                        horizontalAlignment: HorizontalAlignment.Fill
                                        verticalAlignment: VerticalAlignment.Fill
                                        layout: StackLayout {
                                            orientation: LayoutOrientation.LeftToRight
                                        }

                                        Container {
                                            preferredWidth: ui.du(4.25)
                                            preferredHeight: ui.du(4.25)
                                            rightMargin: ui.du(0.25)
                                            background: Color.create("#5865F2")
                                            layout: DockLayout {}

                                            ImageView {
                                                imageSource: folderGuilds.length > 0 ? folderGuilds[0].icon : ""
                                                visible: folderGuilds.length > 0 && folderGuilds[0].icon !== ""
                                                horizontalAlignment: HorizontalAlignment.Fill
                                                verticalAlignment: VerticalAlignment.Fill
                                                scalingMethod: ScalingMethod.AspectFit
                                            }

                                            Label {
                                                text: folderGuilds.length > 0 ? folderGuilds[0].initials : ""
                                                visible: folderGuilds.length > 0 && folderGuilds[0].icon === ""
                                                horizontalAlignment: HorizontalAlignment.Center
                                                verticalAlignment: VerticalAlignment.Center
                                                textStyle.fontWeight: FontWeight.Bold
                                                textStyle.fontSize: FontSize.XXSmall
                                            }
                                        }

                                        Container {
                                            preferredWidth: ui.du(4.25)
                                            preferredHeight: ui.du(4.25)
                                            leftMargin: ui.du(0.25)
                                            background: Color.create("#5865F2")
                                            layout: DockLayout {}

                                            ImageView {
                                                imageSource: folderGuilds.length > 1 ? folderGuilds[1].icon : ""
                                                visible: folderGuilds.length > 1 && folderGuilds[1].icon !== ""
                                                horizontalAlignment: HorizontalAlignment.Fill
                                                verticalAlignment: VerticalAlignment.Fill
                                                scalingMethod: ScalingMethod.AspectFit
                                            }

                                            Label {
                                                text: folderGuilds.length > 1 ? folderGuilds[1].initials : ""
                                                visible: folderGuilds.length > 1 && folderGuilds[1].icon === ""
                                                horizontalAlignment: HorizontalAlignment.Center
                                                verticalAlignment: VerticalAlignment.Center
                                                textStyle.fontWeight: FontWeight.Bold
                                                textStyle.fontSize: FontSize.XXSmall
                                            }
                                        }
                                    }

                                    Container {
                                        horizontalAlignment: HorizontalAlignment.Fill
                                        verticalAlignment: VerticalAlignment.Fill
                                        topMargin: ui.du(0.5)
                                        layout: StackLayout {
                                            orientation: LayoutOrientation.LeftToRight
                                        }

                                        Container {
                                            preferredWidth: ui.du(4.25)
                                            preferredHeight: ui.du(4.25)
                                            rightMargin: ui.du(0.25)
                                            background: Color.create("#5865F2")
                                            layout: DockLayout {}

                                            ImageView {
                                                imageSource: folderGuilds.length > 2 ? folderGuilds[2].icon : ""
                                                visible: folderGuilds.length > 2 && folderGuilds[2].icon !== ""
                                                horizontalAlignment: HorizontalAlignment.Fill
                                                verticalAlignment: VerticalAlignment.Fill
                                                scalingMethod: ScalingMethod.AspectFit
                                            }

                                            Label {
                                                text: folderGuilds.length > 2 ? folderGuilds[2].initials : ""
                                                visible: folderGuilds.length > 2 && folderGuilds[2].icon === ""
                                                horizontalAlignment: HorizontalAlignment.Center
                                                verticalAlignment: VerticalAlignment.Center
                                                textStyle.fontWeight: FontWeight.Bold
                                                textStyle.fontSize: FontSize.XXSmall
                                            }
                                        }

                                        Container {
                                            preferredWidth: ui.du(4.25)
                                            preferredHeight: ui.du(4.25)
                                            leftMargin: ui.du(0.25)
                                            background: Color.create("#5865F2")
                                            layout: DockLayout {}

                                            ImageView {
                                                imageSource: folderGuilds.length > 3 ? folderGuilds[3].icon : ""
                                                visible: folderGuilds.length > 3 && folderGuilds[3].icon !== ""
                                                horizontalAlignment: HorizontalAlignment.Fill
                                                verticalAlignment: VerticalAlignment.Fill
                                                scalingMethod: ScalingMethod.AspectFit
                                            }

                                            Label {
                                                text: folderGuilds.length > 3 ? folderGuilds[3].initials : ""
                                                visible: folderGuilds.length > 3 && folderGuilds[3].icon === ""
                                                horizontalAlignment: HorizontalAlignment.Center
                                                verticalAlignment: VerticalAlignment.Center
                                                textStyle.fontWeight: FontWeight.Bold
                                                textStyle.fontSize: FontSize.XXSmall
                                            }
                                        }
                                    }
                                }
                            }

                            Container {
                                preferredWidth: ui.du(1.0)
                                preferredHeight: ui.du(4.0)
                                horizontalAlignment: HorizontalAlignment.Left
                                verticalAlignment: VerticalAlignment.Center
                                background: Color.create("#FFFFFF")
                                // White bar for any new message (unread or mentionCount > 0). The red badge below
                                // is layered on top of it for pings only.
                                visible: (ListItemData.type == "server" || ListItemData.type == "folder") && (ListItemData.unread == true || ListItemData.mentionCount > 0)
                            }

                            Container {
                                preferredWidth: ui.du(3)
                                preferredHeight: ui.du(3)
                                horizontalAlignment: HorizontalAlignment.Right
                                verticalAlignment: VerticalAlignment.Top
                                background: Color.create("#ED4245")
                                // Pings also get this red count badge, together with the white bar; both clear when mentionCount drops to 0.
                                visible: ListItemData.mentionCount > 0

                                layout: DockLayout {}

                                Label {
                                    text: ListItemData.mentionCount > 99 ? "99+" : ListItemData.mentionCount
                                    horizontalAlignment: HorizontalAlignment.Center
                                    verticalAlignment: VerticalAlignment.Center
                                    textStyle.color: Color.White
                                    textStyle.fontWeight: FontWeight.Bold
                                    textStyle.fontSize: FontSize.XXSmall
                                }
                            }

                        }
                    }
                ]

                function itemType(data, indexPath) {
                    return "";
                }

                leftPadding: ui.du(1.0)
                topPadding: ui.du(1.0)
            }
        }

        Container {
            id: contentHost

            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill

            layout: StackLayout {}
        }
    }

    function openChat(channelId, guildId, channelName) {
        var page = chatCardDefinition.createObject();

        if (page) {
            chatController.openChannel(channelId, guildId, channelName);
            page.channelName = channelName;
            // Do not assign page.title here: ChatCard.qml's titleBar binds
            // title: chatPage.channelName via the "title" alias, and assigning to a bound
            // property permanently breaks the binding, freezing the title bar.
            // page.channelName above is enough.
            page.compactMessageEnabled = settingsController.compactMessageEnabled;
            // Lets main.qml find this page later if channelName arrived empty (Hub invoke).
            mainPage.activeChatPage = page;
            mainPage.activeChatChannelId = channelId;
            mainPage.activeChatChannelName = channelName;
            settingsController.compactMessageEnabledChanged.connect(function (enabled) {
                page.compactMessageEnabled = enabled;
            });
            page.backRequested.connect(function () {
                // Tell the C++ side the user left the channel (ChatController::closeChannel()/
                // AppStore::clearChannelSelection()), or later messages count as read live.
                chatController.closeChannel();
                if (mainPage.activeChatPage === page) {
                    mainPage.activeChatPage = null;
                    mainPage.activeChatChannelId = "";
                    mainPage.activeChatChannelName = "";
                }
                if (mainPage.navigationPane) {
                    mainPage.navigationPane.pop();
                }
            });
            page.memberListRequested.connect(function () {
                var memberPage = channelMemberListDefinition.createObject();

                if (memberPage) {
                    // Use the live mainPage.activeChatChannelName: a Hub-opened chat's name may have
                    // self-healed since openChat().
                    var resolvedChannelName = mainPage.activeChatChannelName !== ""
                        ? mainPage.activeChatChannelName : channelName;
                    memberPage.channelId = channelId;
                    memberPage.guildId = guildId;
                    memberPage.channelName = resolvedChannelName;
                    memberPage.title = "Members #" + resolvedChannelName;
                    memberPage.backRequested.connect(function () {
                        if (mainPage.navigationPane) {
                            mainPage.navigationPane.pop();
                        }
                    });
                    if (mainPage.navigationPane) {
                        mainPage.navigationPane.push(memberPage);
                    }
                    // createObject() runs ChannelMemberList.qml's onCreationCompleted() before the
                    // properties are assigned (channelId/guildId still empty), so its
                    // requestMemberList() returns early. Call it again explicitly here.
                    memberPage.requestMemberListNow();
                } else {
                    console.log("Could not create ChannelMemberList.qml");
                }
            });
            if (mainPage.navigationPane) {
                mainPage.navigationPane.push(page);
            }
        } else {
            console.log("Could not create ChatCard.qml");
        }
    }

    function replaceContent(content) {
        if (activeContent) {
            contentHost.remove(activeContent);
            activeContent.destroy();
        }

        activeContent = content;
        contentHost.add(activeContent);
    }

    function loadDmList() {
        mainPageController.selectHome();

        if (activeContentType == "dm") {
            return;
        }

        var page = dmListDefinition.createObject();

        if (page) {
            page.dmSelected.connect(function (channelId, channelName) {
                mainPage.openChat(channelId, "", channelName);
            });
            replaceContent(page);
            activeContentType = "dm";
            activeServerId = "";
        } else {
            console.log("Could not create DmList.qml");
        }
    }

    // Shared by openChat()'s "Threads" action and direct taps on forum/media channels
    // in ServerList (they have no messages; each post is a thread).
    function openThreadList(channelId, guildId, channelName) {
        var threadPage = threadListDefinition.createObject();

        if (threadPage) {
            threadPage.channelId = channelId;
            threadPage.guildId = guildId;
            threadPage.channelName = channelName;
            // Sets the title again after push: an earlier assignment via the "title" alias
            // overrode ThreadList.qml's own binding. Keep wording in sync with ThreadList.qml.
            threadPage.title = qsTr("Forums #") + channelName;
            threadPage.backRequested.connect(function () {
                threadPage.cleanup();
                if (mainPage.navigationPane) {
                    mainPage.navigationPane.pop();
                }
            });
            threadPage.threadSelected.connect(function (threadId, threadName) {
                threadPage.cleanup();
                if (mainPage.navigationPane) {
                    mainPage.navigationPane.pop();
                }
                // Opens a thread like a regular channel: ChatController only needs a valid
                // channelId (AppStore::selectChannel() does no lookup), so openChat() is reused.
                mainPage.openChat(threadId, guildId, threadName);
            });
            if (mainPage.navigationPane) {
                mainPage.navigationPane.push(threadPage);
            }
            // createObject() runs ThreadList.qml's onCreationCompleted() before the properties
            // are assigned, so it is called again here after channelId is set.
            threadPage.requestThreadsNow();
        } else {
            console.log("Could not create ThreadList.qml");
        }
    }

    function loadServerList(serverId, serverName) {
        if (activeContentType == "server" && activeServerId == serverId) {
            return;
        }

        var page = serverListDefinition.createObject();

        if (page) {
            page.serverId = serverId;
            page.serverName = serverName;
            page.channelSelected.connect(function (channelId, guildId, channelName) {
                mainPage.openChat(channelId, guildId, channelName);
            });
            page.forumChannelSelected.connect(function (channelId, guildId, channelName) {
                mainPage.openThreadList(channelId, guildId, channelName);
            });
            replaceContent(page);
            activeContentType = "server";
            activeServerId = serverId;
        } else {
            console.log("Could not create ServerList.qml");
        }
    }

    onCreationCompleted: {
        loadDmList();
        mainPageController.loadMainData();
    }

    attachedObjects: [
        ComponentDefinition {
            id: chatCardDefinition
            source: "asset:///ChatCard.qml"
        },
        ComponentDefinition {
            id: channelMemberListDefinition
            source: "asset:///ChannelMemberList.qml"
        },
        ComponentDefinition {
            id: threadListDefinition
            source: "asset:///ThreadList.qml"
        },
        ComponentDefinition {
            id: serverListDefinition
            source: "asset:///ServerList.qml"
        },
        ComponentDefinition {
            id: dmListDefinition
            source: "asset:///DmList.qml"
        }
    ]
}
