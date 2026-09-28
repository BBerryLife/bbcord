#include "GuildChannels.hpp"

#include "../AppStore.hpp"
#include "../Client.hpp"
#include "../HubIntegration.hpp"
#include "../client/ItemMapper.hpp"
#include "../client/PermissionUtils.hpp"
#include "../client/SortUtils.hpp"
#include "../discord/GatewayWorker.hpp"
#include "../discord/NetworkWorker.hpp"
#include "FeatureConstants.hpp"

#include <QDebug>

#include <QMetaObject>

void DiscordClient::loadGuildChannels(const QString &guildId) {
  if (m_loadingGuilds || m_loadingDmChannels || guildId.trimmed().isEmpty() ||
      m_token.trimmed().isEmpty()) {
    return;
  }

  QString safeGuildId = guildId.trimmed();
  QString currentUserId = m_store ? m_store->currentUserId() : QString();

  if (safeGuildId == m_selectedGuildId && !m_allGuildChannels.isEmpty()) {
    // Refresh the user's own roles on every (re)select, even when channels are
    // cached: roles can change without a channel list change.
    if (m_networkWorker != 0 && !currentUserId.isEmpty()) {
      QMetaObject::invokeMethod(m_networkWorker, "fetchSelfGuildMember",
                                Qt::QueuedConnection, Q_ARG(QString, m_token),
                                Q_ARG(QString, safeGuildId),
                                Q_ARG(QString, currentUserId));
    }
    return;
  }

  m_selectedGuildId = safeGuildId;
  m_allGuildChannels.clear();
  m_visibleGuildChannels.clear();
  m_visibleGuildChannelCount = 0;
  m_guildChannelsHasMore = false;
  if (m_store) {
    m_store->setGuildChannels(QVariantList());
  }

  m_loadingGuildChannels = true;
  updateDataLoading();
  setStatusText("Loading channels...");
  if (m_networkWorker != 0) {
    QMetaObject::invokeMethod(m_networkWorker, "fetchGuildChannels",
                              Qt::QueuedConnection, Q_ARG(QString, m_token),
                              Q_ARG(QString, m_selectedGuildId),
                              Q_ARG(int, kPageSize), Q_ARG(QString, QString()));
    // READY.guilds has no "members" field, so own roles are unknown until this REST
    // call returns (fetched on every open). onSelfGuildMemberLoaded() then redoes the
    // accessibility pass, since onGuildChannelsLoaded() may have used stale roles.
    if (!currentUserId.isEmpty()) {
      QMetaObject::invokeMethod(m_networkWorker, "fetchSelfGuildMember",
                                Qt::QueuedConnection, Q_ARG(QString, m_token),
                                Q_ARG(QString, m_selectedGuildId),
                                Q_ARG(QString, currentUserId));
    }
  }
}

void DiscordClient::loadMoreGuildChannels() {
  if (!m_guildChannelsHasMore) {
    return;
  }

  appendVisibleGuildChannels();
}

QString DiscordClient::guildIdForChannel(const QString &channelId) const {
  return m_chatGuildByChannelId.value(channelId.trimmed());
}

QString DiscordClient::channelNameForId(const QString &channelId) const {
  QString safeChannelId = channelId.trimmed();
  if (safeChannelId.isEmpty()) {
    return QString();
  }

  // DMs first: m_dmChannelsById is global, so it is safe to check unconditionally.
  QString dmName =
      m_dmChannelsById.value(safeChannelId).toMap().value("name").toString();
  if (!dmName.isEmpty()) {
    return dmName;
  }

  // Only populated for m_selectedGuildId. Both lists are checked because a channel
  // may be filtered out of m_allGuildChannels but still be in the raw list.
  for (int i = 0; i < m_allGuildChannels.size(); ++i) {
    QVariantMap channel = m_allGuildChannels.at(i).toMap();
    if (channel.value("id").toString() == safeChannelId) {
      return channel.value("name").toString();
    }
  }
  for (int i = 0; i < m_rawSelectedGuildChannels.size(); ++i) {
    QVariantMap channel = m_rawSelectedGuildChannels.at(i).toMap();
    if (channel.value("id").toString() == safeChannelId) {
      return channel.value("name").toString();
    }
  }

  return QString();
}

void DiscordClient::fetchChannelInfo(const QString &channelId) {
  QString safeChannelId = channelId.trimmed();
  if (safeChannelId.isEmpty() || m_token.trimmed().isEmpty() ||
      m_networkWorker == 0) {
    return;
  }

  QMetaObject::invokeMethod(m_networkWorker, "fetchChannelInfo",
                            Qt::QueuedConnection, Q_ARG(QString, m_token),
                            Q_ARG(QString, safeChannelId));
}

void DiscordClient::onChannelInfoLoaded(const QString &channelId,
                                        const QString &guildId,
                                        const QString &channelName) {
  QString safeChannelId = channelId.trimmed();
  QString safeGuildId = guildId.trimmed();
  qDebug() << "[discord-chat] channel info resolved" << safeChannelId
           << "guildId=" << safeGuildId << "name=" << channelName;

  if (!safeGuildId.isEmpty()) {
    // Cache like selectChannel() does, so re-tapping the same Hub item skips REST.
    m_chatGuildByChannelId.insert(safeChannelId, safeGuildId);
    // Redo selectChannel()'s unread-clear once onGuildChannelsLoaded() has channel
    // data (see m_pendingUnreadClearChannelId).
    m_pendingUnreadClearChannelId = safeChannelId;
    // Selecting the guild triggers the normal loadGuildChannels() flow, which fires
    // guildChannelsChanged (main.qml's self-heal path) for the unknown-guildId cold start too.
    selectGuild(safeGuildId);
  }

  emit channelInfoResolved(safeChannelId, safeGuildId, channelName);
}

void DiscordClient::onChannelInfoLoadFailed(const QString &channelId,
                                            const QString &message) {
  qDebug() << "[discord-chat] channel info fetch failed for" << channelId
           << ":" << message;
  // Emit with empty guildId/channelName on failure; main.qml treats that as
  // "couldn't resolve" and leaves the UI unchanged.
  emit channelInfoResolved(channelId.trimmed(), QString(), QString());
}

void DiscordClient::selectChannel(const QString &channelId) {
  QString safeChannelId = channelId.trimmed();
  if (safeChannelId.isEmpty()) {
    return;
  }

  clearChannelUnreadStateAndRecomputeGuildBadge(safeChannelId);

  if (m_store) {
    // Also clears the AppStore record of "marked unread while another guild was open".
    m_store->clearChannelUnread(safeChannelId);
    m_store->selectChannel(safeChannelId);
    syncGatewayMessageFilterStateToWorker();
  }

  QString guildId = m_chatGuildByChannelId.value(safeChannelId).trimmed();
  if (guildId.isEmpty()) {
    guildId = m_selectedGuildId.trimmed();
  }
  if (!guildId.isEmpty()) {
    m_chatGuildByChannelId.insert(safeChannelId, guildId);
    if (m_gatewayWorker != 0) {
      QMetaObject::invokeMethod(m_gatewayWorker, "sendLazyRequest",
                                Qt::QueuedConnection, Q_ARG(QString, guildId),
                                Q_ARG(QString, safeChannelId));
    }
  }

  if (m_hubIntegration != 0) {
    m_hubIntegration->markThreadRead(safeChannelId);
  }

  scheduleGuildsCacheSave();
  scheduleDmChannelsCacheSave();
}

void DiscordClient::clearChannelUnreadStateAndRecomputeGuildBadge(
    const QString &channelId, bool forceGuildBadgeRecompute) {
  bool channelStatusChanged = updateGuildChannelUnread(channelId, false);
  channelStatusChanged =
      updateGuildChannelMentionCount(channelId, 0) || channelStatusChanged;
  qDebug() << "[discord-chat] clear channel unread" << channelId
           << "channelStatusChanged=" << channelStatusChanged;
  if (channelStatusChanged && m_store) {
    m_store->setGuildChannels(m_visibleGuildChannels);
  }

  // Re-derive the guild's white bar (any channel unread/mentioned) and red badge
  // (sum of mentions) from its channels whenever one is caught up, whether by
  // opening it or by a live mention in the open channel.
  // forceGuildBadgeRecompute bypasses the "channel changed" check: on the Hub
  // cold-start path the REST channel list already shows the channel as read, so no
  // delta is reported although the guild-level badge is still stale.
  if (channelStatusChanged || forceGuildBadgeRecompute) {
    QString ownerGuildId = m_chatGuildByChannelId.value(channelId).trimmed();
    if (ownerGuildId.isEmpty()) {
      ownerGuildId = m_selectedGuildId.trimmed();
    }
    qDebug() << "[discord-chat] recompute guild badges for" << ownerGuildId
             << "from" << m_allGuildChannels.size() << "channels"
             << "(forced=" << forceGuildBadgeRecompute << ")";
    if (!ownerGuildId.isEmpty()) {
      bool anyChannelStillUnread = false;
      int totalMentionCount = 0;
      for (int i = 0; i < m_allGuildChannels.size(); ++i) {
        QVariantMap channel = m_allGuildChannels.at(i).toMap();
        int channelMentionCount = channel.value("mentionCount").toInt();
        totalMentionCount += channelMentionCount;
        if (channel.value("unread").toBool() || channelMentionCount > 0) {
          anyChannelStillUnread = true;
        }
      }
      qDebug() << "[discord-chat] result anyChannelStillUnread="
               << anyChannelStillUnread
               << "totalMentionCount=" << totalMentionCount;
      updateGuildUnread(ownerGuildId, anyChannelStillUnread);
      updateGuildMentionCount(ownerGuildId, totalMentionCount);
    }
  }
}

void DiscordClient::onGuildChannelsLoaded(const QString &guildId,
                                          const QVariantList &channels) {
  if (guildId != m_selectedGuildId) {
    return;
  }

  m_loadingGuildChannels = false;
  updateDataLoading();

  // GET /guilds/{id}/channels returns every channel regardless of visibility; the
  // client must compute it from roles and permission_overwrites. Done here (not in
  // the stateless ItemMapper) since m_store is needed. Unfiltered items are kept in
  // m_rawSelectedGuildChannels so the pass can be redone without refetching.
  m_rawSelectedGuildChannels.clear();
  for (int i = 0; i < channels.size(); ++i) {
    QVariantMap item = m_itemMapper->guildChannelToItem(channels.at(i).toMap());
    if (item.value("id").toString().isEmpty()) {
      continue;
    }
    m_rawSelectedGuildChannels.append(item);
  }

  recomputeAccessibleGuildChannels(guildId);
  setStatusText("Connected");

  // Redo the unread/badge clear for a channel opened via the Hub cold-start
  // fallback. Guarded by guildId so another guild's load cannot consume the flag.
  if (!m_pendingUnreadClearChannelId.isEmpty() &&
      m_chatGuildByChannelId.value(m_pendingUnreadClearChannelId) == guildId) {
    QString channelIdToClear = m_pendingUnreadClearChannelId;
    m_pendingUnreadClearChannelId.clear();
    // forceGuildBadgeRecompute = true: the fresh REST data shows this channel as
    // read, so the per-channel change check would skip the guild-badge recompute.
    clearChannelUnreadStateAndRecomputeGuildBadge(channelIdToClear, true);
    if (m_store) {
      m_store->clearChannelUnread(channelIdToClear);
    }
  }

  // Threads are not fetched via REST (the "active threads" endpoints are bot-only,
  // code 20002). They arrive via THREAD_LIST_SYNC (op 14, "threads: true") and are
  // written to m_channelThreadsByParentId in Client.cpp.
}

void DiscordClient::onSelfGuildMemberLoaded(const QString &guildId,
                                            const QStringList &roleIds) {
  if (m_store) {
    m_store->setCurrentUserRoleIdsForGuild(guildId, roleIds);
  }

  // fetchSelfGuildMember() supplies the user's roles; it often lands after
  // onGuildChannelsLoaded(), so the accessibility pass is redone with the real roles.
  recomputeAccessibleGuildChannels(guildId);
}

void DiscordClient::recomputeAccessibleGuildChannels(const QString &guildId) {
  if (guildId != m_selectedGuildId) {
    return;
  }

  QString currentUserId = m_store ? m_store->currentUserId() : QString();
  QVariantList guildRoles =
      m_store ? m_store->guildRolesForGuild(guildId) : QVariantList();
  QStringList currentUserRoleIds =
      m_store ? m_store->currentUserRoleIdsForGuild(guildId) : QStringList();

  QVariantList rawChannels;
  for (int i = 0; i < m_rawSelectedGuildChannels.size(); ++i) {
    QVariantMap item = m_rawSelectedGuildChannels.at(i).toMap();
    bool accessible = PermissionUtils::canViewChannel(
        guildId, currentUserId, guildRoles, currentUserRoleIds,
        item.value("permissionOverwrites").toList());
    item["accessible"] = accessible;
    // A message may have arrived while another guild was open (recorded via
    // markChannelUnread()); apply that state now that the channel is loaded.
    if (m_store && m_store->isChannelMarkedUnread(item.value("id").toString())) {
      item["unread"] = true;
    }
    rawChannels.append(item);
  }

  m_allGuildChannels = m_sortUtils->sortedAccessibleGuildChannels(rawChannels);
  m_visibleGuildChannels.clear();
  m_visibleGuildChannelCount = 0;
  // appendVisibleGuildChannels() re-appends everything, so clear the store's copy
  // first to avoid duplicating channels in the QML-bound list.
  if (m_store) {
    m_store->setGuildChannels(QVariantList());
  }
  appendVisibleGuildChannels();
}

QVariantList DiscordClient::threadsForChannel(const QString &channelId) const {
  return m_channelThreadsByParentId.value(channelId.trimmed()).toList();
}

void DiscordClient::requestArchivedThreads(const QString &channelId,
                                           const QString &beforeCursor) {
  QString safeChannelId = channelId.trimmed();
  if (safeChannelId.isEmpty() || m_token.trimmed().isEmpty()) {
    return;
  }

  if (m_networkWorker != 0) {
    QMetaObject::invokeMethod(
        m_networkWorker, "fetchArchivedThreads", Qt::QueuedConnection,
        Q_ARG(QString, m_token), Q_ARG(QString, safeChannelId),
        Q_ARG(QString, beforeCursor.trimmed()));
  }
}

void DiscordClient::onArchivedThreadsLoaded(const QString &channelId,
                                            const QVariantList &threads,
                                            bool hasMore) {
  QVariantList mappedThreads;
  for (int i = 0; i < threads.size(); ++i) {
    QVariantMap item = m_itemMapper->guildChannelToItem(threads.at(i).toMap());
    if (!item.value("id").toString().isEmpty()) {
      mappedThreads.append(item);
    }
  }

  // Not merged into m_channelThreadsByParentId: archived and active threads are kept separate.
  emit archivedThreadsLoaded(channelId, mappedThreads, hasMore);
}

bool DiscordClient::updateGuildChannelUnread(const QString &channelId,
                                             bool unread) {
  QString safeChannelId = channelId.trimmed();
  if (safeChannelId.isEmpty()) {
    return false;
  }

  bool changed = false;
  for (int i = 0; i < m_allGuildChannels.size(); ++i) {
    QVariantMap channel = m_allGuildChannels.at(i).toMap();
    if (channel.value("id").toString() == safeChannelId) {
      if (channel.value("unread").toBool() != unread) {
        channel["unread"] = unread;
        m_allGuildChannels.replace(i, channel);
        changed = true;
      }
      break;
    }
  }

  for (int i = 0; i < m_visibleGuildChannels.size(); ++i) {
    QVariantMap channel = m_visibleGuildChannels.at(i).toMap();
    if (channel.value("id").toString() == safeChannelId) {
      if (channel.value("unread").toBool() != unread) {
        channel["unread"] = unread;
        m_visibleGuildChannels.replace(i, channel);
        changed = true;
      }
      break;
    }
  }

  return changed;
}

bool DiscordClient::updateGuildChannelMentionCount(const QString &channelId,
                                                   int mentionCount) {
  QString safeChannelId = channelId.trimmed();
  if (safeChannelId.isEmpty()) {
    return false;
  }

  if (mentionCount < 0) {
    mentionCount = 0;
  }

  bool changed = false;
  for (int i = 0; i < m_allGuildChannels.size(); ++i) {
    QVariantMap channel = m_allGuildChannels.at(i).toMap();
    if (channel.value("id").toString() == safeChannelId) {
      if (channel.value("mentionCount").toInt() != mentionCount) {
        channel["mentionCount"] = mentionCount;
        m_allGuildChannels.replace(i, channel);
        changed = true;
      }
      break;
    }
  }

  for (int i = 0; i < m_visibleGuildChannels.size(); ++i) {
    QVariantMap channel = m_visibleGuildChannels.at(i).toMap();
    if (channel.value("id").toString() == safeChannelId) {
      if (channel.value("mentionCount").toInt() != mentionCount) {
        channel["mentionCount"] = mentionCount;
        m_visibleGuildChannels.replace(i, channel);
        changed = true;
      }
      break;
    }
  }

  return changed;
}

void DiscordClient::appendVisibleGuildChannels() {
  int nextCount = m_allGuildChannels.size();

  QVariantList appendedChannels;
  for (int i = m_visibleGuildChannelCount; i < nextCount; ++i) {
    m_visibleGuildChannels.append(m_allGuildChannels.at(i));
    appendedChannels.append(m_allGuildChannels.at(i));
  }

  m_visibleGuildChannelCount = nextCount;
  m_guildChannelsHasMore =
      m_visibleGuildChannelCount < m_allGuildChannels.size();
  if (m_store) {
    m_store->appendGuildChannels(appendedChannels);
  }
}
