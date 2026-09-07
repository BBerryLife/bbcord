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

						// Fix: the outer id (memberPage) does NOT resolve
						// from within a ListItemComponent's own scope in
						// Cascades (see comment at the top of the file) —
						// "memberPage.controller" throws a ReferenceError
						// right in onCreationCompleted(), so
						// tryLoadAvatar() never actually calls
						// cachedAvatarSource() and avatarCached never
						// gets connected. Observed effect: avatars didn't
						// load lazily per-row as designed, they only
						// appeared all at once when the whole ListView
						// got rebuilt/re-rendered. Fixed by using the
						// same pattern already proven stable elsewhere in
						// the app (see ChatCard.qml:
						// loadAttachmentImage() declared on the parent
						// ListView, the delegate calling back up via
						// "ListItem.view.<function>" — the standard
						// Cascades API for a delegate to reach its
						// containing ListView) instead of trying to
						// access a context property through a local id
						// not visible from this scope.
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

			// Bridge for the "member" delegate to call back up via
			// ListItem.view.loadMemberAvatar()/connectAvatarCached() -
			// see the comment at tryLoadAvatar() in the "member"
			// ListItemComponent above for why memberListController (or
			// the memberPage.controller alias) isn't called directly
			// from inside the delegate scope.
			function loadMemberAvatar(avatarUrl) {
				return memberListController.cachedAvatarSource(avatarUrl)
			}

			function connectAvatarCached(handler) {
				memberListController.avatarCached.connect(handler)
			}
		}
	}

	// Lazy-load: only sends a request/subscribes the member list when
	// this sheet is actually opened (i.e. when the user opens the
	// Members tab), not as soon as the channel is opened - per the
	// original optimization requirement. See
	// MemberListController::requestMemberList() (MemberListController.cpp)
	// for the details of the Gateway subscribe mechanism.
	//
	// NOT called in onCreationCompleted: createObject() in MainPage.qml
	// fires onCreationCompleted() IMMEDIATELY, synchronously, BEFORE
	// channelId/guildId get assigned (the properties are still the
	// default "" at that point) — so the request would always get empty
	// arguments and be skipped. Instead, MainPage.qml calls this
	// function EXPLICITLY right after channelId/guildId have been
	// assigned, ensuring the correct data is used.
	function requestMemberListNow() {
		memberListController.requestMemberList(memberPage.channelId, memberPage.guildId)
	}

	onBackRequested: {
		memberListController.releaseMemberList()
	}
}
