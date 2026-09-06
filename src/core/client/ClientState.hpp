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
  // Threads KHÔNG được lưu trong allGuildChannels/visibleGuildChannels:
  // SortUtils::sortedAccessibleGuildChannels() chỉ lồng 1 channel dưới
  // channel khác nếu parentId trỏ tới 1 CATEGORY - với thread, parentId
  // trỏ tới channel text/forum cha, nên sẽ bị coi nhầm là root channel
  // nếu trộn chung. Lưu riêng, group sẵn theo parentId (channel chứa
  // thread đó) để UI query đúng "các thread của channel đang mở" mà
  // không cần lọc lại toàn bộ danh sách mỗi lần.
  QVariantMap channelThreadsByParentId;
  // Channel/thread hiện đang mở trong ChatController - dùng để phân biệt
  // "đang xem 1 thread" khỏi "đang xem channel cha của nó", cho những chỗ
  // UI cần biết (vd. nút quay lại channel cha, hoặc tiêu đề màn hình).
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
