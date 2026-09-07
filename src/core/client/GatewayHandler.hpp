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

// Pre-built result for a Hub notification, following the two agreed
// formats:
//   Guild:     title = "Server Name", preview = "Who pinged: content"
//   DM/group:  title = "Sender Name", preview = "Replied: content"
// shouldNotify=false means this message isn't worth pushing to the Hub
// under current rules (no mention of us in a guild; group DM that isn't
// a reply to us) — all other fields are meaningless when shouldNotify
// is false.
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

  // Decides whether a MESSAGE_CREATE payload is worth pushing to the
  // BlackBerry Hub, and if so, pre-builds its title/preview in the two
  // agreed formats. Needs the full payload (won't work with the
  // trimmed-down "light payload" from GatewayEvents.cpp — requires
  // channel_id/guild_id/author/content/mentions/mention_everyone/
  // mention_roles/referenced_message all present).
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
