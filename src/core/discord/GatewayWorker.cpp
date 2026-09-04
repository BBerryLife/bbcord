#include "GatewayWorker.hpp"

#include "DiscordUtils.hpp"

#include <QDebug>
#include <QMetaObject>
#include <QThread>

namespace {
bool isInObjectThread(const QObject *object) {
  return object != 0 && object->thread() == QThread::currentThread();
}

bool applyGuildOrder(QVariantList &guilds, const QStringList &orderedGuildIds) {
  if (orderedGuildIds.isEmpty() || guilds.isEmpty()) {
    return false;
  }

  QVariantList ordered;
  QStringList usedIds;
  for (int i = 0; i < orderedGuildIds.size(); ++i) {
    QString guildId = orderedGuildIds.at(i).trimmed();
    if (guildId.isEmpty() || usedIds.contains(guildId)) {
      continue;
    }

    for (int j = 0; j < guilds.size(); ++j) {
      QVariantMap guild = guilds.at(j).toMap();
      if (guild.value("id").toString() == guildId) {
        ordered.append(guild);
        usedIds.append(guildId);
        break;
      }
    }
  }

  if (ordered.isEmpty()) {
    return false;
  }

  for (int i = 0; i < guilds.size(); ++i) {
    QVariantMap guild = guilds.at(i).toMap();
    if (!usedIds.contains(guild.value("id").toString())) {
      ordered.append(guild);
    }
  }

  guilds = ordered;
  return true;
}

bool applyGuildOrderFromGatewayPayload(QVariantList &guilds,
                                       QStringList &orderedGuildIds,
                                       const QVariantMap &payload) {
  QStringList newOrderedGuildIds;

  DiscordUtils::appendGuildIdsFromUserSettingsProto(
      &newOrderedGuildIds, payload.value("user_settings_proto").toString());

  QVariantMap settings = payload.value("settings").toMap();
  DiscordUtils::appendGuildIdsFromUserSettingsProto(
      &newOrderedGuildIds, settings.value("proto").toString());

  QVariantList folders = payload.value("guild_folders").toList();
  if (folders.isEmpty()) {
    QVariantMap userSettings = payload.value("user_settings").toMap();
    folders = userSettings.value("guild_folders").toList();
  }

  for (int i = 0; i < folders.size(); ++i) {
    QVariantMap folder = folders.at(i).toMap();
    QVariantList guildIds = folder.value("guild_ids").toList();
    for (int j = 0; j < guildIds.size(); ++j) {
      DiscordUtils::appendUniqueGuildId(&newOrderedGuildIds,
                                        guildIds.at(j).toString());
    }
  }

  if (newOrderedGuildIds.isEmpty()) {
    return false;
  }

  orderedGuildIds = newOrderedGuildIds;
  qDebug() << "[discord-gateway-worker] applied guild order from gateway"
           << orderedGuildIds.size();

  return applyGuildOrder(guilds, orderedGuildIds);
}

QString presenceStatusForRecipients(const QVariantList &userIds,
                                    const QVariantMap &dmPresenceByUserId) {
  QString bestStatus = "offline";
  int bestRank = 0;
  for (int i = 0; i < userIds.size(); ++i) {
    QString userId = userIds.at(i).toString().trimmed();
    QString status = dmPresenceByUserId.value(userId).toString();
    int rank = 0;
    if (status == "online") {
      rank = 3;
    } else if (status == "dnd" || status == "busy") {
      rank = 2;
    } else if (status == "idle") {
      rank = 1;
    }

    if (rank > bestRank) {
      bestRank = rank;
      bestStatus = status;
    }
  }
  return bestStatus;
}

QString presenceColor(const QString &status) {
  if (status == "online") {
    return "#23A55A";
  }
  if (status == "idle") {
    return "#F0B232";
  }
  if (status == "dnd" || status == "busy") {
    return "#F23F43";
  }
  return "#80848E";
}

void applyPresenceToDmChannels(QVariantList *channels,
                               const QVariantMap &dmPresenceByUserId) {
  if (channels == 0) {
    return;
  }

  for (int i = 0; i < channels->size(); ++i) {
    QVariantMap channel = channels->at(i).toMap();
    QVariantList recipientIds = channel.value("recipientIds").toList();
    QString status =
        presenceStatusForRecipients(recipientIds, dmPresenceByUserId);
    channel["status"] = status;
    channel["statusColor"] = presenceColor(status);
    channels->replace(i, channel);
  }
}

void moveDmToTop(QVariantList *channels, const QString &channelId,
                 const QString &lastMessageId) {
  if (channels == 0 || channelId.isEmpty()) {
    return;
  }

  for (int i = 0; i < channels->size(); ++i) {
    QVariantMap item = channels->at(i).toMap();
    if (item.value("id").toString() == channelId) {
      if (!lastMessageId.isEmpty()) {
        item["lastMessageId"] = lastMessageId;
      }
      channels->removeAt(i);
      channels->prepend(item);
      return;
    }
  }
}
} // namespace

DiscordGatewayWorker::DiscordGatewayWorker(QObject *parent)
    : QObject(parent), m_gateway(this) {
  connect(&m_gateway, SIGNAL(dispatchReceived(QString, QVariantMap)), this,
          SLOT(onGatewayDispatchReceived(QString, QVariantMap)));
  connect(&m_gateway, SIGNAL(error(QString)), this,
          SIGNAL(gatewayError(QString)));
  connect(&m_gateway, SIGNAL(closed()), this, SIGNAL(gatewayClosed()));
  connect(&m_gateway, SIGNAL(ready(QString)), this,
          SIGNAL(gatewayReady(QString)));
}

DiscordGatewayWorker::~DiscordGatewayWorker() { cancelAll(); }

void DiscordGatewayWorker::connectGateway(const QString &token) {
  if (!isInObjectThread(this)) {
    QMetaObject::invokeMethod(this, "connectGateway", Qt::QueuedConnection,
                              Q_ARG(QString, token));
    return;
  }

  m_gateway.connectToGateway(token);
}

void DiscordGatewayWorker::disconnectGateway() {
  if (!isInObjectThread(this)) {
    QMetaObject::invokeMethod(this, "disconnectGateway", Qt::QueuedConnection);
    return;
  }

  m_gateway.disconnectFromGateway();
}

void DiscordGatewayWorker::sendLazyRequest(const QString &guildId,
                                           const QString &channelId) {
  if (!isInObjectThread(this)) {
    QMetaObject::invokeMethod(this, "sendLazyRequest", Qt::QueuedConnection,
                              Q_ARG(QString, guildId),
                              Q_ARG(QString, channelId));
    return;
  }

  m_gateway.sendLazyRequest(guildId, channelId);
}

void DiscordGatewayWorker::sendMemberListSync(const QString &guildId,
                                              const QString &channelId) {
  if (!isInObjectThread(this)) {
    QMetaObject::invokeMethod(this, "sendMemberListSync", Qt::QueuedConnection,
                              Q_ARG(QString, guildId),
                              Q_ARG(QString, channelId));
    return;
  }

  m_gateway.sendMemberListSync(guildId, channelId);
}

void DiscordGatewayWorker::updateGatewayOrderingState(
    const QVariantList &guilds, const QVariantList &allDmChannels,
    const QVariantList &visibleDmChannels, const QStringList &orderedGuildIds,
    const QVariantMap &dmPresenceByUserId) {
  if (!isInObjectThread(this)) {
    QMetaObject::invokeMethod(this, "updateGatewayOrderingState",
                              Qt::QueuedConnection, Q_ARG(QVariantList, guilds),
                              Q_ARG(QVariantList, allDmChannels),
                              Q_ARG(QVariantList, visibleDmChannels),
                              Q_ARG(QStringList, orderedGuildIds),
                              Q_ARG(QVariantMap, dmPresenceByUserId));
    return;
  }

  m_gatewayGuilds = guilds;
  m_gatewayAllDmChannels = allDmChannels;
  m_gatewayVisibleDmChannels = visibleDmChannels;
  m_gatewayOrderedGuildIds = orderedGuildIds;
  m_gatewayDmPresenceByUserId = dmPresenceByUserId;
}

void DiscordGatewayWorker::updateGatewayMessageFilterState(
    const QString &selectedChannelId, const QStringList &loadedChannelIds,
    const QString &currentUserId) {
  if (!isInObjectThread(this)) {
    QMetaObject::invokeMethod(
        this, "updateGatewayMessageFilterState", Qt::QueuedConnection,
        Q_ARG(QString, selectedChannelId), Q_ARG(QStringList, loadedChannelIds),
        Q_ARG(QString, currentUserId));
    return;
  }

  m_selectedChannelId = selectedChannelId.trimmed();
  m_loadedChannelIds.clear();
  for (int i = 0; i < loadedChannelIds.size(); ++i) {
    QString channelId = loadedChannelIds.at(i).trimmed();
    if (!channelId.isEmpty() && !m_loadedChannelIds.contains(channelId)) {
      m_loadedChannelIds.append(channelId);
    }
  }
  m_currentUserId = currentUserId.trimmed();
  m_gateway.updateMessageFilterState(m_selectedChannelId, m_loadedChannelIds,
                                     m_currentUserId);
}

void DiscordGatewayWorker::onGatewayDispatchReceived(
    const QString &eventName, const QVariantMap &payload) {
  if (!isInObjectThread(this)) {
    QMetaObject::invokeMethod(this, "onGatewayDispatchReceived",
                              Qt::QueuedConnection, Q_ARG(QString, eventName),
                              Q_ARG(QVariantMap, payload));
    return;
  }

  emit gatewayDispatchReceived(eventName, payload);

  if (eventName == "MESSAGE_CREATE") {
    QString guildId = payload.value("guild_id").toString();
    QString channelId = payload.value("channel_id").toString().trimmed();
    if (guildId.isEmpty() && !channelId.isEmpty()) {
      QString lastMessageId = payload.value("id").toString().trimmed();
      moveDmToTop(&m_gatewayAllDmChannels, channelId, lastMessageId);
      moveDmToTop(&m_gatewayVisibleDmChannels, channelId, lastMessageId);
    }
    return;
  }

  if (eventName == "PRESENCE_UPDATE") {
    QString userId = payload.value("user").toMap().value("id").toString();
    QString status = payload.value("status").toString().trimmed();
    if (!userId.isEmpty()) {
      m_gatewayDmPresenceByUserId.insert(
          userId, status.isEmpty() ? QString("offline") : status);
    }
    return;
  }

  if (eventName == "READY") {
    QVariantList presences = payload.value("presences").toList();
    for (int i = 0; i < presences.size(); ++i) {
      QVariantMap presence = presences.at(i).toMap();
      QString userId = presence.value("user").toMap().value("id").toString();
      QString status = presence.value("status").toString().trimmed();
      if (!userId.isEmpty()) {
        m_gatewayDmPresenceByUserId.insert(
            userId, status.isEmpty() ? QString("offline") : status);
      }
    }
  }

  if (eventName != "GUILD_CREATE" && eventName != "GUILD_DELETE" &&
      eventName != "READY" && eventName != "USER_SETTINGS_PROTO_UPDATE") {
    return;
  }

  bool orderChanged = applyGuildOrderFromGatewayPayload(
      m_gatewayGuilds, m_gatewayOrderedGuildIds, payload);
  if (!orderChanged) {
    applyGuildOrder(m_gatewayGuilds, m_gatewayOrderedGuildIds);
  }

  applyPresenceToDmChannels(&m_gatewayAllDmChannels,
                            m_gatewayDmPresenceByUserId);
  applyPresenceToDmChannels(&m_gatewayVisibleDmChannels,
                            m_gatewayDmPresenceByUserId);
  DiscordUtils::stableSortItems(&m_gatewayAllDmChannels,
                                DiscordUtils::dmShouldMoveBefore);
  DiscordUtils::stableSortItems(&m_gatewayVisibleDmChannels,
                                DiscordUtils::dmShouldMoveBefore);

  emit guildsAndDmsReady(m_gatewayGuilds, m_gatewayAllDmChannels,
                         m_gatewayVisibleDmChannels, m_gatewayOrderedGuildIds,
                         m_gatewayDmPresenceByUserId);
}

void DiscordGatewayWorker::cancelAll() {
  if (!isInObjectThread(this)) {
    QMetaObject::invokeMethod(this, "cancelAll", Qt::QueuedConnection);
    return;
  }

  m_gateway.disconnectFromGateway();
  emit cancelAllFinished();
}
