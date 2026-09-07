#ifndef CLIENTSTATE_HPP_
#define CLIENTSTATE_HPP_

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class ClientState {
public:
  ClientState();

  void resetSession();
  void resetGuildChannels();
  void resetDmChannels();
  void resetGatewayPendingUpdates();

  QVariantMap dmChannelsById;
  QVariantMap dmChannelIdsByRecipientId;
  QVariantMap allDmChannelIndexById;
  QVariantMap visibleDmChannelIndexById;
  QStringList orderedGuildIds;
  QVariantList guildFolders;
  QVariantMap dmPresenceByUserId;
  QString lastGuildId;
  QString lastDmChannelId;
  QString selectedGuildId;
  QVariantList guilds;
  QVariantList allDmChannels;
  QVariantList dmChannels;
  QVariantList allGuildChannels;
  QVariantList visibleGuildChannels;
  // Threads are NOT stored in allGuildChannels/visibleGuildChannels:
  // SortUtils::sortedAccessibleGuildChannels() only nests a channel
  // under another if parentId points to a CATEGORY - for threads,
  // parentId points to the parent text/forum channel, so mixing them
  // in would misclassify threads as root channels. Stored separately,
  // pre-grouped by parentId (the channel that owns the thread) so the
  // UI can query "threads of the currently open channel" without
  // re-filtering the whole list each time.
  QVariantMap channelThreadsByParentId;
  // Channel/thread currently open in ChatController - used to tell
  // "viewing a thread" apart from "viewing its parent channel", for
  // places the UI needs to know (e.g. back-to-parent button, title).
  QString activeThreadChannelId;
  QString activeThreadParentId;
  QVariantMap pendingMentionCountsByGuildId;
  QVariantMap pendingMentionCountsByChannelId;
  QStringList pendingUnreadGuildIds;
  QStringList pendingUnreadChannelIds;
  QStringList pendingDmPresenceUserIds;
  int visibleDmChannelCount;
  int visibleGuildChannelCount;
  bool loadingGuilds;
  bool loadingDmChannels;
  bool loadingGuildChannels;
  bool guildsHasMore;
  bool dmChannelsHasMore;
  bool guildChannelsHasMore;
  bool gatewayUiUpdateQueued;
  bool pendingDmUiUpdate;
  bool guildsCacheSaveQueued;
  bool dmCacheSaveQueued;
  bool bootstrapCacheLoaded;
};

#endif /* CLIENTSTATE_HPP_ */
