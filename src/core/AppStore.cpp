#include "AppStore.hpp"

#include <QStringList>

namespace {
const char *kDefaultUserAvatar = "asset:///images/default-avt.png";
}

AppStore::AppStore(QObject *parent)
    : QObject(parent), m_loggedIn(false), m_busy(false), m_dataLoading(false),
      m_statusText("Disconnected"),
      m_currentUserAvatarSource(kDefaultUserAvatar) {}

bool AppStore::loggedIn() const { return m_loggedIn; }

bool AppStore::busy() const { return m_busy; }

bool AppStore::dataLoading() const { return m_dataLoading; }

QString AppStore::statusText() const { return m_statusText; }

QString AppStore::currentUserName() const {
  return m_currentUser.displayName();
}

QString AppStore::currentUserId() const { return m_currentUser.id; }

QString AppStore::currentUserTag() const {
  if (m_currentUser.username.isEmpty()) {
    return QString();
  }

  if (!m_currentUser.discriminator.isEmpty() &&
      m_currentUser.discriminator != "0") {
    return QString("%1#%2")
        .arg(m_currentUser.username)
        .arg(m_currentUser.discriminator);
  }

  return m_currentUser.username;
}

QString AppStore::currentUserAvatarSource() const {
  if (m_currentUserAvatarSource.isEmpty()) {
    return kDefaultUserAvatar;
  }

  return m_currentUserAvatarSource;
}

QVariantList AppStore::guilds() const { return m_guilds; }

QVariantList AppStore::guildFolders() const { return m_guildFolders; }

QVariantList AppStore::dmChannels() const { return m_dmChannels; }

QVariantList AppStore::guildChannels() const { return m_guildChannels; }

QString AppStore::selectedGuildId() const { return m_selectedGuildId; }

QString AppStore::selectedChannelId() const { return m_selectedChannelId; }

QVariantList AppStore::currentChannelMessages() const {
  return messagesForChannel(m_selectedChannelId);
}

bool AppStore::chatInitialLoaded() const {
  return isChatInitialLoaded(m_selectedChannelId);
}

bool AppStore::chatLoadingInitial() const {
  return isChatLoadingInitial(m_selectedChannelId);
}

bool AppStore::chatLoadingBefore() const {
  return isChatLoadingBefore(m_selectedChannelId);
}

bool AppStore::chatHasMoreBefore() const {
  return hasMoreChatBefore(m_selectedChannelId);
}

QString AppStore::chatOldestMessageId() const {
  return oldestChatMessageId(m_selectedChannelId);
}

QString AppStore::chatNewestMessageId() const {
  return newestChatMessageId(m_selectedChannelId);
}

QVariantList AppStore::messagesForChannel(const QString &channelId) const {
  QString safeChannelId = channelId.trimmed();
  if (safeChannelId.isEmpty()) {
    return QVariantList();
  }

  return m_messageCache.messagesForChannel(safeChannelId);
}

bool AppStore::isChatInitialLoaded(const QString &channelId) const {
  return m_messageCache.isInitialLoaded(channelId.trimmed());
}

bool AppStore::isChatLoadingInitial(const QString &channelId) const {
  return m_messageCache.isLoadingInitial(channelId.trimmed());
}

bool AppStore::isChatLoadingBefore(const QString &channelId) const {
  return m_messageCache.isLoadingBefore(channelId.trimmed());
}

bool AppStore::hasMoreChatBefore(const QString &channelId) const {
  return m_messageCache.hasMoreBefore(channelId.trimmed());
}

QStringList AppStore::loadedChatChannelIds() const {
  return m_messageCache.initialLoadedChannelIds();
}

QString AppStore::oldestChatMessageId(const QString &channelId) const {
  return m_messageCache.oldestMessageId(channelId.trimmed());
}

QString AppStore::newestChatMessageId(const QString &channelId) const {
  return m_messageCache.newestMessageId(channelId.trimmed());
}

QStringList
AppStore::currentUserRoleIdsForGuild(const QString &guildId) const {
  return m_currentUserRoleIdsByGuildId.value(guildId.trimmed());
}

QVariantList AppStore::guildRolesForGuild(const QString &guildId) const {
  return m_guildRolesByGuildId.value(guildId.trimmed());
}

QVariantList AppStore::memberListForChannel(const QString &channelId) const {
  return m_memberListByChannelId.value(channelId.trimmed());
}

void AppStore::selectHome() {
  if (m_selectedGuildId.isEmpty() && m_selectedChannelId.isEmpty()) {
    return;
  }

  m_selectedGuildId.clear();
  m_selectedChannelId.clear();
  emit selectionChanged();
  emit currentChannelMessagesChanged();
  emit currentChatStateChanged();
}

void AppStore::selectGuild(const QString &guildId) {
  if (m_selectedGuildId == guildId && m_selectedChannelId.isEmpty()) {
    return;
  }

  m_selectedGuildId = guildId;
  m_selectedChannelId.clear();
  emit selectionChanged();
  emit currentChannelMessagesChanged();
  emit currentChatStateChanged();
}

void AppStore::selectChannel(const QString &channelId) {
  if (m_selectedChannelId == channelId) {
    return;
  }

  m_selectedChannelId = channelId;
  emit selectionChanged();
  emit currentChannelMessagesChanged();
  emit currentChatStateChanged();
}

void AppStore::clearSession() {
  setLoggedIn(false);
  setBusy(false);
  setDataLoading(false);
  setStatusText("Disconnected");
  setCurrentUser(DiscordUser());
  setGuilds(QVariantList());
  setGuildFolders(QVariantList());
  setDmChannels(QVariantList());
  setGuildChannels(QVariantList());
  clearChatCache();
  selectHome();
}

void AppStore::setLoggedIn(bool loggedIn) {
  if (m_loggedIn == loggedIn) {
    return;
  }

  m_loggedIn = loggedIn;
  emit loggedInChanged(m_loggedIn);
}

void AppStore::setBusy(bool busy) {
  if (m_busy == busy) {
    return;
  }

  m_busy = busy;
  emit busyChanged(m_busy);
}

void AppStore::setDataLoading(bool dataLoading) {
  if (m_dataLoading == dataLoading) {
    return;
  }

  m_dataLoading = dataLoading;
  emit dataLoadingChanged(m_dataLoading);
}

void AppStore::setStatusText(const QString &statusText) {
  if (m_statusText == statusText) {
    return;
  }

  m_statusText = statusText;
  emit statusTextChanged(m_statusText);
}

void AppStore::setCurrentUser(const DiscordUser &user) {
  if (m_currentUser.id == user.id && m_currentUser.username == user.username &&
      m_currentUser.globalName == user.globalName &&
      m_currentUser.discriminator == user.discriminator &&
      m_currentUser.avatarHash == user.avatarHash) {
    return;
  }

  m_currentUser = user;
  m_currentUserAvatarSource = kDefaultUserAvatar;
  emit currentUserChanged();
}

void AppStore::setCurrentUserAvatarSource(const QString &avatarSource) {
  QString safeAvatarSource = avatarSource.trimmed();
  if (safeAvatarSource.isEmpty()) {
    safeAvatarSource = kDefaultUserAvatar;
  }

  if (m_currentUserAvatarSource == safeAvatarSource) {
    return;
  }

  m_currentUserAvatarSource = safeAvatarSource;
  emit currentUserChanged();
}

void AppStore::setGuilds(const QVariantList &guilds) {
  m_guilds = guilds;
  emit guildsChanged();
}

void AppStore::setGuildFolders(const QVariantList &folders) {
  m_guildFolders = folders;
  emit guildFoldersChanged();
}

void AppStore::reorderGuilds(const QVariantList &guilds) {
  if (m_guilds.size() != guilds.size()) {
    setGuilds(guilds);
    return;
  }

  QStringList oldIds;
  QStringList newIds;
  for (int i = 0; i < m_guilds.size(); ++i) {
    oldIds.append(m_guilds.at(i).toMap().value("id").toString());
    newIds.append(guilds.at(i).toMap().value("id").toString());
  }

  QStringList sortedOldIds = oldIds;
  QStringList sortedNewIds = newIds;
  sortedOldIds.sort();
  sortedNewIds.sort();
  if (sortedOldIds != sortedNewIds) {
    setGuilds(guilds);
    return;
  }

  if (oldIds == newIds) {
    m_guilds = guilds;
    emit guildsChanged();
    return;
  }

  m_guilds = guilds;
  emit guildsReordered();
}

void AppStore::updateGuildIcon(const QString &guildId,
                               const QString &iconSource) {
  for (int i = 0; i < m_guilds.size(); ++i) {
    QVariantMap guild = m_guilds.at(i).toMap();
    if (guild.value("id").toString() == guildId) {
      guild["icon"] = iconSource;
      m_guilds.replace(i, guild);
      emit guildIconChanged(guildId, iconSource);
      return;
    }
  }
}

void AppStore::updateDmAvatar(const QString &channelId,
                              const QString &avatarSource) {
  for (int i = 0; i < m_dmChannels.size(); ++i) {
    QVariantMap channel = m_dmChannels.at(i).toMap();
    if (channel.value("id").toString() == channelId) {
      channel["avatar"] = avatarSource;
      m_dmChannels.replace(i, channel);
      emit dmAvatarChanged(channelId, avatarSource);
      return;
    }
  }
}

void AppStore::updateDmAvatar2(const QString &channelId,
                               const QString &avatarSource) {
  for (int i = 0; i < m_dmChannels.size(); ++i) {
    QVariantMap channel = m_dmChannels.at(i).toMap();
    if (channel.value("id").toString() == channelId) {
      channel["avatar2"] = avatarSource;
      m_dmChannels.replace(i, channel);
      emit dmAvatar2Changed(channelId, avatarSource);
      return;
    }
  }
}

void AppStore::updateDmStatus(const QString &channelId, const QString &status,
                              const QString &statusColor) {
  for (int i = 0; i < m_dmChannels.size(); ++i) {
    QVariantMap channel = m_dmChannels.at(i).toMap();
    if (channel.value("id").toString() == channelId) {
      if (channel.value("status").toString() == status &&
          channel.value("statusColor").toString() == statusColor) {
        return;
      }

      channel["status"] = status;
      channel["statusColor"] = statusColor;
      m_dmChannels.replace(i, channel);
      emit dmStatusChanged(channelId, status, statusColor);
      return;
    }
  }
}

void AppStore::setDmChannels(const QVariantList &dmChannels) {
  if (dmChannels.size() == m_dmChannels.size()) {
    bool same = true;
    for (int i = 0; i < dmChannels.size(); ++i) {
      QVariantMap oldChannel = m_dmChannels.at(i).toMap();
      QVariantMap newChannel = dmChannels.at(i).toMap();
      if (oldChannel.value("id").toString() !=
              newChannel.value("id").toString() ||
          oldChannel.value("name").toString() !=
              newChannel.value("name").toString() ||
          oldChannel.value("avatar").toString() !=
              newChannel.value("avatar").toString() ||
          oldChannel.value("avatar2").toString() !=
              newChannel.value("avatar2").toString() ||
          oldChannel.value("statusColor").toString() !=
              newChannel.value("statusColor").toString()) {
        same = false;
        break;
      }
    }
    if (same) {
      return;
    }
  }

  QVariantList appended;
  bool appendOnly = dmChannels.size() > m_dmChannels.size();
  if (appendOnly) {
    for (int i = 0; i < m_dmChannels.size(); ++i) {
      QVariantMap oldChannel = m_dmChannels.at(i).toMap();
      QVariantMap newChannel = dmChannels.at(i).toMap();
      if (oldChannel.value("id").toString() !=
          newChannel.value("id").toString()) {
        appendOnly = false;
        break;
      }
    }
  }

  if (appendOnly) {
    for (int i = m_dmChannels.size(); i < dmChannels.size(); ++i) {
      appended.append(dmChannels.at(i));
    }
  }

  m_dmChannels = dmChannels;
  if (appendOnly && !appended.isEmpty()) {
    emit dmChannelsAppended(appended);
    return;
  }

  emit dmChannelsChanged();
}

void AppStore::appendDmChannels(const QVariantList &channels) {
  if (channels.isEmpty()) {
    return;
  }

  for (int i = 0; i < channels.size(); ++i) {
    m_dmChannels.append(channels.at(i));
  }
  emit dmChannelsAppended(channels);
}

void AppStore::setGuildChannels(const QVariantList &channels) {
  if (channels.size() == m_guildChannels.size()) {
    bool same = true;
    for (int i = 0; i < channels.size(); ++i) {
      QVariantMap oldChannel = m_guildChannels.at(i).toMap();
      QVariantMap newChannel = channels.at(i).toMap();
      if (oldChannel.value("id").toString() !=
              newChannel.value("id").toString() ||
          oldChannel.value("unread").toBool() !=
              newChannel.value("unread").toBool() ||
          oldChannel.value("mentionCount").toInt() !=
              newChannel.value("mentionCount").toInt()) {
        same = false;
        break;
      }
    }
    if (same) {
      return;
    }
  }

  QVariantList appended;
  if (channels.size() >= m_guildChannels.size()) {
    bool appendOnly = true;
    for (int i = 0; i < m_guildChannels.size(); ++i) {
      QVariantMap oldChannel = m_guildChannels.at(i).toMap();
      QVariantMap newChannel = channels.at(i).toMap();
      if (oldChannel.value("id").toString() !=
          newChannel.value("id").toString()) {
        appendOnly = false;
        break;
      }
    }
    if (appendOnly) {
      for (int i = m_guildChannels.size(); i < channels.size(); ++i) {
        appended.append(channels.at(i));
      }
    }
  }

  m_guildChannels = channels;
  if (!appended.isEmpty()) {
    emit guildChannelsAppended(appended);
    return;
  }
  emit guildChannelsChanged();
}

void AppStore::appendGuildChannels(const QVariantList &channels) {
  if (channels.isEmpty()) {
    return;
  }

  for (int i = 0; i < channels.size(); ++i) {
    m_guildChannels.append(channels.at(i));
  }
  emit guildChannelsAppended(channels);
}

void AppStore::setChatLoadingInitial(const QString &channelId, bool loading) {
  QString safeChannelId = channelId.trimmed();
  if (safeChannelId.isEmpty()) {
    return;
  }

  m_messageCache.setLoadingInitial(safeChannelId, loading);
  if (safeChannelId == m_selectedChannelId) {
    emit currentChatStateChanged();
  }
}

void AppStore::setChatLoadingBefore(const QString &channelId, bool loading) {
  QString safeChannelId = channelId.trimmed();
  if (safeChannelId.isEmpty()) {
    return;
  }

  m_messageCache.setLoadingBefore(safeChannelId, loading);
  if (safeChannelId == m_selectedChannelId) {
    emit currentChatStateChanged();
  }
}

void AppStore::setChatHasMoreBefore(const QString &channelId, bool hasMore) {
  QString safeChannelId = channelId.trimmed();
  if (safeChannelId.isEmpty()) {
    return;
  }

  m_messageCache.setHasMoreBefore(safeChannelId, hasMore);
  if (safeChannelId == m_selectedChannelId) {
    emit currentChatStateChanged();
  }
}

void AppStore::setInitialChatMessages(const QString &channelId,
                                      const QString &guildId,
                                      const QList<DiscordMessage> &messages,
                                      bool hasMoreBefore) {
  QString safeChannelId = channelId.trimmed();
  if (safeChannelId.isEmpty()) {
    return;
  }

  m_messageCache.setInitialMessages(safeChannelId, guildId.trimmed(), messages,
                                    hasMoreBefore);
  QVariantList cachedMessages =
      m_messageCache.messagesForChannel(safeChannelId);
  emit chatMessagesReset(safeChannelId, cachedMessages);
  if (safeChannelId == m_selectedChannelId) {
    emit currentChannelMessagesChanged();
    emit currentChatStateChanged();
  }
}

void AppStore::prependOlderChatMessages(const QString &channelId,
                                        const QList<DiscordMessage> &messages,
                                        bool hasMoreBefore) {
  QString safeChannelId = channelId.trimmed();
  if (safeChannelId.isEmpty()) {
    return;
  }

  QVariantList added = m_messageCache.prependOlderMessages(
      safeChannelId, messages, hasMoreBefore);
  if (!added.isEmpty()) {
    emit chatMessagesPrepended(safeChannelId, added);
  }
  if (safeChannelId == m_selectedChannelId) {
    if (!added.isEmpty()) {
      emit currentChannelMessagesChanged();
    }
    emit currentChatStateChanged();
  }
}

void AppStore::addOrReplaceChatMessage(const DiscordMessage &message) {
  QString inputChannelId = message.channelId;
  bool alreadyExists =
      !inputChannelId.isEmpty() &&
      m_messageCache.containsMessage(inputChannelId, message.id);
  bool replacesPending =
      !inputChannelId.isEmpty() && !message.nonce.isEmpty() &&
      m_messageCache.containsMessage(inputChannelId, message.nonce);

  QVariantMap item = m_messageCache.addOrReplaceMessage(message);
  QString channelId = item.value("channelId").toString();
  if (channelId.isEmpty()) {
    return;
  }

  if (replacesPending) {
    emit chatMessagesReset(channelId,
                           m_messageCache.messagesForChannel(channelId));
  } else if (alreadyExists) {
    emit chatMessageUpdated(channelId, item);
  } else {
    emit chatMessageAdded(channelId, item);
  }
  if (channelId == m_selectedChannelId) {
    emit currentChannelMessagesChanged();
    emit currentChatStateChanged();
  }
}

void AppStore::addOrReplaceChatMessages(const QList<DiscordMessage> &messages) {
  if (messages.isEmpty()) {
    return;
  }

  QVariantMap changedByChannel = m_messageCache.addOrReplaceMessages(messages);
  bool selectedChanged = false;

  QVariantMap::const_iterator it = changedByChannel.constBegin();
  for (; it != changedByChannel.constEnd(); ++it) {
    QString channelId = it.key();
    QVariantList changedMessages = it.value().toList();
    if (changedMessages.isEmpty()) {
      continue;
    }

    emit chatMessagesBatched(channelId, changedMessages);
    if (channelId == m_selectedChannelId) {
      selectedChanged = true;
    }
  }

  if (selectedChanged) {
    emit currentChannelMessagesChanged();
    emit currentChatStateChanged();
  }
}

void AppStore::updateChatMessage(const DiscordMessage &message) {
  QVariantMap item = m_messageCache.updateMessage(message);
  QString channelId = item.value("channelId").toString();
  if (channelId.isEmpty()) {
    return;
  }

  emit chatMessageUpdated(channelId, item);
  if (channelId == m_selectedChannelId) {
    emit currentChannelMessagesChanged();
    emit currentChatStateChanged();
  }
}

void AppStore::deleteChatMessage(const QString &channelId,
                                 const QString &messageId) {
  QString safeChannelId = channelId.trimmed();
  QString safeMessageId = messageId.trimmed();
  if (safeChannelId.isEmpty() || safeMessageId.isEmpty()) {
    return;
  }

  if (!m_messageCache.deleteMessage(safeChannelId, safeMessageId)) {
    return;
  }

  emit chatMessageDeleted(safeChannelId, safeMessageId);
  if (safeChannelId == m_selectedChannelId) {
    emit currentChannelMessagesChanged();
    emit currentChatStateChanged();
  }
}

QString AppStore::addPendingChatMessage(const DiscordMessage &message) {
  QString pendingId = m_messageCache.addPendingMessage(message);
  if (pendingId.isEmpty()) {
    return QString();
  }

  QVariantMap item =
      m_messageCache.message(message.channelId, pendingId).toVariantMap();
  emit chatMessageAdded(message.channelId, item);
  if (message.channelId == m_selectedChannelId) {
    emit currentChannelMessagesChanged();
    emit currentChatStateChanged();
  }
  return pendingId;
}

void AppStore::markPendingChatMessageFailed(const QString &channelId,
                                            const QString &messageId) {
  QString safeChannelId = channelId.trimmed();
  QString safeMessageId = messageId.trimmed();
  if (safeChannelId.isEmpty() || safeMessageId.isEmpty()) {
    return;
  }

  m_messageCache.markPendingFailed(safeChannelId, safeMessageId);
  QVariantMap item =
      m_messageCache.message(safeChannelId, safeMessageId).toVariantMap();
  emit chatMessageUpdated(safeChannelId, item);
  if (safeChannelId == m_selectedChannelId) {
    emit currentChannelMessagesChanged();
    emit currentChatStateChanged();
  }
}

void AppStore::notifyChatAvatarChanged(const QString &userId,
                                       const QString &avatarSource) {
  QString safeUserId = userId.trimmed();
  QString safeAvatarSource = avatarSource.trimmed();
  if (safeUserId.isEmpty() || safeAvatarSource.isEmpty()) {
    return;
  }

  emit chatAvatarChanged(safeUserId, safeAvatarSource);
}

void AppStore::clearMediaCacheState() {
  bool guildIconsChanged = false;
  for (int i = 0; i < m_guilds.size(); ++i) {
    QVariantMap guild = m_guilds.at(i).toMap();
    if (!guild.value("icon").toString().isEmpty()) {
      guild["icon"] = QString();
      m_guilds.replace(i, guild);
      guildIconsChanged = true;
    }
  }

  bool dmsChanged = false;
  for (int i = 0; i < m_dmChannels.size(); ++i) {
    QVariantMap channel = m_dmChannels.at(i).toMap();
    bool changed = false;
    if (!channel.value("avatar").toString().isEmpty()) {
      channel["avatar"] = QString();
      changed = true;
    }
    if (!channel.value("avatar2").toString().isEmpty()) {
      channel["avatar2"] = QString();
      changed = true;
    }
    if (changed) {
      m_dmChannels.replace(i, channel);
      dmsChanged = true;
    }
  }

  m_currentUserAvatarSource = kDefaultUserAvatar;
  emit currentUserChanged();
  if (guildIconsChanged) {
    emit guildsChanged();
  }
  if (dmsChanged) {
    emit dmChannelsChanged();
  }
}

void AppStore::clearChatCache() {
  m_messageCache.clear();
  emit currentChannelMessagesChanged();
  emit currentChatStateChanged();
}

void AppStore::setCurrentUserRoleIdsForGuild(const QString &guildId,
                                             const QStringList &roleIds) {
  QString safeGuildId = guildId.trimmed();
  if (safeGuildId.isEmpty()) {
    return;
  }
  m_currentUserRoleIdsByGuildId.insert(safeGuildId, roleIds);
}

void AppStore::setGuildRoles(const QString &guildId,
                             const QVariantList &roles) {
  QString safeGuildId = guildId.trimmed();
  if (safeGuildId.isEmpty()) {
    return;
  }
  m_guildRolesByGuildId.insert(safeGuildId, roles);
  emit guildRolesChanged(safeGuildId);
}

void AppStore::setMemberListForChannel(const QString &channelId,
                                       const QVariantList &members) {
  QString safeChannelId = channelId.trimmed();
  if (safeChannelId.isEmpty()) {
    return;
  }
  m_memberListByChannelId.insert(safeChannelId, members);
  emit memberListChanged(safeChannelId);
}
