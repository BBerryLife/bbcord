import bb.cascades 1.4
import QtQuick 1.0

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

	// Auto-fill: Discord reports the real number of threads ("total_results"). After
	// every page the list keeps requesting more (and retries after failures/stalls)
	// until it holds that many, both streams are exhausted, or nothing new arrives for
	// maxStallRounds rounds in a row.
	//  - searchTotal: largest total_results seen (0 = unknown);
	//  - creationOffset/creationHasMore: paging of the "creation_time" stream, which also
	//    lists posts nobody replied to yet (the "last_message_time" stream skips them);
	//  - pendingRequests: REST pages in flight (searchLoading is derived from it).
	property int searchTotal: 0
	property int creationOffset: 0
	property bool creationHasMore: false
	property int pendingRequests: 0
	property bool autoFill: false
	property int stallRounds: 0
	property int lastCount: -1
	property int requestsSent: 0
	property bool incomplete: false
	property int maxStallRounds: 3
	property int maxRequests: 60

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
		},
		// Pause between auto-fill rounds; grows with stallRounds so retries back off.
		Timer {
			id: autoFillTimer
			interval: 800
			repeat: false
			onTriggered: {
				threadListPage.autoStep()
			}
		},
		// A page that never answers (REST error: no signal is emitted) must not freeze
		// auto-fill; after the timeout it is counted as a failed round and retried.
		Timer {
			id: pendingWatchdog
			interval: 12000
			repeat: false
			onTriggered: {
				if (threadListPage.pendingRequests > 0) {
					threadListPage.pendingRequests = 0
					threadListPage.searchLoading = false
					threadListPage.afterRound()
				}
			}
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
		threadListPage.incomplete = threadListPage.searchTotal > 0 && threadListPage.threadCount < threadListPage.searchTotal
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
		threadListPage.autoFill = false
		autoFillTimer.running = false
		pendingWatchdog.running = false
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
		threadListPage.searchTotal = 0
		threadListPage.creationOffset = 0
		threadListPage.creationHasMore = false
		threadListPage.pendingRequests = 0
		threadListPage.stallRounds = 0
		threadListPage.lastCount = -1
		threadListPage.requestsSent = 0
		threadListPage.autoFill = true
		threadListPage.sendSearch(0, "last_message_time")
		threadListPage.sendSearch(0, "creation_time")
	}

	function sendSearch(offset, sortBy) {
		threadListPage.pendingRequests = threadListPage.pendingRequests + 1
		threadListPage.requestsSent = threadListPage.requestsSent + 1
		threadListPage.searchLoading = true
		pendingWatchdog.running = false
		pendingWatchdog.running = true
		discordClient.requestThreadSearch(threadListPage.channelId, offset, sortBy)
	}

	// Manual "Load more": restarts auto-fill from the current offsets.
	function loadMoreThreads() {
		if (threadListPage.searchLoading || threadListPage.channelId === "") {
			return
		}
		threadListPage.stallRounds = 0
		threadListPage.requestsSent = 0
		threadListPage.autoFill = true
		threadListPage.autoStep()
	}

	// One auto-fill round: asks for the next page of every stream that still has data,
	// or (when none reports more but the total is not reached) re-asks the current
	// page of the main stream as a retry.
	function autoStep() {
		if (!threadListPage._isActive || !threadListPage.autoFill || threadListPage.searchLoading || threadListPage.channelId === "") {
			return
		}
		if (threadListPage.requestsSent >= threadListPage.maxRequests) {
			threadListPage.autoFill = false
			return
		}
		var sent = false
		if (threadListPage.searchHasMore) {
			threadListPage.sendSearch(threadListPage.searchOffset, "last_message_time")
			sent = true
		}
		if (threadListPage.creationHasMore) {
			threadListPage.sendSearch(threadListPage.creationOffset, "creation_time")
			sent = true
		}
		if (!sent) {
			threadListPage.sendSearch(threadListPage.searchOffset, "last_message_time")
		}
	}

	// Runs once no request is in flight: decides whether the list is complete, whether
	// the last round made progress, and schedules the next round.
	function afterRound() {
		reload()
		if (!threadListPage._isActive || !threadListPage.autoFill) {
			return
		}
		var have = threadListPage.threadCount
		var total = threadListPage.searchTotal
		var more = threadListPage.searchHasMore || threadListPage.creationHasMore

		// Complete: we hold as many threads as Discord says exist.
		if (total > 0 && have >= total) {
			threadListPage.autoFill = false
			return
		}
		// Total unknown (or not reached) and nothing more to page: only retries remain.
		if (total <= 0 && !more) {
			threadListPage.autoFill = false
			return
		}

		if (have > threadListPage.lastCount) {
			threadListPage.stallRounds = 0
		} else {
			threadListPage.stallRounds = threadListPage.stallRounds + 1
		}
		threadListPage.lastCount = have
		if (threadListPage.stallRounds >= threadListPage.maxStallRounds) {
			threadListPage.autoFill = false
			return
		}
		autoFillTimer.interval = 800 * (1 + threadListPage.stallRounds)
		autoFillTimer.running = false
		autoFillTimer.running = true
	}

	function onThreadSearchResult(channelId, threads, hasMore, offset, sortBy, totalResults) {
		if (!threadListPage._isActive || channelId !== threadListPage.channelId) {
			return
		}
		threadListPage.searchLoadedOnce = true
		if (threadListPage.pendingRequests > 0) {
			threadListPage.pendingRequests = threadListPage.pendingRequests - 1
		}
		threadListPage.searchLoading = threadListPage.pendingRequests > 0
		if (!threadListPage.searchLoading) {
			pendingWatchdog.running = false
		}
		if (totalResults > threadListPage.searchTotal) {
			threadListPage.searchTotal = totalResults
		}

		// Each sort order is its own paged stream with its own offset.
		if (sortBy === "last_message_time") {
			threadListPage.searchHasMore = hasMore
			threadListPage.searchOffset = offset + threads.length
		} else {
			threadListPage.creationHasMore = hasMore
			threadListPage.creationOffset = offset + threads.length
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
		if (threadListPage.pendingRequests === 0) {
			threadListPage.afterRound()
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
			text: threadListPage.searchLoading ? qsTr("Loading threads...") : qsTr("No threads in this channel")
			horizontalAlignment: HorizontalAlignment.Center
			verticalAlignment: VerticalAlignment.Center
			textStyle.color: Color.create("#88ffffff")
			visible: threadListPage.threadCount === 0
		}

		// Shown while auto-fill keeps loading toward Discord's reported thread count.
		Label {
			text: threadListPage.searchTotal > 0 ? qsTr("Loading threads... ") + threadListPage.threadCount + " / " + threadListPage.searchTotal : qsTr("Loading threads...")
			horizontalAlignment: HorizontalAlignment.Center
			textStyle.fontSize: FontSize.XSmall
			textStyle.color: Color.create("#88ffffff")
			visible: threadListPage.autoFill && threadListPage.threadCount > 0
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
			text: threadListPage.searchLoading ? qsTr("Loading...") : (threadListPage.incomplete ? qsTr("Load more threads") + " (" + threadListPage.threadCount + " / " + threadListPage.searchTotal + ")" : qsTr("Load more threads"))
			// Center alignment so the button sizes to its text instead of filling the width.
			horizontalAlignment: HorizontalAlignment.Center
			topMargin: ui.du(1.0)
			bottomMargin: ui.du(1.5)
			enabled: !threadListPage.searchLoading
			visible: !threadListPage.autoFill && (threadListPage.searchHasMore || threadListPage.creationHasMore || threadListPage.incomplete)

			onClicked: {
				threadListPage.loadMoreThreads()
			}
		}
	}
}
