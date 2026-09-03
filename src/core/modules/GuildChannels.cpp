#include "GuildChannels.hpp"

#include "../AppStore.hpp"
#include "../Client.hpp"
#include "../HubIntegration.hpp"
#include "../client/ItemMapper.hpp"
#include "../client/SortUtils.hpp"
#include "../discord/GatewayWorker.hpp"
#include "../discord/NetworkWorker.hpp"
#include "FeatureConstants.hpp"

#include <QMetaObject>

void DiscordClient::loadGuildChannels(const QString &guildId) {
  if (m_loadingGuilds || m_loadingDmChannels || guildId.trimmed().isEmpty() ||
      m_token.trimmed().isEmpty()) {
    return;
  }

  if (guildId.trimmed() == m_selectedGuildId && !m_allGuildChannels.isEmpty()) {
    return;
  }

  m_selectedGuildId = guildId.trimmed();
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

void DiscordClient::selectChannel(const QString &channelId) {
  QString safeChannelId = channelId.trimmed();
  if (safeChannelId.isEmpty()) {
    return;
  }

  bool channelStatusChanged = updateGuildChannelUnread(safeChannelId, false);
  channelStatusChanged =
      updateGuildChannelMentionCount(safeChannelId, 0) || channelStatusChanged;
  if (channelStatusChanged && m_store) {
    m_store->setGuildChannels(m_visibleGuildChannels);
  }
  if (m_store) {
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

void DiscordClient::onGuildChannelsLoaded(const QString &guildId,
                                          const QVariantList &channels) {
  if (guildId != m_selectedGuildId) {
    return;
  }

  m_loadingGuildChannels = false;
  updateDataLoading();
  m_allGuildChannels.clear();
  QVariantList rawChannels;
  for (int i = 0; i < channels.size(); ++i) {
    QVariantMap item = m_itemMapper->guildChannelToItem(channels.at(i).toMap());
    if (!item.value("id").toString().isEmpty()) {
      rawChannels.append(item);
    }
  }
  m_allGuildChannels = m_sortUtils->sortedAccessibleGuildChannels(rawChannels);

  appendVisibleGuildChannels();
  setStatusText("Connected");
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
