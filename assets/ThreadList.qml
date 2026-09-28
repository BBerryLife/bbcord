import bb.cascades 1.4

// Active thread list for a channel. Loads discordClient.threadsForChannel()
// straight into an ArrayDataModel; no dedicated C++ controller needed.
Page {
	id: threadListPage

	property string channelId: ""
	property string guildId: ""
	property string channelName: "general"
	property alias title: titleBar.title

	// Explicit property instead of "visible: threadDataModel.size() === 0": bindings
	// do not re-evaluate on append()/clear(), only on properties with NOTIFY signals.
	// Updated after every reload().
	property int threadCount: 0

	// Guard against the page being popped some other way (e.g. hardware back)
	// without cleanup(); set false in cleanup() and checked at the top of reload().
	property bool _isActive: true

	// Archived threads are kept separate from threadDataModel (gateway vs REST).
	// archivedHasMore shows/hides "Load older threads"; archivedCursor is the oldest
	// loaded thread id, used as "before" for the next page.
	property bool archivedLoading: false
	property bool archivedHasMore: false
	property string archivedCursor: ""

	signal backRequested()
	signal threadSelected(string threadId, string threadName)

	actionBarVisibility: ChromeVisibility.Hidden

	titleBar: TitleBar {
		id: titleBar
		title: qsTr("Forums #") + threadListPage.channelName
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

	// Reloads on open and whenever AppStore.channelThreadsByParentId changes while open.
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
		// createObject() runs onCreationCompleted() before channelId is assigned, so
		// do not reload() here; MainPage.qml calls requestThreadsNow() afterwards.
		appStore.channelThreadsChanged.connect(threadListPage.reload)
		discordClient.archivedThreadsLoaded.connect(threadListPage.onArchivedThreadsResult)
	}

	// Page has no onDestruction, and onBackRequested only fires for the title bar's
	// back button, not when MainPage.qml pops the page directly. cleanup() is called
	// explicitly from every place that pops the page, so a late THREAD_LIST_SYNC or
	// archivedThreadsLoaded does not hit a destroyed object.
	function cleanup() {
		threadListPage._isActive = false
		appStore.channelThreadsChanged.disconnect(threadListPage.reload)
		discordClient.archivedThreadsLoaded.disconnect(threadListPage.onArchivedThreadsResult)
	}

	function requestThreadsNow() {
		// Threads arrive passively via THREAD_LIST_SYNC (the channel-level REST endpoint
		// is bot-only), so this only reads the cache. reload() also runs on
		// appStore.channelThreadsChanged.
		reload()
	}

	// Archived threads must be requested via REST (/channels/{id}/threads/archived/public).
	// Called by "Load older threads", and automatically the first time if never fetched.
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
		// Appended into threadDataModel (same list as active threads) instead of building section headers.
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

		// Container bottomPadding: in a StackLayout a child's margin only spaces it from
		// siblings, so the Button's bottomMargin had no effect at the container edge.
		// The Button's own margins still give top spacing.
		bottomPadding: ui.du(1.5)

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

			// ListItemComponent has no onTouch; use ListView.onTriggered like ServerList.qml,
			// ChatCard.qml and DmList.qml.
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

		// Posts past the auto-archive window are fetched via REST on tap, paginated by archivedCursor.
		Button {
			text: threadListPage.archivedLoading ? qsTr("Loading...") : qsTr("Load older threads")
			// Center alignment so the button sizes to its text instead of filling the width.
			horizontalAlignment: HorizontalAlignment.Center
			topMargin: ui.du(1.0)
			// leftMargin/rightMargin have no effect while the button is not Fill-width; kept in case Fill returns.
			leftMargin: ui.du(2.0)
			rightMargin: ui.du(2.0)
			bottomMargin: ui.du(1.5)
			enabled: !threadListPage.archivedLoading
			// Shown even when threadCount === 0 (archived threads may remain); hidden only
			// once archivedHasMore is false. archivedCursor === "" means never loaded.
			visible: threadListPage.archivedCursor !== "" ? threadListPage.archivedHasMore : true

			onClicked: {
				threadListPage.loadOlderThreads()
			}
		}
	}
}
