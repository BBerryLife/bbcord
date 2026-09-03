#ifndef AppStore_HPP_
#define AppStore_HPP_

#include <QMap>
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
  Q_INVOKABLE QVariantList messagesForChannel(const QString &channelId) const;
  Q_INVOKABLE bool isChatInitialLoaded(const QString &channelId) const;
  Q_INVOKABLE bool isChatLoadingInitial(const QString &channelId) const;
  Q_INVOKABLE bool isChatLoadingBefore(const QString &channelId) const;
  Q_INVOKABLE bool hasMoreChatBefore(const QString &channelId) const;
  QStringList loadedChatChannelIds() const;
  Q_INVOKABLE QString oldestChatMessageId(const QString &channelId) const;
  Q_INVOKABLE QString newestChatMessageId(const QString &channelId) const;
  Q_INVOKABLE void clearSession();

  // Role IDs của user hiện tại trong 1 guild cụ thể — nạp từ field
  // "member" (self member object) của payload GUILD_CREATE, xem
  // DiscordClient::onGatewayGuildCreate() (Guilds.cpp). Dùng để xác định
  // 1 tin nhắn có role-mention (mention_roles) tới mình hay không, cho
  // tính năng thông báo Hub. Trả về danh sách rỗng nếu chưa có dữ liệu
  // cho guild đó (chưa nhận GUILD_CREATE, hoặc chưa đăng nhập).
  Q_INVOKABLE QStringList currentUserRoleIdsForGuild(const QString &guildId) const;

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
  QString m_selectedGuildId;
  QString m_selectedChannelId;
  MessageCache m_messageCache;
  QMap<QString, QStringList> m_currentUserRoleIdsByGuildId;
};

#endif /* AppStore_HPP_ */
