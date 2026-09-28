import bb.cascades 1.4

Page {
	id: memberPage

	property string channelId: ""
	property string guildId: ""
	property string channelName: "general"
	property alias title: titleBar.title

	signal backRequested()

	actionBarVisibility: ChromeVisibility.Hidden

	titleBar: TitleBar {
		id: titleBar
		title: qsTr("Members #") + memberPage.channelName
		visibility: ChromeVisibility.Visible

		dismissAction: ActionItem {
			imageSource: "asset:///images/icons/accent/caret-left.png"

			onTriggered: {
				memberPage.backRequested()
			}
		}
	}

	Container {
		horizontalAlignment: HorizontalAlignment.Fill
		verticalAlignment: VerticalAlignment.Fill

		layout: StackLayout {}

		ListView {
			id: memberList
			dataModel: memberListController.memberDataModel
			horizontalAlignment: HorizontalAlignment.Fill
			verticalAlignment: VerticalAlignment.Fill

			listItemComponents: [
				ListItemComponent {
					type: "role"

					Container {
						horizontalAlignment: HorizontalAlignment.Fill
						leftPadding: ui.du(2.0)
						rightPadding: ui.du(2.0)
						topPadding: ui.du(2.2)
						bottomPadding: ui.du(0.6)

						Label {
							text: ListItemData.name + " — " + ListItemData.count
							opacity: 0.55
							textStyle.fontSize: FontSize.XSmall
							textStyle.fontWeight: FontWeight.Normal
							textStyle.color: Color.create("#B5BAC1")
						}
					}
				},

				ListItemComponent {
					type: "member"

					Container {
						id: memberRow
						horizontalAlignment: HorizontalAlignment.Fill
						leftPadding: ui.du(2.0)
						rightPadding: ui.du(2.0)
						topPadding: ui.du(0.8)
						bottomPadding: ui.du(0.8)

						property string avatarSource: ""

						layout: StackLayout {
							orientation: LayoutOrientation.LeftToRight
						}

						// The outer id (memberPage) does not resolve inside a ListItemComponent's scope,
						// so "memberPage.controller" threw in onCreationCompleted() and avatars never
						// loaded per row. Use ListItem.view.<function>, as ChatCard.qml's
						// loadAttachmentImage() does.
						function tryLoadAvatar() {
							if (ListItemData.avatarUrl === "") {
								return
							}
							var cached = ListItem.view.loadMemberAvatar(ListItemData.avatarUrl)
							if (cached !== "") {
								memberRow.avatarSource = cached
							}
						}

						function onAvatarCached(url, imageSource) {
							if (url === ListItemData.avatarUrl) {
								memberRow.avatarSource = imageSource
							}
						}

						onCreationCompleted: {
							tryLoadAvatar()
							ListItem.view.connectAvatarCached(memberRow.onAvatarCached)
						}

						Container {
							preferredWidth: ui.du(7.0)
							preferredHeight: ui.du(7.0)
							minWidth: ui.du(7.0)
							minHeight: ui.du(7.0)
							maxWidth: ui.du(7.0)
							maxHeight: ui.du(7.0)
							verticalAlignment: VerticalAlignment.Center
							background: Color.create(ListItemData.avatarColor)

							layout: DockLayout {}

							ImageView {
								imageSource: memberRow.avatarSource
								visible: memberRow.avatarSource !== ""
								horizontalAlignment: HorizontalAlignment.Fill
								verticalAlignment: VerticalAlignment.Fill
								scalingMethod: ScalingMethod.AspectFill
							}

							Label {
								text: ListItemData.initials
								visible: memberRow.avatarSource === ""
								horizontalAlignment: HorizontalAlignment.Center
								verticalAlignment: VerticalAlignment.Center
								textStyle.fontSize: FontSize.Small
								textStyle.fontWeight: FontWeight.Bold
								textStyle.color: Color.White
							}
						}

						Container {
							horizontalAlignment: HorizontalAlignment.Fill
							verticalAlignment: VerticalAlignment.Center
							leftMargin: ui.du(1.5)

							Label {
								text: ListItemData.name
								textStyle.fontSize: FontSize.Small
								textStyle.fontWeight: FontWeight.Normal
								textStyle.color: Color.create(ListItemData.nameColor)
							}

							Label {
								text: ListItemData.status
								topMargin: ui.du(-0.4)
								opacity: 0.6
								textStyle.fontSize: FontSize.XSmall
								textStyle.color: Color.create("#B5BAC1")
							}
						}
					}
				}
			]

			function itemType(data, indexPath) {
				return data.type
			}

			// Bridge for the "member" delegate: it calls ListItem.view.loadMemberAvatar()/
			// connectAvatarCached() (see tryLoadAvatar() above).
			function loadMemberAvatar(avatarUrl) {
				return memberListController.cachedAvatarSource(avatarUrl)
			}

			function connectAvatarCached(handler) {
				memberListController.avatarCached.connect(handler)
			}
		}
	}

	// Lazy-load: subscribes the member list only when the sheet opens (see
	// MemberListController::requestMemberList()). Not called in onCreationCompleted:
	// createObject() runs it before channelId/guildId are assigned, so MainPage.qml
	// calls this explicitly afterwards.
	function requestMemberListNow() {
		memberListController.requestMemberList(memberPage.channelId, memberPage.guildId)
	}

	onBackRequested: {
		memberListController.releaseMemberList()
	}
}
