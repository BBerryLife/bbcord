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

// Kết quả build sẵn cho 1 thông báo Hub, theo đúng 2 định dạng đã chốt:
//   Guild:     title = "Tên Server", preview = "Tên ai ping: nội dung"
//   DM/group:  title = "Tên người gửi", preview = "Replied: nội dung"
// shouldNotify=false nghĩa là tin nhắn này không đáng đẩy vào Hub theo quy
// tắc hiện tại (không ping mình ở guild; group DM nhưng không phải reply
// tới mình) — mọi field khác không có ý nghĩa khi shouldNotify=false.
struct MentionNotification {
  bool shouldNotify;
  QString sourceId; // channelId — dùng làm UDS source_id cho dòng Hub
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

  // Quyết định 1 payload MESSAGE_CREATE có đáng đẩy vào BlackBerry Hub hay
  // không, và nếu có thì build sẵn title/preview theo đúng 2 định dạng đã
  // thống nhất. payload cần đủ field (không dùng được với "light payload"
  // rút gọn từ GatewayEvents.cpp — cần channel_id/guild_id/author/content/
  // mentions/mention_everyone/mention_roles/referenced_message đầy đủ).
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
