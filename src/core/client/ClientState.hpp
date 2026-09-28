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
  // Raw mapped channel items (permissionOverwrites intact, before the "accessible"
  // pass) of the selected guild. Kept apart from allGuildChannels so
  // PermissionUtils::canViewChannel() can be re-run once real roles arrive from
  // fetchSelfGuildMember() (see onSelfGuildMemberLoaded()).
  QVariantList rawSelectedGuildChannels;
  QVariantList visibleGuildChannels;
  // Threads are stored separately, pre-grouped by parentId (the owning channel):
  // SortUtils::sortedAccessibleGuildChannels() only nests under CATEGORY parents, so
  // mixing them into allGuildChannels would misclassify threads as root channels.
  QVariantMap channelThreadsByParentId;
  // Channel/thread currently open in ChatController; tells a thread from its parent (e.g. back button, title).
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
