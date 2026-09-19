import bb.cascades 1.4

Page {
    id: mainPage

    property variant navigationPane: null
    property variant activeContent: null
    property string activeContentType: ""
    property string activeServerId: ""

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
                                // Fix: white bar for ANY new message in
                                // the server, mention or not - unread
                                // (a plain new message) OR mentionCount
                                // > 0 (a ping) both show it. The red
                                // badge below is separate and layered
                                // on TOP of this bar for pings
                                // specifically, not a replacement for
                                // it - a plain unread message gets only
                                // the white bar, a ping gets both.
                                visible: (ListItemData.type == "server" || ListItemData.type == "folder") && (ListItemData.unread == true || ListItemData.mentionCount > 0)
                            }

                            Container {
                                preferredWidth: ui.du(3)
                                preferredHeight: ui.du(3)
                                horizontalAlignment: HorizontalAlignment.Right
                                verticalAlignment: VerticalAlignment.Top
                                background: Color.create("#ED4245")
                                // Fix: pings specifically (not every
                                // unread message) still get this red
                                // count badge, shown together with the
                                // white bar above rather than instead
                                // of it - both clear the same way, when
                                // the user reads the pinging message
                                // (mentionCount drops to 0).
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
            page.title = channelName;
            page.compactMessageEnabled = settingsController.compactMessageEnabled;
            settingsController.compactMessageEnabledChanged.connect(function (enabled) {
                page.compactMessageEnabled = enabled;
            });
            page.backRequested.connect(function () {
                if (mainPage.navigationPane) {
                    mainPage.navigationPane.pop();
                }
            });
            page.memberListRequested.connect(function () {
                var memberPage = channelMemberListDefinition.createObject();

                if (memberPage) {
                    memberPage.channelId = channelId;
                    memberPage.guildId = guildId;
                    memberPage.channelName = channelName;
                    memberPage.title = "Members #" + channelName;
                    memberPage.backRequested.connect(function () {
                        if (mainPage.navigationPane) {
                            mainPage.navigationPane.pop();
                        }
                    });
                    if (mainPage.navigationPane) {
                        mainPage.navigationPane.push(memberPage);
                    }
                    // IMPORTANT: createObject() fires
                    // ChannelMemberList.qml's onCreationCompleted()
                    // IMMEDIATELY, synchronously, BEFORE the property
                    // assignments above (channelId/guildId are still the
                    // default empty "" at that point) — so
                    // requestMemberList() called from inside
                    // onCreationCompleted() always gets two empty
                    // arguments and early-returns, leaving the Members
                    // sheet permanently blank even though
                    // channelId/guildId get assigned correctly right
                    // after (confirmed bug via real logs — none of
                    // MemberListController::requestMemberList()'s
                    // qDebug lines ever showed up). Call it again
                    // EXPLICITLY here, after the properties have real
                    // values, to make sure the correct data gets used.
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

    // Pulled out from inside openChat() (which previously could only be
    // opened via a channel's "Threads" action) so it can be shared with
    // tapping DIRECTLY on a forum/media channel in ServerList (channels
    // of this type can't open via ChatCard - they have no messages of
    // their own, each "post" IS a thread).
    function openThreadList(channelId, guildId, channelName) {
        var threadPage = threadListDefinition.createObject();

        if (threadPage) {
            threadPage.channelId = channelId;
            threadPage.guildId = guildId;
            threadPage.channelName = channelName;
            threadPage.title = qsTr("Threads #") + channelName;
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
                // Opens a thread exactly like a regular channel:
                // ChatController just needs a valid channelId, it
                // doesn't care whether it's in
                // allGuildChannels/channelTree (see AppStore::
                // selectChannel() - only sets m_selectedChannelId, no
                // extra lookup) - so openChat() is reused directly, no
                // need for a separate flow for threads.
                mainPage.openChat(threadId, guildId, threadName);
            });
            if (mainPage.navigationPane) {
                mainPage.navigationPane.push(threadPage);
            }
            // Same caveat as ChannelMemberList.qml: createObject() runs
            // ThreadList.qml's onCreationCompleted() BEFORE the property
            // assignments above, so it's called again explicitly after
            // channelId has a real value.
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
