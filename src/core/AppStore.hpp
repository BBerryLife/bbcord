#ifndef AppStore_HPP_
#define AppStore_HPP_

#include <QMap>
#include <QSet>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include "client/MessageCache.hpp"

#include "models/Models.hpp"

class AppStore : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY loggedInChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
  Q_PROPERTY(bool dataLoading READ dataLoading NOTIFY dataLoadingChanged)
  Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
  Q_PROPERTY(
      QString currentUserName READ currentUserName NOTIFY currentUserChanged)
  Q_PROPERTY(QString currentUserId READ currentUserId NOTIFY currentUserChanged)
  Q_PROPERTY(
      QString currentUserTag READ currentUserTag NOTIFY currentUserChanged)
  Q_PROPERTY(QString currentUserAvatarSource READ currentUserAvatarSource NOTIFY
                 currentUserChanged)
  Q_PROPERTY(QVariantList guilds READ guilds NOTIFY guildsChanged)
  Q_PROPERTY(
      QVariantList guildFolders READ guildFolders NOTIFY guildFoldersChanged)
  Q_PROPERTY(QVariantList dmChannels READ dmChannels NOTIFY dmChannelsChanged)
  Q_PROPERTY(
      QVariantList guildChannels READ guildChannels NOTIFY guildChannelsChanged)
  // Active threads for the currently selected guild, grouped by parent
  // channel (key = parent channelId, value = list of thread items). QML
  // reads this map via discordClient.threadsForChannel(channelId)
  // instead of binding directly to this Q_PROPERTY — mainly exposed to
  // fire the changed signal (channelThreadsChanged) so ChatCard knows
  // to call threadsForChannel() again.
  Q_PROPERTY(QVariantMap channelThreadsByParentId READ channelThreadsByParentId
                 NOTIFY channelThreadsChanged)
  Q_PROPERTY(
      QString selectedGuildId READ selectedGuildId NOTIFY selectionChanged)
  Q_PROPERTY(
      QString selectedChannelId READ selectedChannelId NOTIFY selectionChanged)
  Q_PROPERTY(QVariantList currentChannelMessages READ currentChannelMessages
                 NOTIFY currentChannelMessagesChanged)
  Q_PROPERTY(bool chatInitialLoaded READ chatInitialLoaded NOTIFY
                 currentChatStateChanged)
  Q_PROPERTY(bool chatLoadingInitial READ chatLoadingInitial NOTIFY
                 currentChatStateChanged)
  Q_PROPERTY(bool chatLoadingBefore READ chatLoadingBefore NOTIFY
                 currentChatStateChanged)
  Q_PROPERTY(bool chatHasMoreBefore READ chatHasMoreBefore NOTIFY
                 currentChatStateChanged)
  Q_PROPERTY(QString chatOldestMessageId READ chatOldestMessageId NOTIFY
                 currentChatStateChanged)
  Q_PROPERTY(QString chatNewestMessageId READ chatNewestMessageId NOTIFY
                 currentChatStateChanged)

public:
  explicit AppStore(QObject *parent = 0);

  bool loggedIn() const;
  bool busy() const;
  bool dataLoading() const;
  QString statusText() const;
  QString currentUserName() const;
  QString currentUserId() const;
  QString currentUserTag() const;
  QString currentUserAvatarSource() const;
  QVariantList guilds() const;
  QVariantList guildFolders() const;
  QVariantList dmChannels() const;
  QVariantList guildChannels() const;
  QVariantMap channelThreadsByParentId() const;
  QString selectedGuildId() const;
  QString selectedChannelId() const;
  QVariantList currentChannelMessages() const;
  bool chatInitialLoaded() const;
  bool chatLoadingInitial() const;
  bool chatLoadingBefore() const;
  bool chatHasMoreBefore() const;
  QString chatOldestMessageId() const;
  QString chatNewestMessageId() const;

  Q_INVOKABLE void selectHome();
  Q_INVOKABLE void selectGuild(const QString &guildId);
  Q_INVOKABLE void selectChannel(const QString &channelId);
  // Fix: clears ONLY the channel selection, keeping the guild selection
  // intact - needed for backRequested on the chat page (see
  // ChatController::closeChannel()/MainPage.qml's openChat()): the
  // user backed out of the channel but is still sitting inside that
  // guild's channel list, so selectGuild()/selectHome() (which also
  // clear/change the guild) are the wrong tool here. Before this
  // existed, backing out of a channel left m_selectedChannelId
  // pointing at the channel the user just left, so every
  // "is the user currently looking at this channel" check elsewhere
  // (the guild-badge recompute, GatewayHandler's isCurrentlyOpenChannel
  // guard, etc.) kept treating a non-mention message that arrived
  // AFTER the user left as if they were still reading it live - no
  // unread mark, so the channel/server badge never lit up for it,
  // confirmed as a real bug via logs.
  Q_INVOKABLE void clearChannelSelection();
  Q_INVOKABLE QVariantList messagesForChannel(const QString &channelId) const;
  Q_INVOKABLE bool isChatInitialLoaded(const QString &channelId) const;
  Q_INVOKABLE bool isChatLoadingInitial(const QString &channelId) const;
  Q_INVOKABLE bool isChatLoadingBefore(const QString &channelId) const;
  Q_INVOKABLE bool hasMoreChatBefore(const QString &channelId) const;
  QStringList loadedChatChannelIds() const;
  Q_INVOKABLE QString oldestChatMessageId(const QString &channelId) const;
  Q_INVOKABLE QString newestChatMessageId(const QString &channelId) const;
  Q_INVOKABLE void clearSession();

  // Current user's role IDs in a specific guild — loaded from the
  // "member" field (self member object) of the GUILD_CREATE payload,
  // see DiscordClient::onGatewayGuildCreate() (Guilds.cpp). Used to
  // determine if a message's role-mention (mention_roles) targets us,
  // for the Hub notification feature. Returns an empty list if there's
  // no data for that guild yet (no GUILD_CREATE received, or not
  // logged in).
  Q_INVOKABLE QStringList currentUserRoleIdsForGuild(const QString &guildId) const;
  // Fix: whether channelId has a message that arrived while some OTHER
  // guild was open (or before any guild was ever opened this session)
  // - updateGuildChannelUnread() in GuildChannels.cpp can only mark a
  // channel unread if it's already present in the currently-loaded
  // m_allGuildChannels/m_visibleGuildChannels, which isn't the case
  // for a channel in a guild the user hasn't opened yet. This tracks
  // that fact independently so onGuildChannelsLoaded() can apply it
  // once the channel's guild actually gets loaded - see
  // markChannelUnread()/clearChannelUnread() below.
  Q_INVOKABLE bool isChannelMarkedUnread(const QString &channelId) const;

  // Full role list (id/name/color/position/hoisted) for a guild, loaded
  // from the "roles" field of the GUILD_CREATE payload — see
  // DiscordClient::onGatewayGuildCreate() (Guilds.cpp). Each item is a
  // QVariantMap with keys: id, name, color ("#RRGGBB" or empty),
  // position, hoisted. Used by ChannelMemberList.qml to show role
  // name/color for members. Returns an empty list if there's no data
  // for that guild yet.
  Q_INVOKABLE QVariantList guildRolesForGuild(const QString &guildId) const;

  // Flattened member list (see DiscordMember in Models.hpp) for a
  // channel, loaded from the GUILD_MEMBER_LIST_UPDATE opcode ("SYNC"
  // op) — see DiscordClient::onGatewayDispatch() (Client.cpp). Keyed by
  // channelId (not guildId) since Discord scopes the member list by
  // channel permission overwrites, not the whole guild. Each item is a
  // QVariantMap with keys: userId, displayName, avatarUrl, status,
  // primaryRoleId. Returns an empty list if there's no data yet (sheet
  // never opened, or SYNC hasn't arrived).
  Q_INVOKABLE QVariantList memberListForChannel(const QString &channelId) const;

public Q_SLOTS:
  void clearMediaCacheState();

  void setLoggedIn(bool loggedIn);
  void setBusy(bool busy);
  void setDataLoading(bool dataLoading);
  void setStatusText(const QString &statusText);
  void setCurrentUser(const DiscordUser &user);
  void setCurrentUserAvatarSource(const QString &avatarSource);
  void setGuilds(const QVariantList &guilds);
  void setGuildFolders(const QVariantList &folders);
  void reorderGuilds(const QVariantList &guilds);
  void updateGuildIcon(const QString &guildId, const QString &iconSource);
  void updateDmAvatar(const QString &channelId, const QString &avatarSource);
  void updateDmAvatar2(const QString &channelId, const QString &avatarSource);
  void updateDmStatus(const QString &channelId, const QString &status,
                      const QString &statusColor);
  void notifyChatAvatarChanged(const QString &userId,
                               const QString &avatarSource);
  void setDmChannels(const QVariantList &dmChannels);
  void appendDmChannels(const QVariantList &channels);
  void setGuildChannels(const QVariantList &channels);
  void setChannelThreadsByParentId(const QVariantMap &threadsByParentId);
  void appendGuildChannels(const QVariantList &channels);
  void setChatLoadingInitial(const QString &channelId, bool loading);
  void setChatLoadingBefore(const QString &channelId, bool loading);
  void setChatHasMoreBefore(const QString &channelId, bool hasMore);
  void setInitialChatMessages(const QString &channelId, const QString &guildId,
                              const QList<DiscordMessage> &messages,
                              bool hasMoreBefore);
  void prependOlderChatMessages(const QString &channelId,
                                const QList<DiscordMessage> &messages,
                                bool hasMoreBefore);
  void addOrReplaceChatMessage(const DiscordMessage &message);
  void addOrReplaceChatMessages(const QList<DiscordMessage> &messages);
  void updateChatMessage(const DiscordMessage &message);
  void deleteChatMessage(const QString &channelId, const QString &messageId);
  QString addPendingChatMessage(const DiscordMessage &message);
  void markPendingChatMessageFailed(const QString &channelId,
                                    const QString &messageId);
  void clearChatCache();
  void setCurrentUserRoleIdsForGuild(const QString &guildId,
                                     const QStringList &roleIds);
  // Fix: mark/clear a channel's "has an unseen message from a guild
  // that wasn't open when it arrived" state - see
  // isChannelMarkedUnread() above for why this exists separately from
  // the unread flag directly on the channel's own QVariantMap entry.
  void markChannelUnread(const QString &channelId);
  void clearChannelUnread(const QString &channelId);

  // Overwrites the full role list for a guild (full replace, not a
  // per-item patch — matching Discord's GUILD_CREATE behavior: each
  // time this event arrives it's treated as the full current state).
  // Called by DiscordClient on GUILD_CREATE.
  void setGuildRoles(const QString &guildId, const QVariantList &roles);

  // Overwrites the full member list for a channel — only called for
  // the "SYNC" op of GUILD_MEMBER_LIST_UPDATE (a full snapshot). The
  // "INSERT"/"UPDATE"/"DELETE" ops (incremental changes while the sheet
  // is open) are NOT handled in this version — accepted trade-off to
  // keep the change scope small and safe; revisit if realtime presence
  // in the Members sheet is needed.
  void setMemberListForChannel(const QString &channelId,
                               const QVariantList &members);

Q_SIGNALS:
  void loggedInChanged(bool loggedIn);
  void busyChanged(bool busy);
  void dataLoadingChanged(bool dataLoading);
  void statusTextChanged(const QString &statusText);
  void currentUserChanged();
  void guildsChanged();
  void guildFoldersChanged();
  void guildsReordered();
  void guildIconChanged(const QString &guildId, const QString &iconSource);
  void dmChannelsChanged();
  void dmChannelsAppended(const QVariantList &channels);
  void dmAvatarChanged(const QString &channelId, const QString &avatarSource);
  void dmAvatar2Changed(const QString &channelId, const QString &avatarSource);
  void dmStatusChanged(const QString &channelId, const QString &status,
                       const QString &statusColor);
  void guildChannelsChanged();
  void guildChannelsAppended(const QVariantList &channels);
  void channelThreadsChanged();
  void selectionChanged();
  void currentChannelMessagesChanged();
  void currentChatStateChanged();
  void chatMessagesReset(const QString &channelId,
                         const QVariantList &messages);
  void chatMessagesPrepended(const QString &channelId,
                             const QVariantList &messages);
  void chatMessagesBatched(const QString &channelId,
                           const QVariantList &messages);
  void chatMessageAdded(const QString &channelId, const QVariantMap &message);
  void chatMessageUpdated(const QString &channelId, const QVariantMap &message);
  void chatMessageDeleted(const QString &channelId, const QString &messageId);
  void chatAvatarChanged(const QString &userId, const QString &avatarSource);
  void guildRolesChanged(const QString &guildId);
  void memberListChanged(const QString &channelId);

private:
  bool m_loggedIn;
  bool m_busy;
  bool m_dataLoading;
  QString m_statusText;
  DiscordUser m_currentUser;
  QString m_currentUserAvatarSource;
  QVariantList m_guilds;
  QVariantList m_guildFolders;
  QVariantList m_dmChannels;
  QVariantList m_guildChannels;
  QVariantMap m_channelThreadsByParentId;
  QString m_selectedGuildId;
  QString m_selectedChannelId;
  MessageCache m_messageCache;
  QMap<QString, QStringList> m_currentUserRoleIdsByGuildId;
  QMap<QString, QVariantList> m_guildRolesByGuildId;
  QMap<QString, QVariantList> m_memberListByChannelId;
  // Fix: see isChannelMarkedUnread()/markChannelUnread()/
  // clearChannelUnread() in this header for why this exists.
  QSet<QString> m_unreadChannelIds;
};

#endif /* AppStore_HPP_ */
