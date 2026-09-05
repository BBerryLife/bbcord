#ifndef GatewayWorker_HPP_
#define GatewayWorker_HPP_

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include "Gateway.hpp"

class DiscordGatewayWorker : public QObject {
  Q_OBJECT

public:
  explicit DiscordGatewayWorker(QObject *parent = 0);
  virtual ~DiscordGatewayWorker();

public Q_SLOTS:
  void connectGateway(const QString &token);
  void disconnectGateway();
  void sendLazyRequest(const QString &guildId, const QString &channelId);
  void sendMemberListSync(const QString &guildId, const QString &channelId);
  void clearMemberListSync();
  void updateGatewayOrderingState(const QVariantList &guilds,
                                  const QVariantList &allDmChannels,
                                  const QVariantList &visibleDmChannels,
                                  const QStringList &orderedGuildIds,
                                  const QVariantMap &dmPresenceByUserId);
  void updateGatewayMessageFilterState(const QString &selectedChannelId,
                                       const QStringList &loadedChannelIds,
                                       const QString &currentUserId);
  void onGatewayDispatchReceived(const QString &eventName,
                                 const QVariantMap &payload);
  void cancelAll();

Q_SIGNALS:
  void gatewayDispatchReceived(const QString &eventName,
                               const QVariantMap &payload);
  void guildsAndDmsReady(const QVariantList &guilds,
                         const QVariantList &allDmChannels,
                         const QVariantList &visibleDmChannels,
                         const QStringList &orderedGuildIds,
                         const QVariantMap &dmPresenceByUserId);
  void gatewayError(const QString &message);
  void gatewayClosed();
  void gatewayReady(const QString &sessionId);
  void cancelAllFinished();

private:
  DiscordGateway m_gateway;
  QVariantList m_gatewayGuilds;
  QVariantList m_gatewayAllDmChannels;
  QVariantList m_gatewayVisibleDmChannels;
  QStringList m_gatewayOrderedGuildIds;
  QVariantMap m_gatewayDmPresenceByUserId;
  QString m_selectedChannelId;
  QStringList m_loadedChannelIds;
  QString m_currentUserId;
};

#endif /* GatewayWorker_HPP_ */
