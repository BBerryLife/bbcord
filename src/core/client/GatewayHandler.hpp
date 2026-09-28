#ifndef GATEWAYHANDLER_HPP_
#define GATEWAYHANDLER_HPP_

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include "../models/Models.hpp"

class DiscordClient;

class AppStore;
class QTimer;

// Pre-built Hub notification result.
//   Guild:    title = "Server Name", preview = "Who pinged: content"
//   DM/group: title = "Sender Name", preview = "Replied: content"
// shouldNotify=false means not worth pushing (no mention of us in a guild; group DM
// not replying to us); other fields are then meaningless.
struct MentionNotification {
  bool shouldNotify;
  QString sourceId; // channelId — used as the UDS source_id for the Hub entry
  QString title;
  QString preview;
  qint64 timestampMs;

  MentionNotification() : shouldNotify(false), timestampMs(0) {}
};

class GatewayHandler : public QObject {

  Q_OBJECT
public:
  explicit GatewayHandler(DiscordClient *client, AppStore *store,
                          QObject *parent = 0);

  virtual ~GatewayHandler();

  void applyGatewayOrderingEvent(const QString &eventName,
                                 const QVariantMap &payload,
                                 QStringList &pendingUnreadGuildIds,
                                 QVariantMap &pendingMentionCountsByGuildId,
                                 QVariantMap &pendingMentionCountsByChannelId,
                                 QStringList &pendingUnreadChannelIds,
                                 bool &pendingDmUiUpdate,
                                 bool &gatewayUiUpdateQueued);

  bool gatewayMessageMentionsCurrentUser(const QVariantMap &payload) const;

  // Decides whether a MESSAGE_CREATE is worth pushing to Hub and pre-builds its
  // title/preview. Needs the full payload, not the "light" one from GatewayEvents.cpp.
  MentionNotification
  buildMentionNotification(const QVariantMap &payload) const;

private Q_SLOTS:
  void flushMessageQueue();

private:
  bool shouldApplyChatEvent(const QString &channelId) const;

  DiscordClient *m_client;

  AppStore *m_store;
  QList<DiscordMessage> m_messageQueue;
  QTimer *m_batchTimer;
};

#endif /* GATEWAYHANDLER_HPP_ */
