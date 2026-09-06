import bb.cascades 1.4

// Danh sách active thread của 1 channel. Không dùng controller C++ riêng vì
// dữ liệu đã có sẵn qua discordClient.threadsForChannel(channelId)
// (Q_INVOKABLE, xem GuildChannels.cpp::threadsForChannel()) - đủ đơn giản để
// nạp thẳng vào 1 ArrayDataModel ở tầng QML, không cần thêm 1
// ListItemProvider/Controller mới chỉ để trung chuyển cùng dữ liệu.
Page {
	id: threadListPage

	property string channelId: ""
	property string guildId: ""
	property string channelName: "general"
	property alias title: titleBar.title

	// Fix: "visible: threadDataModel.size() === 0" (dùng trước đây) gọi
	// thẳng hàm size() bên trong biểu thức binding declarative - cùng
	// lớp bug với "enabled: field.text.length" ở LoginPage.qml/MfaSheet.qml
	// (xem lịch sử fix đó): binding declarative không đảm bảo tự
	// re-evaluate khi NỘI DUNG model đổi qua append()/clear(), chỉ đáng
	// tin khi phụ thuộc 1 property có NOTIFY signal chuẩn. Dùng property
	// tường minh này, tự cập nhật ngay sau mỗi lần reload(), để chắc chắn
	// UI re-render đúng lúc.
	property int threadCount: 0

	// Lớp phòng thủ thứ 2, độc lập với việc disconnect() có chạy đúng
	// lúc hay không (xem cleanup() bên dưới) - đề phòng cả trường hợp
	// NavigationPane tự pop qua đường khác (vd. nút back vật lý của hệ
	// thống) mà không đi qua backRequested lẫn threadSelected, nơi
	// cleanup() được gọi tường minh. Tự quản lý hoàn toàn ở đây (không
	// dựa vào 1 API "isValid"/lifecycle nào của framework mà tôi không
	// chắc chắn tồn tại), set false trong cleanup() và kiểm tra ngay đầu
	// reload() trước khi động vào threadDataModel.
	property bool _isActive: true

	// Archived threads: tách biệt hoàn toàn khỏi threadDataModel (active
	// threads) - Discord trả 2 khái niệm khác nhau qua 2 nguồn khác nhau
	// (gateway thụ động vs REST theo yêu cầu), trộn chung dễ gây nhầm lẫn
	// trạng thái nào là active/archived. archivedHasMore điều khiển hiện/
	// ẩn nút "Load older threads"; archivedCursor là id thread cũ nhất đã
	// tải, dùng làm "before" cho trang tiếp theo.
	property bool archivedLoading: false
	property bool archivedHasMore: false
	property string archivedCursor: ""

	signal backRequested()
	signal threadSelected(string threadId, string threadName)

	actionBarVisibility: ChromeVisibility.Hidden

	titleBar: TitleBar {
		id: titleBar
		title: qsTr("Threads #") + threadListPage.channelName
		visibility: ChromeVisibility.Visible

		dismissAction: ActionItem {
			imageSource: "asset:///images/icons/accent/caret-left.png"

			onTriggered: {
				threadListPage.backRequested()
			}
		}
	}

	attachedObjects: [
		ArrayDataModel {
			id: threadDataModel
		}
	]

	// Nạp lại danh sách thread mỗi lần sheet này được mở, và mỗi khi
	// AppStore.channelThreadsByParentId đổi (thread mới tạo, thread active
	// khác load về) trong lúc sheet đang mở — tránh tình trạng danh sách
	// đứng yên (stale) nếu backend cập nhật threads trong khi người dùng
	// đang xem sheet.
	function reload() {
		if (!threadListPage._isActive) {
			return
		}
		if (threadListPage.channelId === "") {
			threadDataModel.clear()
			threadListPage.threadCount = 0
			return
		}
		threadDataModel.clear()
		var threads = discordClient.threadsForChannel(threadListPage.channelId)
		for (var i = 0; i < threads.length; ++i) {
			threadDataModel.append(threads[i])
		}
		threadListPage.threadCount = threadDataModel.size()
	}

	onCreationCompleted: {
		// Cùng lưu ý như ChannelMemberList.qml/MainPage.qml: createObject()
		// chạy onCreationCompleted() TRƯỚC khi property channelId được gán
		// từ nơi gọi (xem MainPage.qml::openChat()) - đừng gọi reload() ở
		// đây, gọi lại tường minh sau khi property đã có giá trị thật
		// (threadListPage.requestThreadsNow(), gọi từ MainPage.qml).
		appStore.channelThreadsChanged.connect(threadListPage.reload)
		discordClient.archivedThreadsLoaded.connect(threadListPage.onArchivedThreadsResult)
	}

	// Fix: "Page" trong Cascades KHÔNG có signal "onDestruction" (đó là
	// API của QtQuick Component, không tồn tại ở đây) - khai báo nó khiến
	// toàn bộ file lỗi parse (bug đã xác nhận qua log thực tế trước đây).
	// Từng dùng onBackRequested để disconnect, NHƯNG signal đó chỉ emit
	// khi bấm đúng nút back trên titleBar - khi người dùng bấm vào 1
	// thread (threadSelected), MainPage.qml gọi thẳng navigationPane.
	// pop() mà KHÔNG qua backRequested, nên disconnect không bao giờ
	// chạy trong trường hợp đó. Hệ quả quan sát thực tế: ReferenceError
	// "Can't find variable: threadListPage" khi THREAD_LIST_SYNC bắn tới
	// sau khi page đã bị pop/hủy, cố gọi lại reload() trên object không
	// còn tồn tại. Đưa cleanup ra 1 hàm riêng, gọi tường minh từ MỌI nơi
	// pop trang này (xem MainPage.qml), không chỉ riêng nút back. Áp dụng
	// đúng bài học này cho cả archivedThreadsLoaded (cùng rủi ro).
	function cleanup() {
		threadListPage._isActive = false
		appStore.channelThreadsChanged.disconnect(threadListPage.reload)
		discordClient.archivedThreadsLoaded.disconnect(threadListPage.onArchivedThreadsResult)
	}

	function requestThreadsNow() {
		// Fix: KHÔNG còn discordClient.requestThreadsForChannel() để gọi -
		// hàm đó (REST cấp-channel) đã bị xoá vì Discord xác nhận từ chối
		// nó với đúng lỗi "Only bots can use this endpoint." (code 20002),
		// y hệt endpoint cấp-guild đã thử trước đó. Threads giờ đến hoàn
		// toàn THỤ ĐỘNG qua gateway (event THREAD_LIST_SYNC, xử lý ở
		// Client.cpp::onGatewayDispatch(), tự chảy vào khi guild được
		// subscribe) - ở đây chỉ cần đọc lại cache hiện có, không có gì để
		// "request" chủ động nữa. reload() cũng đã được gọi lại tự động
		// mỗi khi appStore.channelThreadsChanged bắn (connect ở
		// onCreationCompleted), nên danh sách sẽ tự cập nhật khi
		// THREAD_LIST_SYNC tiếp theo về, kể cả sau lần gọi này.
		reload()
	}

	// Khác active threads (thụ động qua gateway) - archived threads phải
	// CHỦ ĐỘNG request qua REST (endpoint /channels/{id}/threads/archived/
	// public, khác 2 endpoint "active threads" bot-only đã xác nhận trước
	// đây). Gọi khi bấm nút "Load older threads", hoặc tự động lần đầu
	// nếu channel chưa từng archive-fetch (archivedCursor rỗng và chưa
	// loading).
	function loadOlderThreads() {
		if (threadListPage.archivedLoading || threadListPage.channelId === "") {
			return
		}
		threadListPage.archivedLoading = true
		discordClient.requestArchivedThreads(threadListPage.channelId, threadListPage.archivedCursor)
	}

	function onArchivedThreadsResult(channelId, threads, hasMore) {
		if (!threadListPage._isActive || channelId !== threadListPage.channelId) {
			return
		}
		threadListPage.archivedLoading = false
		threadListPage.archivedHasMore = hasMore
		// Nối thẳng vào threadDataModel (cùng danh sách với active threads)
		// thay vì 1 ListView/model riêng - đơn giản hơn nhiều so với dựng
		// section header "Active"/"Archived" phân biệt trong Cascades
		// (chưa có tiền lệ đáng tin cậy trong codebase này để làm đúng),
		// và người dùng chỉ cần thấy TẤT CẢ bài viết, không nhất thiết
		// phải phân biệt rạch ròi active/archived ở UI.
		for (var i = 0; i < threads.length; ++i) {
			threadDataModel.append(threads[i])
		}
		threadListPage.threadCount = threadDataModel.size()
		if (threads.length > 0) {
			threadListPage.archivedCursor = threads[threads.length - 1].id
		}
	}

	Container {
		horizontalAlignment: HorizontalAlignment.Fill
		verticalAlignment: VerticalAlignment.Fill

		layout: StackLayout {}

		Label {
			text: qsTr("No active threads in this channel")
			horizontalAlignment: HorizontalAlignment.Center
			verticalAlignment: VerticalAlignment.Center
			textStyle.color: Color.create("#88ffffff")
			visible: threadListPage.threadCount === 0
		}

		ListView {
			id: threadList
			dataModel: threadDataModel
			horizontalAlignment: HorizontalAlignment.Fill
			verticalAlignment: VerticalAlignment.Fill
			visible: threadListPage.threadCount > 0

			// Fix: "onTouch" không phải signal hợp lệ trên ListItemComponent
			// (đó chỉ là 1 factory định nghĩa delegate, không phải bản thân
			// 1 control nhận sự kiện chạm) - khai báo nó khiến file lỗi
			// parse y hệt lỗi "onDestruction" đã gặp trước đó ("Cannot
			// assign to non-existent property"), ThreadList.qml không tạo
			// được. Toàn bộ codebase (ServerList.qml, ChatCard.qml,
			// DmList.qml) đều dùng ListView.onTriggered để xử lý bấm vào 1
			// item - theo đúng pattern đó thay vì onTouch trên delegate.
			onTriggered: {
				var item = threadDataModel.data(indexPath);
				threadListPage.threadSelected(item.id, item.name);
			}

			listItemComponents: [
				ListItemComponent {
					Container {
						horizontalAlignment: HorizontalAlignment.Fill
						leftPadding: ui.du(2.0)
						rightPadding: ui.du(2.0)
						topPadding: ui.du(1.0)
						bottomPadding: ui.du(1.0)

						layout: StackLayout {
							orientation: LayoutOrientation.LeftToRight
						}

						ImageView {
							imageSource: "asset:///images/icons/ic_chat_multiperson.png"
							verticalAlignment: VerticalAlignment.Center
							rightMargin: ui.du(1.5)
							minWidth: ui.du(3.0)
							minHeight: ui.du(3.0)
						}

						Label {
							text: ListItemData.name
							verticalAlignment: VerticalAlignment.Center
							horizontalAlignment: HorizontalAlignment.Fill
							layoutProperties: StackLayoutProperties {
								spaceQuota: 1.0
							}
						}
					}
				}
			]
		}

		// Bài viết cũ hơn thời hạn auto-archive (Discord tự archive, không
		// còn nằm trong active threads/THREAD_LIST_SYNC nữa - xem comment
		// requestArchivedThreads() ở Client.hpp) - bấm để chủ động fetch
		// thêm qua REST, phân trang bằng archivedCursor.
		Button {
			text: threadListPage.archivedLoading ? qsTr("Loading...") : qsTr("Load older threads")
			horizontalAlignment: HorizontalAlignment.Fill
			topMargin: ui.du(1.0)
			enabled: !threadListPage.archivedLoading
			// Hiện ngay cả khi threadCount === 0 (channel có thể không còn
			// active thread nào nhưng vẫn có archived) - chỉ ẩn hẳn sau khi
			// đã xác nhận không còn trang nào nữa (archivedHasMore chuyển
			// false, xem onArchivedThreadsResult()). archivedCursor === ""
			// nghĩa là CHƯA từng bấm/tải lần nào - vẫn hiện nút để người
			// dùng có thể chủ động bấm.
			visible: threadListPage.archivedCursor !== "" ? threadListPage.archivedHasMore : true

			onClicked: {
				threadListPage.loadOlderThreads()
			}
		}
	}
}
