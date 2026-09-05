import bb.cascades 1.4

Page {
	id: memberPage

	property string channelId: ""
	property string guildId: ""
	property string channelName: "general"
	property alias title: titleBar.title
	// Alias trỏ tới context property memberListController (inject từ C++
	// vào root context của Page này). Cần alias riêng vì các delegate bên
	// trong ListItemComponent chạy trong scope RIÊNG của chúng và KHÔNG
	// nhìn thấy context property gắn ở root Page - chỉ thấy được
	// ListItemData và các property thực sự khai báo trên Page cha, truy
	// cập ngược lên qua id (memberPage.controller). Không có alias này,
	// gọi thẳng "memberListController" trong ListItemComponent ném
	// ReferenceError (xem tryLoadAvatar()/onCreationCompleted() bên dưới).
	// Cascades QML không hỗ trợ kiểu "var" cho property (khác QtQuick
	// thường) - dùng "variant" thay thế.
	property variant controller: memberListController

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

						function tryLoadAvatar() {
							if (ListItemData.avatarUrl === "") {
								return
							}
							var cached = memberPage.controller.cachedAvatarSource(ListItemData.avatarUrl)
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
							memberPage.controller.avatarCached.connect(memberRow.onAvatarCached)
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
		}
	}

	// Lazy-load: chỉ gửi request/subscribe member list khi sheet này thực
	// sự được mở (tương ứng lúc người dùng mở tab Members), không phải
	// ngay khi mở channel — đúng yêu cầu tối ưu ban đầu. Xem
	// MemberListController::requestMemberList() (MemberListController.cpp)
	// để biết chi tiết cơ chế subscribe qua Gateway.
	//
	// KHÔNG gọi trong onCreationCompleted: createObject() ở MainPage.qml
	// kích hoạt onCreationCompleted() NGAY LẬP TỨC, đồng bộ, TRƯỚC khi
	// channelId/guildId kịp được gán (property vẫn là "" mặc định lúc
	// đó) — nên request sẽ luôn nhận tham số rỗng và bị bỏ qua. Thay vào
	// đó, MainPage.qml gọi hàm này TƯỜNG MINH ngay sau khi đã gán xong
	// channelId/guildId, đảm bảo dữ liệu đúng được dùng.
	function requestMemberListNow() {
		memberListController.requestMemberList(memberPage.channelId, memberPage.guildId)
	}

	onBackRequested: {
		memberListController.releaseMemberList()
	}
}
