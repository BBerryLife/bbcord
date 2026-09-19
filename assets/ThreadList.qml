import bb.cascades 1.4

// Active thread list for a channel. Doesn't use a dedicated C++
// controller since the data is already available via
// discordClient.threadsForChannel(channelId) (Q_INVOKABLE, see
// GuildChannels.cpp::threadsForChannel()) - simple enough to load
// straight into an ArrayDataModel at the QML level, no need for another
// ListItemProvider/Controller just to relay the same data.
Page {
	id: threadListPage

	property string channelId: ""
	property string guildId: ""
	property string channelName: "general"
	property alias title: titleBar.title

	// Fix: "visible: threadDataModel.size() === 0" (used previously)
	// calls size() directly inside a declarative binding expression -
	// same bug class as "enabled: field.text.length" in
	// LoginPage.qml/MfaSheet.qml (see that fix history): a declarative
	// binding isn't guaranteed to re-evaluate when the model's CONTENT
	// changes via append()/clear(), it's only reliable when depending
	// on a property with a proper NOTIFY signal. Uses this explicit
	// property instead, updated right after every reload(), to make
	// sure the UI re-renders at the right time.
	property int threadCount: 0

	// A second line of defense, independent of whether disconnect()
	// runs at the right time (see cleanup() below) - in case the
	// NavigationPane gets popped some other way (e.g. the system's
	// physical back button) without going through either backRequested
	// or threadSelected, where cleanup() is called explicitly.
	// Fully self-managed here (not relying on some framework
	// "isValid"/lifecycle API I'm not sure exists), set to false in
	// cleanup() and checked right at the top of reload() before
	// touching threadDataModel.
	property bool _isActive: true

	// Archived threads: kept entirely separate from threadDataModel
	// (active threads) - Discord returns these as two different
	// concepts through two different sources (passive gateway vs
	// on-demand REST), mixing them would easily confuse which state is
	// active/archived. archivedHasMore controls showing/hiding the
	// "Load older threads" button; archivedCursor is the oldest loaded
	// thread's id, used as "before" for the next page.
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

	// Reloads the thread list every time this sheet is opened, and
	// whenever AppStore.channelThreadsByParentId changes (a new thread
	// created, another active thread loaded) while the sheet is open —
	// avoids the list going stale if the backend updates threads while
	// the user is viewing the sheet.
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
		// Same caveat as ChannelMemberList.qml/MainPage.qml:
		// createObject() runs onCreationCompleted() BEFORE the
		// channelId property gets assigned by the caller (see
		// MainPage.qml::openChat()) - don't call reload() here, call it
		// again explicitly after the property has a real value
		// (threadListPage.requestThreadsNow(), called from
		// MainPage.qml).
		appStore.channelThreadsChanged.connect(threadListPage.reload)
		discordClient.archivedThreadsLoaded.connect(threadListPage.onArchivedThreadsResult)
	}

	// Fix: Cascades' "Page" has NO "onDestruction" signal (that's a
	// QtQuick Component API, doesn't exist here) - declaring it breaks
	// parsing for the whole file (confirmed bug via real logs before).
	// Used to use onBackRequested to disconnect, BUT that signal only
	// fires on the titleBar's actual back button - when the user taps a
	// thread (threadSelected), MainPage.qml calls navigationPane.pop()
	// directly WITHOUT going through backRequested, so disconnect never
	// runs in that case. Observed real-world effect: ReferenceError
	// "Can't find variable: threadListPage" when THREAD_LIST_SYNC fires
	// after the page has already been popped/destroyed, trying to call
	// reload() on an object that no longer exists. Pulled cleanup into
	// its own function, called explicitly from EVERY place that pops
	// this page (see MainPage.qml), not just the back button. Applies
	// the same lesson to archivedThreadsLoaded too (same risk).
	function cleanup() {
		threadListPage._isActive = false
		appStore.channelThreadsChanged.disconnect(threadListPage.reload)
		discordClient.archivedThreadsLoaded.disconnect(threadListPage.onArchivedThreadsResult)
	}

	function requestThreadsNow() {
		// Fix: there's NO discordClient.requestThreadsForChannel() to
		// call anymore - that function (channel-level REST) was
		// removed since Discord confirmed rejecting it with the exact
		// same "Only bots can use this endpoint." error (code 20002),
		// same as the guild-level endpoint tried earlier. Threads now
		// arrive entirely PASSIVELY through the gateway
		// (THREAD_LIST_SYNC event, handled in
		// Client.cpp::onGatewayDispatch(), flowing in automatically
		// when the guild gets subscribed) - here it just needs to read
		// the existing cache, there's nothing left to actively
		// "request". reload() is also already called automatically
		// every time appStore.channelThreadsChanged fires (connected in
		// onCreationCompleted), so the list will self-update on the
		// next THREAD_LIST_SYNC, even after this call.
		reload()
	}

	// Unlike active threads (passive via gateway) - archived threads
	// must be ACTIVELY requested via REST (endpoint
	// /channels/{id}/threads/archived/public, different from the two
	// bot-only "active threads" endpoints confirmed earlier). Called
	// when tapping "Load older threads", or automatically the first
	// time if the channel has never had an archive-fetch (archivedCursor
	// empty and not currently loading).
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
		// Appended directly into threadDataModel (the same list as
		// active threads) instead of a separate ListView/model - much
		// simpler than building distinct "Active"/"Archived" section
		// headers in Cascades (no reliable precedent in this codebase
		// to do that correctly), and the user just needs to see ALL
		// posts, without necessarily needing a strict active/archived
		// split in the UI.
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

			// Fix: "onTouch" isn't a valid signal on ListItemComponent
			// (that's just a factory defining the delegate, not itself
			// a control that receives touch events) - declaring it
			// breaks parsing the same way the earlier "onDestruction"
			// bug did ("Cannot assign to non-existent property"),
			// ThreadList.qml fails to create. The whole codebase
			// (ServerList.qml, ChatCard.qml, DmList.qml) uses
			// ListView.onTriggered to handle tapping an item - follows
			// that same pattern instead of onTouch on the delegate.
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

		// Posts older than the auto-archive window (Discord archives
		// them automatically, no longer in active threads/
		// THREAD_LIST_SYNC - see the requestArchivedThreads() comment
		// in Client.hpp) - tap to actively fetch more via REST,
		// paginated using archivedCursor.
		Button {
			text: threadListPage.archivedLoading ? qsTr("Loading...") : qsTr("Load older threads")
			horizontalAlignment: HorizontalAlignment.Fill
			topMargin: ui.du(1.0)
			// Fix: was sitting flush against the screen edges (left,
			// right, bottom) with only topMargin set - added matching
			// side/bottom margins so it has breathing room on all
			// sides like the rest of the app's buttons.
			leftMargin: ui.du(2.0)
			rightMargin: ui.du(2.0)
			bottomMargin: ui.du(1.5)
			enabled: !threadListPage.archivedLoading
			// Shown even when threadCount === 0 (a channel may have no
			// active threads left but still have archived ones) - only
			// fully hidden once confirmed there are no more pages
			// (archivedHasMore turns false, see
			// onArchivedThreadsResult()). archivedCursor === "" means
			// NEVER tapped/loaded before - still shown so the user can
			// tap it proactively.
			visible: threadListPage.archivedCursor !== "" ? threadListPage.archivedHasMore : true

			onClicked: {
				threadListPage.loadOlderThreads()
			}
		}
	}
}
