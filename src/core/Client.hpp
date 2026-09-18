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
  Q_INVOKABLE void loadMoreGuildChannels();  Q_INVOKABLE void selectHome();
  Q_INVOKABLE void selectGuild(const QString &guildId);
  Q_INVOKABLE void selectChannel(const QString &channelId);
  // Cached guildId for a specific channelId, if that channel was ever
  // selected/loaded this session (m_chatGuildByChannelId, see
  // GuildChannels.cpp::selectChannel()). Returns empty if not found —
  // meaning that channelId is a DM (guild channels are always inserted
  // into this map on select), or a guild channel never opened this
  // session (e.g. opening the app from the Hub during a cold start —
  // see ApplicationUI::onInvoked()).
  Q_INVOKABLE QString guildIdForChannel(const QString &channelId) const;
  // Active thread list (mapped through ItemMapper, same shape as items
  // in guildChannels) for a specific channel. Data comes entirely
  // through the GATEWAY (THREAD_LIST_SYNC event, see
  // Client.cpp::onGatewayDispatch()) - NOT via REST, since every REST
  // "active threads" endpoint (guild-level and channel-level alike) is
  // rejected by Discord for user tokens ({"message": "Only bots can use
  // this endpoint.", "code": 20002}, confirmed through repeated
  // testing). Can be empty if Discord hasn't sent THREAD_LIST_SYNC for
  // this channel this session yet (this event fires per-guild on
  // subscribe, not on-demand from the app).
  Q_INVOKABLE QVariantList threadsForChannel(const QString &channelId) const;
  // Threads auto-archived by Discord are no longer in
  // threadsForChannel()/active threads - fetch them separately via REST
  // (channel-level endpoint /channels/{id}/threads/archived/public,
  // confirmed working with user tokens, unlike the two "active threads"
  // endpoints which are bot-only). "beforeCursor" empty = first page;
  // pass the oldest loaded thread's id to fetch an older page. Result
  // comes back via the archivedThreadsLoaded signal, NOT stored in
  // threadsForChannel() (kept separate from active threads, to avoid
  // mixing the two concepts).
  Q_INVOKABLE void requestArchivedThreads(const QString &channelId,
                                          const QString &beforeCursor);

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
  // QML uses a Connections{} on this signal itself (unlike
  // threadsForChannel() - a read-on-demand function) since
  // requestArchivedThreads() is an async REST request, needing the
  // result pushed back to the UI when it arrives, not readable
  // immediately.
  void archivedThreadsLoaded(const QString &channelId,
                             const QVariantList &threads, bool hasMore);

public Q_SLOTS:
  void clearAvatarCacheState();
  Q_INVOKABLE void subscribeToGuildChannel(const QString &channelId,
                                           const QString &guildId);
  // Force-resends the member-list-sync (op:14) request through
  // DiscordGateway::sendMemberListSync() — NOT blocked by
  // subscribeToGuildChannel()'s dedup cache. Called from
  // MemberListController when the Members sheet opens, since the
  // channel has almost always already had its subscribeToGuildChannel()
  // "consumed" earlier when the user opened it (message lazy-load),
  // which would cause sendLazyRequest() to be dropped by dedup if
  // called again with the same key.
  Q_INVOKABLE void requestMemberListSync(const QString &channelId,
                                         const QString &guildId);
  // Called when the Members sheet closes (MemberListController::
  // releaseMemberList()). Forwards to
  // DiscordGateway::clearMemberListSync() to stop auto-resending SYNC
  // for this channel if the gateway reconnects AFTER the user has
  // already left the tab (see comment at Gateway.hpp:
  // m_activeMemberListGuildId/ChannelId).
  Q_INVOKABLE void clearMemberListSync();
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
  // Fix: needed because READY.guilds[i] has no "members" field on this
  // user-token gateway - see fetchSelfGuildMember()/
  // mergeGuildRolesIntoCache() comments. Re-derives accessible channels
  // once the real role list is in, same as onGuildChannelsLoaded() does
  // after a fresh channel fetch (see GuildChannels.cpp).
  void onSelfGuildMemberLoaded(const QString &guildId,
                               const QStringList &roleIds);
  void onArchivedThreadsLoaded(const QString &channelId,
                               const QVariantList &threads, bool hasMore);
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
  // Fix: re-runs PermissionUtils::canViewChannel() over
  // m_rawSelectedGuildChannels (the unfiltered, already-mapped channel
  // items for m_selectedGuildId) and rebuilds m_allGuildChannels from
  // that - shared between onGuildChannelsLoaded() (fresh channel data)
  // and onSelfGuildMemberLoaded() (fresh role data only, channels
  // unchanged) since either one arriving can change which channels are
  // accessible. No-ops if guildId isn't the currently selected guild.
  void recomputeAccessibleGuildChannels(const QString &guildId);
  // Shared between GUILD_CREATE (payload.threads - threads already
  // present as soon as a guild becomes available, per official Discord
  // docs) and THREAD_LIST_SYNC (fires on "gains access to a channel",
  // rarer during normal app usage) - both events carry the same
  // "threads"/"channel_ids" shape, so the map + cache-merge logic is
  // combined in one place, see Client.cpp::onGatewayDispatch().
  void mergeThreadsIntoCache(const QVariantList &rawThreads,
                             const QVariantList &channelIdsToClear);
  // Shared between READY (payload.guilds[i] - the real source of guild
  // roles/self-member-roles on this user-token gateway, since
  // GUILD_CREATE never fires on initial load - see the comment above
  // the "READY" handling in onGatewayDispatch()) and GUILD_CREATE
  // (kept as a fallback for the join-a-new-guild-while-running case,
  // where GUILD_CREATE genuinely is sent per Discord's docs). Parses
  // "roles" and the current user's entry in "members" out of one raw
  // guild object and writes them to m_store, same roleMap/roleIds
  // shape either caller used to build inline before this was factored
  // out.
  void mergeGuildRolesIntoCache(const QString &guildId,
                                const QVariantMap &guildRaw);
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
  QVariantList &m_rawSelectedGuildChannels;
  QVariantList &m_visibleGuildChannels;
  QVariantMap &m_channelThreadsByParentId;
  QString &m_activeThreadChannelId;
  QString &m_activeThreadParentId;
  QVariantMap &m_pendingMentionCountsByGuildId;
  QVariantMap &m_pendingMentionCountsByChannelId;
  QStringList &m_pendingUnreadGuildIds;
  QStringList &m_pendingUnreadChannelIds;
  QStringList &m_pendingDmPresenceUserIds;
  QHash<QString, QString> m_chatGuildByChannelId;
  // Most recent channel requested via requestMemberListSync().
  // GUILD_MEMBER_LIST_UPDATE (op 14 - unofficial protocol) doesn't
  // guarantee a root-level "channel_id" field, and the "id" field (list
  // id) is often "everyone" rather than the channel id — not reliable
  // for mapping data to the right channel. Since the current flow only
  // tracks one channel's member list at a time (the Members sheet), the
  // most recently requested channel is used as a last-resort fallback
  // when the payload can't self-identify its channel - see
  // GUILD_MEMBER_LIST_UPDATE handling in Client.cpp.
  QString m_pendingMemberListChannelId;
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
