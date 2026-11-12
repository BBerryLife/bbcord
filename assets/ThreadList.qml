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

	// Threads come from two sources, merged by id in reload():
	//  - the gateway cache (discordClient.threadsForChannel): only what Discord pushes,
	//    which is NOT every active thread;
	//  - REST /channels/{id}/threads/search (what the official client uses): active AND
	//    archived threads, paged by offset. searchThreads holds everything loaded so far.
	// searchOffset is the offset of the next "last_message_time" page.
	property variant searchThreads: []
	property bool searchLoading: false
	property bool searchHasMore: false
	property bool searchLoadedOnce: false
	property int searchOffset: 0

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
		// Union of gateway + search results, deduplicated by id (search data wins: it
		// carries the freshest archived/last-message info), newest activity first.
		var byId = {}
		var merged = []
		var gatewayThreads = discordClient.threadsForChannel(threadListPage.channelId)
		var searched = threadListPage.searchThreads
		var sources = [gatewayThreads, searched]
		for (var s = 0; s < sources.length; ++s) {
			for (var i = 0; i < sources[s].length; ++i) {
				var t = sources[s][i]
				var existing = byId[t.id]
				if (existing === undefined) {
					byId[t.id] = merged.length
					merged.push(t)
				} else {
					merged[existing] = t
				}
			}
		}
		merged.sort(function (a, b) {
			return threadListPage.compareSnowflakes(threadListPage.activityKey(b), threadListPage.activityKey(a))
		})
		threadDataModel.clear()
		for (var k = 0; k < merged.length; ++k) {
			threadDataModel.append(merged[k])
		}
		threadListPage.threadCount = threadDataModel.size()
	}

	// Most recent activity of a thread: last message, else its creation (the thread id).
	function activityKey(thread) {
		if (thread.lastMessageId !== undefined && thread.lastMessageId !== "") {
			return thread.lastMessageId
		}
		return thread.id
	}

	// Snowflakes are decimal strings of different lengths; compare length first, then
	// lexicographically (avoids Number precision loss above 2^53).
	function compareSnowflakes(a, b) {
		if (a.length !== b.length) {
			return a.length - b.length
		}
		if (a === b) {
			return 0
		}
		return a < b ? -1 : 1
	}

	onCreationCompleted: {
		// createObject() runs onCreationCompleted() before channelId is assigned, so
		// do not reload() here; MainPage.qml calls requestThreadsNow() afterwards.
		appStore.channelThreadsChanged.connect(threadListPage.reload)
		discordClient.threadSearchLoaded.connect(threadListPage.onThreadSearchResult)
	}

	// Page has no onDestruction, and onBackRequested only fires for the title bar's
	// back button, not when MainPage.qml pops the page directly. cleanup() is called
	// explicitly from every place that pops the page, so a late THREAD_LIST_SYNC or
	// threadSearchLoaded does not hit a destroyed object.
	function cleanup() {
		threadListPage._isActive = false
		appStore.channelThreadsChanged.disconnect(threadListPage.reload)
		discordClient.threadSearchLoaded.disconnect(threadListPage.onThreadSearchResult)
	}

	function requestThreadsNow() {
		// Shows whatever the gateway already cached, then asks REST for the full list.
		// Page 1 is requested twice on purpose: "last_message_time" skips posts nobody
		// has replied to yet, "creation_time" covers them (the official client merges both).
		reload()
		if (threadListPage.channelId === "") {
			return
		}
		threadListPage.searchThreads = []
		threadListPage.searchOffset = 0
		threadListPage.searchHasMore = false
		threadListPage.searchLoadedOnce = false
		threadListPage.searchLoading = true
		discordClient.requestThreadSearch(threadListPage.channelId, 0, "last_message_time")
		discordClient.requestThreadSearch(threadListPage.channelId, 0, "creation_time")
	}

	// Next page of /threads/search ("last_message_time" order), by offset.
	function loadMoreThreads() {
		if (threadListPage.searchLoading || threadListPage.channelId === "") {
			return
		}
		threadListPage.searchLoading = true
		discordClient.requestThreadSearch(threadListPage.channelId, threadListPage.searchOffset, "last_message_time")
	}

	function onThreadSearchResult(channelId, threads, hasMore, offset, sortBy) {
		if (!threadListPage._isActive || channelId !== threadListPage.channelId) {
			return
		}
		threadListPage.searchLoadedOnce = true
		threadListPage.searchLoading = false

		// Only the "last_message_time" stream drives paging; the creation_time first page
		// is just merged in.
		if (sortBy === "last_message_time") {
			threadListPage.searchHasMore = hasMore
			threadListPage.searchOffset = offset + threads.length
		}

		var all = threadListPage.searchThreads.slice(0)
		for (var i = 0; i < threads.length; ++i) {
			var found = false
			for (var j = 0; j < all.length; ++j) {
				if (all[j].id === threads[i].id) {
					all[j] = threads[i]
					found = true
					break
				}
			}
			if (!found) {
				all.push(threads[i])
			}
		}
		threadListPage.searchThreads = all
		reload()
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
			text: threadListPage.searchLoading ? qsTr("Loading threads...") : qsTr("No threads in this channel")
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

		// Next page of threads (active + archived), paged by offset. Hidden once the
		// server says there is nothing more.
		Button {
			text: threadListPage.searchLoading ? qsTr("Loading...") : qsTr("Load more threads")
			// Center alignment so the button sizes to its text instead of filling the width.
			horizontalAlignment: HorizontalAlignment.Center
			topMargin: ui.du(1.0)
			bottomMargin: ui.du(1.5)
			enabled: !threadListPage.searchLoading
			visible: threadListPage.searchHasMore

			onClicked: {
				threadListPage.loadMoreThreads()
			}
		}
	}
}
