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
    // Fix: still re-fetch the current user's own roles for this guild
    // even on the "already have this guild's channels cached, skip the
    // channel refetch" path below - reported bug: granting a role to a
    // user while BBCord has that guild open (or previously opened, then
    // reselected without closing the app) never revealed the newly
    // unlocked channel, even after a full app restart, because
    // fetchSelfGuildMember() was only ever called from the "cold" path
    // past this early return. Roles CAN change without a channel list
    // change, so this needs its own unconditional refresh every time
    // the guild is (re)selected, not just on the first load.
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
    // Fix: READY.guilds[i] has no "members" field on this user-token
    // gateway (confirmed via real debug logs - only "roles"/"threads"
    // are present per guild), so the current user's own roles in this
    // guild are unknown until this REST call comes back - fetched
    // every time a guild is opened/reopened (not cached indefinitely)
    // so a role granted while the app was closed is picked up as soon
    // as the guild is selected again, without needing a restart.
    // onSelfGuildMemberLoaded() re-runs the channel accessibility pass
    // once this returns, since onGuildChannelsLoaded() above may well
    // have already run (and filtered) with stale/empty role data.
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

  // DMs first - m_dmChannelsById is global (not scoped to a single
  // selected guild like m_allGuildChannels below), so this is safe to
  // check unconditionally.
  QString dmName =
      m_dmChannelsById.value(safeChannelId).toMap().value("name").toString();
  if (!dmName.isEmpty()) {
    return dmName;
  }

  // Guild channels: only ever populated for m_selectedGuildId (see
  // loadGuildChannels()) - a channel belonging to a guild that hasn't
  // been selected/loaded this session simply won't be found here yet.
  // Checked in both lists since a channel can be filtered out of
  // m_allGuildChannels (permission pass) while still present in the
  // raw pre-filter list.
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
    // Cache it the same way selectChannel()'s normal path does, so a
    // second lookup this session (e.g. re-tapping the same Hub item)
    // hits guildIdForChannel() directly instead of round-tripping REST
    // again.
    m_chatGuildByChannelId.insert(safeChannelId, safeGuildId);
    // Fix: see m_pendingUnreadClearChannelId's doc comment in
    // Client.hpp - selectChannel()'s unread-clear already ran and
    // found nothing (guild wasn't loaded yet), so flag this channel to
    // have it redone once onGuildChannelsLoaded() actually has real
    // channel data for this guild.
    m_pendingUnreadClearChannelId = safeChannelId;
    // Fix: this is the actual point of the REST call, not just a
    // side-effect - selecting the guild triggers the NORMAL
    // loadGuildChannels() flow (see above), which populates
    // m_allGuildChannels and fires appStore's guildChannelsChanged -
    // the self-heal path main.qml already wired up for the
    // "guildId WAS known but channels weren't loaded yet" case (see
    // that file's onGuildChannelsChanged() comment) now also covers
    // this "guildId itself was unknown" cold-start case for free.
    selectGuild(safeGuildId);
  }

  emit channelInfoResolved(safeChannelId, safeGuildId, channelName);
}

void DiscordClient::onChannelInfoLoadFailed(const QString &channelId,
                                            const QString &message) {
  qDebug() << "[discord-chat] channel info fetch failed for" << channelId
           << ":" << message;
  // Fix: still emit with empty guildId/channelName rather than swallow
  // the failure - main.qml's handler already treats an empty
  // channelName as "couldn't resolve, leave whatever's showing" (see
  // its onGuildChannelsChanged()/tryOpenPendingHubChannel() comments),
  // so this is a safe no-op on the UI side, just keeps the two arms
  // symmetric instead of one of them silently going nowhere.
  emit channelInfoResolved(channelId.trimmed(), QString(), QString());
}

void DiscordClient::selectChannel(const QString &channelId) {
  QString safeChannelId = channelId.trimmed();
  if (safeChannelId.isEmpty()) {
    return;
  }

  clearChannelUnreadStateAndRecomputeGuildBadge(safeChannelId);

  if (m_store) {
    // Fix: clears the independent "marked unread while some other
    // guild was open" record from AppStore too (see markChannelUnread()
    // in flushGatewayUiUpdates()/recomputeAccessibleGuildChannels()) -
    // without this, reopening this channel's guild later would keep
    // re-marking it unread even after the user has already read it
    // here.
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

  // Fix: updateGuildUnread(guildId, true)/updateGuildMentionCount()
  // (Client.cpp/Guilds.cpp) are what light up the server's white bar
  // and red mention badge in the first place, but nothing ever called
  // them again to clear those - confirmed as a real bug: the white
  // bar stayed on (and, separately, the badge was reported as always
  // gone even when it shouldn't be, from an earlier fix that removed
  // it from the UI entirely rather than just adjusting when it shows)
  // even after every channel in that guild had been read. Re-derive
  // both from the guild's channels every time one of them is caught up
  // (either by opening it, or by a mention arriving in the channel
  // that's already open - see the MESSAGE_CREATE handling in
  // Client.cpp::onGatewayDispatch(), added because reading a mention
  // live in an already-open channel never called this before, so the
  // server's badge/white bar stayed on even though the channel itself
  // showed as read): the white bar should reflect ANY channel still
  // unread/mentioned; the red badge should reflect the SUM of mentions
  // still outstanding across the guild's channels (m_allGuildChannels
  // already reflects channelId's own just-cleared state above).
  //
  // Fix: was "if (channelStatusChanged)" only - confirmed as a real
  // bug via logs (a second real-world case, not just theoretical) for
  // the Hub cold-start path (see
  // m_pendingUnreadClearChannelId/onGuildChannelsLoaded() in this same
  // file): fetchChannelInfo()'s REST channel list already comes back
  // with unread=false/mentionCount=0 for the just-opened channel (its
  // own state was never stale - the GUILD badge lit up from an
  // earlier gateway mention event, well before the guild's channels
  // were ever loaded this session), so updateGuildChannelUnread()/
  // updateGuildChannelMentionCount() correctly report "no change" and
  // this whole block used to get skipped - the guild's badge/white bar
  // stayed lit forever even though the channel itself was genuinely
  // read. forceGuildBadgeRecompute lets that caller ask for the
  // guild-level recompute unconditionally, since a channel's own
  // "already correct, no delta" state says nothing about whether the
  // GUILD-level badge (separate piece of state) is still stale.
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

  // Fix: GET /guilds/{id}/channels returns every channel in the guild
  // regardless of real per-user visibility - Discord expects the
  // client to compute that itself from the guild's roles + each
  // channel's permission_overwrites (see PermissionUtils.hpp for why,
  // and the "accessible" comment removed from ItemMapper.cpp). Compute
  // it here rather than in ItemMapper, since that mapper is stateless
  // and doesn't have access to m_store (roles / current user id).
  // The mapped-but-unfiltered items are kept in
  // m_rawSelectedGuildChannels so recomputeAccessibleGuildChannels()
  // can redo just the accessibility pass later (e.g. once
  // onSelfGuildMemberLoaded() brings in the real role list) without
  // needing to re-fetch channels from Discord.
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

  // Fix: see m_pendingUnreadClearChannelId's doc comment in Client.hpp
  // - redo the unread/badge clear for a channel that was opened via
  // Hub's cold-start REST fallback (fetchChannelInfo()) before this
  // guild's channels were loaded. Guarded by guildId match (not just
  // "pending id is non-empty") so a DIFFERENT guild loading its
  // channels around the same time can't consume/clear the flag for a
  // channel that belongs elsewhere.
  if (!m_pendingUnreadClearChannelId.isEmpty() &&
      m_chatGuildByChannelId.value(m_pendingUnreadClearChannelId) == guildId) {
    QString channelIdToClear = m_pendingUnreadClearChannelId;
    m_pendingUnreadClearChannelId.clear();
    // Fix: forceGuildBadgeRecompute=true - confirmed via logs as the
    // actual missing piece: this channel's own unread/mentionCount in
    // the just-loaded REST data already reads false/0 (Discord's own
    // channel list, fetched fresh, has no reason to show it as
    // unread), so the per-channel "did anything change" check here
    // finds nothing new and would otherwise skip the guild-badge
    // recompute entirely - leaving the guild's white bar/red badge lit
    // from whatever earlier gateway event set it, forever. Force it
    // here specifically, since this call only happens once, right
    // after this guild's channel data has JUST become available for
    // the first time this session - exactly the case the normal
    // "only recompute if this channel changed" guard was never meant
    // to cover.
    clearChannelUnreadStateAndRecomputeGuildBadge(channelIdToClear, true);
    if (m_store) {
      m_store->clearChannelUnread(channelIdToClear);
    }
  }

  // Fix: Threads are NOT fetched via REST here (or anywhere else).
  // Tried both GET /guilds/{id}/threads/active and GET /channels/{id}/
  // threads/active - Discord rejects both with the exact same error:
  // {"message": "Only bots can use this endpoint.", "code": 20002} -
  // this is a hard limit, only bot tokens can call the "active threads"
  // REST endpoints, no way around it from the request side. Thread
  // data now comes entirely through the GATEWAY: Discord pushes a
  // THREAD_LIST_SYNC event when subscribing to a guild with the
  // "threads: true" flag (already enabled in
  // buildGuildSubscribePayload(), see JsonParser.cpp) - handled in
  // Client.cpp::onGatewayDispatch(), written straight into
  // m_channelThreadsByParentId, no intermediate layer needed here.
}

void DiscordClient::onSelfGuildMemberLoaded(const QString &guildId,
                                            const QStringList &roleIds) {
  if (m_store) {
    m_store->setCurrentUserRoleIdsForGuild(guildId, roleIds);
  }

  // Fix: this REST call (fetchSelfGuildMember(), fired alongside
  // fetchGuildChannels() in loadGuildChannels()) is what actually
  // supplies the current user's roles for a guild on this gateway
  // (READY.guilds[i] has no "members" field - see the comment above).
  // It very often lands AFTER onGuildChannelsLoaded() already ran its
  // accessibility pass with an empty/stale role list, so that pass has
  // to be redone here with the roles that just came in - otherwise a
  // role granted to the user (while the app was closed OR while it was
  // open, since this fetch also fires on every fresh loadGuildChannels()
  // call) would never actually reveal the channel, even after a
  // restart, exactly as reported.
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
    // Fix: a message can arrive (via gateway) for this channel while
    // some OTHER guild is open, in which case updateGuildChannelUnread()
    // (Client.cpp) had nothing to update yet - AppStore recorded it
    // separately via markChannelUnread() instead (see
    // flushGatewayUiUpdates()/AppStore.hpp). Apply that recorded state
    // now that this channel is actually being (re)loaded, so it shows
    // up white/unread immediately rather than only after the NEXT
    // message arrives while this guild happens to be open.
    if (m_store && m_store->isChannelMarkedUnread(item.value("id").toString())) {
      item["unread"] = true;
    }
    rawChannels.append(item);
  }

  m_allGuildChannels = m_sortUtils->sortedAccessibleGuildChannels(rawChannels);
  m_visibleGuildChannels.clear();
  m_visibleGuildChannelCount = 0;
  // Fix: appendVisibleGuildChannels() below re-appends the ENTIRE
  // rebuilt m_allGuildChannels into m_store (since
  // m_visibleGuildChannelCount was just reset to 0) - if the store
  // still has the previous pass's channels in it (from an earlier
  // onGuildChannelsLoaded() or recomputeAccessibleGuildChannels() call
  // for this same guild), that would duplicate every channel in the
  // QML-bound list instead of replacing it. Clear the store's copy
  // first, same as loadGuildChannels() does before the very first
  // fetch, so this stays a full rebuild rather than an accumulation.
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

  // Deliberately NOT merged into m_channelThreadsByParentId/
  // threadsForChannel() - archived threads are a separate list, shown
  // apart from active threads in the UI, to avoid mixing two different
  // states (archived vs active) into the same data source.
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
