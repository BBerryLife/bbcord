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
                                visible: (ListItemData.type == "server" || ListItemData.type == "folder") && ListItemData.unread == true
                            }

                            Container {
                                preferredWidth: ui.du(3)
                                preferredHeight: ui.du(3)
                                horizontalAlignment: HorizontalAlignment.Right
                                verticalAlignment: VerticalAlignment.Top
                                background: Color.create("#ED4245")
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
                    // QUAN TRỌNG: createObject() kích hoạt
                    // onCreationCompleted() của ChannelMemberList.qml
                    // NGAY LẬP TỨC, đồng bộ, TRƯỚC các dòng gán property ở
                    // trên (channelId/guildId lúc đó vẫn là giá trị mặc
                    // định rỗng "") — nên requestMemberList() gọi từ bên
                    // trong onCreationCompleted() luôn nhận 2 tham số
                    // rỗng và bị early-return, sheet Members hiện trống
                    // vĩnh viễn dù channelId/guildId đã được gán đúng
                    // ngay sau đó (bug đã xác nhận qua log thực tế —
                    // không có bất kỳ dòng qDebug nào của
                    // MemberListController::requestMemberList() xuất
                    // hiện). Gọi lại TƯỜNG MINH ở đây, sau khi property
                    // đã có giá trị thật, để đảm bảo đúng dữ liệu được
                    // dùng.
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
            id: serverListDefinition
            source: "asset:///ServerList.qml"
        },
        ComponentDefinition {
            id: dmListDefinition
            source: "asset:///DmList.qml"
        }
    ]
}
