#ifndef CLIENT_HPP_
#define CLIENT_HPP_

#include <QHash>
#include <QObject>
#include <QQueue>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include "client/AvatarState.hpp"
#include "client/ClientState.hpp"
#include "models/Models.hpp"

class AppStore;
class AvatarCacheWorker;
class DiscordGatewayWorker;
class DiscordNetworkWorker;
class QThread;

class CacheManager;
class AvatarManager;
class GatewayHandler;
class ItemMapper;
class SortUtils;
class HubIntegration;

class DiscordClient : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY loggedInChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
  Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)

public:
  explicit DiscordClient(QObject *parent = 0);
  explicit DiscordClient(AppStore *store, QObject *parent = 0);
  virtual ~DiscordClient();

  Q_INVOKABLE void login(const QString &token);
  Q_INVOKABLE void loginWithPassword(const QString &email,
                                     const QString &password);
  Q_INVOKABLE void submitMfaCode(const QString &ticket,
                                 const QString &loginInstanceId,
                                 const QString &code);
  Q_INVOKABLE void submitCaptchaKey(const QString &captchaKey);
  Q_INVOKABLE void autoLogin();
  Q_INVOKABLE void logout();
  Q_INVOKABLE void loadMainData();
  Q_INVOKABLE void loadGuildIcon(const QString &guildId);
  Q_INVOKABLE void loadMoreDmChannels();
  Q_INVOKABLE void loadDmAvatar(const QString &channelId);
  Q_INVOKABLE void loadGuildChannels(const QString &guildId);
  Q_INVOKABLE void loadMoreGuildChannels();
  Q_INVOKABLE void selectHome();
  Q_INVOKABLE void selectGuild(const QString &guildId);
  Q_INVOKABLE void selectChannel(const QString &channelId);
  // guildId đã cache cho 1 channelId cụ thể, nếu channel đó từng được
  // select/load trong phiên hiện tại (m_chatGuildByChannelId, xem
  // GuildChannels.cpp::selectChannel()). Trả về rỗng nếu không tìm thấy
  // — nghĩa là channelId đó là DM (guild channel luôn được insert vào map
  // này ngay khi select), hoặc guild channel chưa từng mở trong phiên
  // này (trường hợp mở app từ Hub khi app đang cold-start — xem
  // ApplicationUI::onInvoked()).
  Q_INVOKABLE QString guildIdForChannel(const QString &channelId) const;

  bool loggedIn() const;
  bool busy() const;
  QString statusText() const;

Q_SIGNALS:
  void loginSucceeded();
  void loginFailed(const QString &message);
  void mfaRequired(const QString &ticket, const QString &loginInstanceId);
  void captchaRequired(const QString &requestKind, const QString &sitekey,
                       const QString &rqdata, const QString &rqtoken);
  void loggedInChanged(bool loggedIn);
  void busyChanged(bool busy);
  void statusTextChanged(const QString &statusText);

public Q_SLOTS:
  void clearAvatarCacheState();
  Q_INVOKABLE void subscribeToGuildChannel(const QString &channelId,
                                           const QString &guildId);
  Q_INVOKABLE void loadInitialChatMessages(const QString &channelId,
                                           const QString &guildId);
  Q_INVOKABLE void loadOlderChatMessages(const QString &channelId,
                                         const QString &beforeMessageId);
  Q_INVOKABLE void sendChatMessage(const QString &channelId,
                                   const QString &content, const QString &nonce,
                                   const QString &replyMessageId,
                                   const QStringList &attachmentPaths);
  Q_INVOKABLE QString avatarSourceForUser(const QString &userId) const;
  Q_INVOKABLE void loadUserAvatar(const QString &userId,
                                  const QString &avatarHash);
  Q_INVOKABLE void editChatMessage(const QString &channelId,
                                   const QString &messageId,
                                   const QString &content);
  Q_INVOKABLE void deleteChatMessage(const QString &channelId,
                                     const QString &messageId);

private Q_SLOTS:
  void onRestLoginSucceeded(const QVariantMap &user, const QString &token);
  void onRestLoginFailed(const QString &message);
  void onRestMfaRequired(const QString &ticket, const QString &loginInstanceId);
  void onRestCaptchaRequired(const QString &requestKind, const QString &sitekey,
                             const QString &rqdata, const QString &rqtoken);
  void onGuildsLoaded(const QVariantList &guilds);
  void onDmChannelsLoaded(const QVariantList &channels);
  void onGuildChannelsLoaded(const QString &guildId,
                             const QVariantList &channels);
  void onChannelMessagesLoaded(const QString &channelId,
                               const QString &beforeMessageId,
                               const QVariantList &messages);
  void onChannelMessageSent(const QString &channelId, const QString &nonce,
                            const QVariantMap &message);
  void onChannelMessageEdited(const QString &channelId,
                              const QVariantMap &message);
  void onChannelMessageDeleted(const QString &channelId,
                               const QString &messageId);
  void onChatRequestFailed(const QString &operation, const QString &channelId,
                           const QString &nonce, const QString &message);
  void onDataRequestFailed(const QString &message);
  void onAvatarDownloaded(const QString &userId, const QString &localPath);
  void onAvatarDownloadFailed(const QString &userId, const QString &message);
  void onGuildIconDownloaded(const QString &guildId, const QString &localPath);
  void onGuildIconDownloadFailed(const QString &guildId,
                                 const QString &message);
  void onAvatarCacheHit(const QString &userId, const QString &path);
  void onAvatarCacheMiss(const QString &userId, const QString &path);
  void onGuildIconCacheHit(const QString &guildId, const QString &path);
  void onGuildIconCacheMiss(const QString &guildId, const QString &path);
  void onGatewayDispatch(const QString &eventName, const QVariantMap &payload);
  void onGatewayReady(const QString &sessionId);
  void onGatewayError(const QString &message);
  void onGatewayClosed();
  void onGatewayGuildsAndDmsReady(const QVariantList &guilds,
                                  const QVariantList &allDmChannels,
                                  const QVariantList &visibleDmChannels,
                                  const QStringList &orderedGuildIds,
                                  const QVariantMap &dmPresenceByUserId);
  void flushGatewayUiUpdates();
  void savePendingGuildsCache();
  void savePendingDmChannelsCache();

  void moveDmToTop(const QString &channelId, const QString &lastMessageId);
  int guildMentionCount(const QString &guildId) const;
  void updateDmPresence(const QString &userId, const QString &status);
  bool applyPendingDmPresences();
  bool applyGuildOrderFromGatewayPayload(const QVariantMap &payload);
  void sortGuilds();
  void sortDmChannels();
  void rebuildDmChannelIndexes();
  void updateStoreWithGuildsAndDms();
  void saveGuildsCache() const;
  void saveDmChannelsCache() const;

private:
  void setLoggedIn(bool loggedIn);
  void setBusy(bool busy);
  void setStatusText(const QString &statusText);
  void saveToken();
  void clearSavedToken();
  void loadGuilds();
  void indexDmChannelRecipients(const QVariantMap &channel);
  void rebuildDmRecipientIndex();
  void updateGuildMentionCount(const QString &guildId, int mentionCount);
  void updateGuildUnread(const QString &guildId, bool unread);
  bool updateGuildChannelUnread(const QString &channelId, bool unread);
  bool updateGuildChannelMentionCount(const QString &channelId,
                                      int mentionCount);
  void appendVisibleGuildChannels();
  void scheduleGuildsCacheSave();
  void scheduleDmChannelsCacheSave();
  void updateDataLoading();
  void initializeNetworkWorker();
  void shutdownNetworkWorker();
  void initializeGatewayWorker();
  void shutdownGatewayWorker();
  void initializeAvatarCacheWorker();
  void shutdownAvatarCacheWorker();
  void initializeManagers();
  void updateAvatarManagerWorkers();
  void syncStateToNetworkWorker();
  void syncGatewayOrderingStateToWorker();
  void syncGatewayMessageFilterStateToWorker();

  AppStore *m_store;
  QThread *m_networkThread;
  DiscordNetworkWorker *m_networkWorker;
  QThread *m_gatewayThread;
  DiscordGatewayWorker *m_gatewayWorker;
  QThread *m_avatarCacheThread;
  AvatarCacheWorker *m_avatarCacheWorker;

  CacheManager *m_cacheManager;
  AvatarManager *m_avatarManager;
  GatewayHandler *m_gatewayHandler;
  ItemMapper *m_itemMapper;
  SortUtils *m_sortUtils;
  HubIntegration *m_hubIntegration;

  ClientState m_state;
  AvatarState m_avatarState;

  QQueue<QVariantMap> &m_pendingAvatars;
  QQueue<QVariantMap> &m_pendingGuildIcons;
  QVariantMap &m_avatarCacheRequests;
  QVariantMap &m_guildIconCacheRequests;
  QVariantMap &m_avatarSourcesByUserId;
  QVariantMap &m_dmChannelsById;
  QVariantMap &m_dmChannelIdsByRecipientId;
  QVariantMap &m_allDmChannelIndexById;
  QVariantMap &m_visibleDmChannelIndexById;
  QString &m_loadingAvatarUserId;
  QString &m_loadingAvatarUserId2;
  QString &m_loadingGuildIconId;
  QString &m_loadingGuildIconId2;
  QStringList &m_queuedAvatarUserIds;
  QStringList &m_loadedAvatarUserIds;
  QStringList &m_queuedGuildIconIds;
  QStringList &m_loadedGuildIconIds;
  QStringList &m_orderedGuildIds;
  QVariantList &m_guildFolders;
  QVariantMap &m_dmPresenceByUserId;
  QString m_token;
  QString &m_lastGuildId;
  QString &m_lastDmChannelId;
  QString &m_selectedGuildId;
  QVariantList &m_guilds;
  QVariantList &m_allDmChannels;
  QVariantList &m_dmChannels;
  QVariantList &m_allGuildChannels;
  QVariantList &m_visibleGuildChannels;
  QVariantMap &m_pendingMentionCountsByGuildId;
  QVariantMap &m_pendingMentionCountsByChannelId;
  QStringList &m_pendingUnreadGuildIds;
  QStringList &m_pendingUnreadChannelIds;
  QStringList &m_pendingDmPresenceUserIds;
  QHash<QString, QString> m_chatGuildByChannelId;
  int &m_visibleDmChannelCount;
  int &m_visibleGuildChannelCount;
  bool &m_loadingGuilds;
  bool &m_loadingDmChannels;
  bool &m_loadingGuildChannels;
  bool &m_guildsHasMore;
  bool &m_dmChannelsHasMore;
  bool &m_guildChannelsHasMore;
  bool m_loggedIn;
  bool m_busy;
  bool &m_gatewayUiUpdateQueued;
  bool &m_pendingDmUiUpdate;
  bool &m_guildsCacheSaveQueued;
  bool &m_dmCacheSaveQueued;
  bool &m_bootstrapCacheLoaded;
  QString m_statusText;
};

#endif /* CLIENT_HPP_ */
