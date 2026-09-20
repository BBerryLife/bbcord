#include "Guilds.hpp"

#include "../AppStore.hpp"
#include "../Client.hpp"
#include "../client/AvatarManager.hpp"
#include "../client/ItemMapper.hpp"
#include "../client/SortUtils.hpp"
#include "../discord/NetworkWorker.hpp"
#include "FeatureConstants.hpp"

#include <QDebug>
#include <QMetaObject>

void DiscordClient::loadMainData() {
  if (!m_loggedIn || m_token.trimmed().isEmpty()) {
    return;
  }

  if (m_store) {
    m_store->setGuildFolders(m_guildFolders);
    m_store->setGuilds(m_guilds);
  }

  if (m_guilds.isEmpty()) {
    loadGuilds();
  } else if (m_dmChannels.isEmpty()) {
    loadMoreDmChannels();
  }
}

void DiscordClient::loadGuilds() {
  if (m_loadingGuilds || m_loadingDmChannels || m_loadingGuildChannels ||
      !m_guildsHasMore || m_token.trimmed().isEmpty()) {
    return;
  }

  m_loadingGuilds = true;
  updateDataLoading();
  setStatusText("Loading servers...");
  if (m_networkWorker != 0) {
    QMetaObject::invokeMethod(m_networkWorker, "fetchGuilds",
                              Qt::QueuedConnection, Q_ARG(QString, m_token),
                              Q_ARG(int, kGuildPageSize),
                              Q_ARG(QString, m_lastGuildId));
  }
}

void DiscordClient::loadGuildIcon(const QString &guildId) {
  QString safeGuildId = guildId.trimmed();
  if (safeGuildId.isEmpty()) {
    return;
  }

  for (int i = 0; i < m_guilds.size(); ++i) {
    QVariantMap guild = m_guilds.at(i).toMap();
    if (guild.value("id").toString() == safeGuildId) {
      m_avatarManager->queueGuildIcon(
          safeGuildId, guild.value("iconHash").toString(), m_loadedGuildIconIds,
          m_guildIconCacheRequests, m_queuedGuildIconIds, m_loadingGuildIconId,
          m_loadingGuildIconId2, m_pendingGuildIcons);
      m_avatarManager->loadNextGuildIcon(
          m_loadingGuildIconId, m_loadingGuildIconId2, m_pendingGuildIcons,
          m_queuedGuildIconIds);
      return;
    }
  }
}

void DiscordClient::selectGuild(const QString &guildId) {
  QString safeGuildId = guildId.trimmed();
  if (safeGuildId.isEmpty()) {
    return;
  }

  bool sameGuild = safeGuildId == m_selectedGuildId;

  if (m_store) {
    m_store->selectGuild(safeGuildId);
    syncGatewayMessageFilterStateToWorker();
  }
  scheduleDmChannelsCacheSave();
  if (!sameGuild || m_allGuildChannels.isEmpty()) {
    loadGuildChannels(safeGuildId);
  }
}

void DiscordClient::onGuildsLoaded(const QVariantList &guilds) {
  QVariantMap existingIconsByGuildId;
  QVariantMap existingUnreadByGuildId;
  QVariantMap existingMentionCountByGuildId;
  for (int i = 0; i < m_guilds.size(); ++i) {
    QVariantMap existingGuild = m_guilds.at(i).toMap();
    QString guildId = existingGuild.value("id").toString();
    QString icon = existingGuild.value("icon").toString();
    if (!guildId.isEmpty() && !icon.isEmpty()) {
      existingIconsByGuildId.insert(guildId, icon);
    }
    if (!guildId.isEmpty()) {
      existingUnreadByGuildId.insert(guildId,
                                     existingGuild.value("unread").toBool());
      existingMentionCountByGuildId.insert(
          guildId, existingGuild.value("mentionCount").toInt());
    }
  }

  if (m_bootstrapCacheLoaded) {
    m_guilds.clear();
    m_lastGuildId.clear();
    m_guildsHasMore = true;
    m_bootstrapCacheLoaded = false;
  }

  for (int i = 0; i < guilds.size(); ++i) {
    QVariantMap guild = guilds.at(i).toMap();
    QVariantMap item = m_itemMapper->guildToItem(guild);
    if (!item.value("id").toString().isEmpty()) {
      QString existingIcon =
          existingIconsByGuildId.value(item.value("id").toString()).toString();
      if (!existingIcon.isEmpty()) {
        item["icon"] = existingIcon;
      }
      if (existingUnreadByGuildId.contains(item.value("id").toString())) {
        item["unread"] =
            existingUnreadByGuildId.value(item.value("id").toString()).toBool();
      }
      if (existingMentionCountByGuildId.contains(item.value("id").toString())) {
        item["mentionCount"] =
            existingMentionCountByGuildId.value(item.value("id").toString())
                .toInt();
      }
      m_guilds.append(item);
      m_lastGuildId = item.value("id").toString();
    }
  }

  if (guilds.size() >= kGuildPageSize && !m_lastGuildId.isEmpty()) {
    m_guildsHasMore = true;
    setStatusText("Loading servers...");
    if (m_networkWorker != 0) {
      QMetaObject::invokeMethod(m_networkWorker, "fetchGuilds",
                                Qt::QueuedConnection, Q_ARG(QString, m_token),
                                Q_ARG(int, kGuildPageSize),
                                Q_ARG(QString, m_lastGuildId));
    }
    return;
  }

  m_loadingGuilds = false;
  m_guildsHasMore = false;
  updateDataLoading();

  sortGuilds();
  syncGatewayOrderingStateToWorker();

  if (m_store) {
    m_store->setGuildFolders(m_guildFolders);
    m_store->setGuilds(m_guilds);
    scheduleGuildsCacheSave();
  }
  setStatusText("Connected");
  if (!m_loadingDmChannels && m_token.trimmed().isEmpty() == false) {
    m_lastDmChannelId.clear();
    m_dmChannelsHasMore = true;
    m_loadingDmChannels = true;
    updateDataLoading();
    setStatusText("Loading DMs...");
    if (m_networkWorker != 0) {
      QMetaObject::invokeMethod(m_networkWorker, "fetchDmChannels",
                                Qt::QueuedConnection, Q_ARG(QString, m_token),
                                Q_ARG(int, kPageSize),
                                Q_ARG(QString, QString()));
    }
  }
}

bool DiscordClient::applyGuildOrderFromGatewayPayload(
    const QVariantMap &payload) {
  return m_sortUtils->applyGuildOrderFromGatewayPayload(
      m_guilds, m_orderedGuildIds, payload);
}

void DiscordClient::sortGuilds() {
  m_sortUtils->applyGuildOrder(m_guilds, m_orderedGuildIds);
}

int DiscordClient::guildMentionCount(const QString &guildId) const {
  QString safeGuildId = guildId.trimmed();
  for (int i = 0; i < m_guilds.size(); ++i) {
    QVariantMap guild = m_guilds.at(i).toMap();
    if (guild.value("id").toString() == safeGuildId) {
      return guild.value("mentionCount").toInt();
    }
  }
  return 0;
}

void DiscordClient::updateGuildMentionCount(const QString &guildId,
                                            int mentionCount) {
  QString safeGuildId = guildId.trimmed();
  if (safeGuildId.isEmpty()) {
    return;
  }

  for (int i = 0; i < m_guilds.size(); ++i) {
    QVariantMap guild = m_guilds.at(i).toMap();
    if (guild.value("id").toString() == safeGuildId) {
      guild["mentionCount"] = mentionCount < 0 ? 0 : mentionCount;
      m_guilds.replace(i, guild);
      syncStateToNetworkWorker();
      // Fix: confirmed as a real bug - m_guilds (this class's own
      // working copy of the guild list) was updated above, but that
      // alone doesn't reach the UI. m_store->setGuilds() is only ever
      // called from loadMainData()/onGuildsLoaded() (the initial
      // load), so a badge/mention-count change made here after that
      // point never propagated to AppStore, and therefore never
      // reached the sidebar's badge/white-bar in QML - the server
      // list kept showing whatever it last had, even though m_guilds
      // (and any debug log built from it) already had the corrected
      // value. Push the updated list to the store here too, exactly
      // like the initial load does.
      if (m_store) {
        m_store->setGuilds(m_guilds);
      }
      return;
    }
  }
  // Fix: temporary diagnostic logging - if guildId is never found in
  // m_guilds here, this function silently does nothing (no badge
  // update, no log) - needed to pin down a reported bug where a
  // non-mention message's guild badge only seems to "take" after the
  // FIRST mention of a session has been read/cleared, never before.
  qDebug() << "[discord-chat] updateGuildMentionCount: guild" << safeGuildId
           << "not found in m_guilds (size=" << m_guilds.size() << ")";
}

void DiscordClient::updateGuildUnread(const QString &guildId, bool unread) {
  QString safeGuildId = guildId.trimmed();
  if (safeGuildId.isEmpty()) {
    return;
  }

  for (int i = 0; i < m_guilds.size(); ++i) {
    QVariantMap guild = m_guilds.at(i).toMap();
    if (guild.value("id").toString() == safeGuildId) {
      guild["unread"] = unread;
      m_guilds.replace(i, guild);
      syncStateToNetworkWorker();
      // Fix: see the matching comment in updateGuildMentionCount()
      // just above - same missing propagation to AppStore/QML, same
      // fix.
      if (m_store) {
        m_store->setGuilds(m_guilds);
      }
      return;
    }
  }
  // Fix: temporary diagnostic logging - see the matching comment in
  // updateGuildMentionCount() just above.
  qDebug() << "[discord-chat] updateGuildUnread: guild" << safeGuildId
           << "not found in m_guilds (size=" << m_guilds.size() << ")";
}
