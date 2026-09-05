#ifndef Gateway_HPP_
#define Gateway_HPP_

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <stdint.h>
#include <zlib.h>

struct mg_mgr;
struct mg_connection;
class QTimerEvent;

class DiscordGateway : public QObject {
  Q_OBJECT

public:
  enum ConnectionState { Disconnected, Connecting, Connected, Ready };

  // A single dropped UDP packet to the DNS resolver on a flaky mobile/Wi-Fi
  // connection can exhaust mongoose's (already-raised) DNS timeout with no
  // automatic retry. Allow a couple of silent reconnect attempts before
  // surfacing "DNS timeout" to the user - see Gateway.cpp/GatewayEvents.cpp.
  static const int kMaxDnsRetries = 2;

  explicit DiscordGateway(QObject *parent = 0);
  virtual ~DiscordGateway();

  Q_INVOKABLE void connectToGateway(const QString &token);
  Q_INVOKABLE void disconnectFromGateway();
  Q_INVOKABLE void sendLazyRequest(const QString &guildId,
                                   const QString &channelId);
  // Giống sendLazyRequest() (cùng payload op:14, cùng
  // buildGuildSubscribePayload()) nhưng BỎ QUA hoàn toàn cache
  // m_sentLazyRequests — luôn gửi request mới. Dùng riêng cho sheet
  // Members: sendLazyRequest() đã bị "tiêu" dedup key ngay khi user mở
  // channel (ChatController gọi trước đó), nên nếu sheet Members gọi lại
  // sendLazyRequest() với cùng guildId/channelId sẽ bị chặn im lặng,
  // không có SYNC mới nào trả về — đây là nguyên nhân sheet Members hiện
  // trống dù channel đã mở trước đó. Hàm này không ghi vào
  // m_sentLazyRequests để không ảnh hưởng dedup của luồng lazy-load tin
  // nhắn (subscribeToGuildChannel/Chat.cpp), tách biệt hoàn toàn 2 mối
  // quan tâm.
  Q_INVOKABLE void sendMemberListSync(const QString &guildId,
                                      const QString &channelId);
  // Gọi khi sheet Members đóng lại, để dừng việc tự động re-sync sau
  // reconnect (xem m_activeMemberListGuildId/ChannelId). Không bắt buộc
  // gọi hàm này để sendMemberListSync() hoạt động đúng - chỉ ảnh hưởng
  // tới việc có tự re-sync hay không nếu gateway reconnect SAU KHI user
  // đã rời tab.
  Q_INVOKABLE void clearMemberListSync();
  Q_INVOKABLE void updateMessageFilterState(const QString &selectedChannelId,
                                            const QStringList &loadedChannelIds,
                                            const QString &currentUserId);

  ConnectionState state() const;

Q_SIGNALS:
  void stateChanged(int state);
  void ready(const QString &sessionId);
  void dispatchReceived(const QString &eventName, const QVariantMap &payload);
  void error(const QString &message);
  void closed();

protected:
  void timerEvent(QTimerEvent *event);

private:
  static void eventHandler(struct mg_connection *connection, int event,
                           void *eventData);

  void handleEvent(struct mg_connection *connection, int event,
                   void *eventData);
  void handleTextMessage(const char *data, int length);
  void handleCompressedMessage(const char *data, int length);
  void handleHello(const QVariantMap &data);
  void handleDispatch(const QString &eventName, const QVariantMap &data,
                      int sequence);
  void sendHeartbeat();
  void sendIdentify();
  void sendJsonText(const QString &text);
  void initializeTls(struct mg_connection *connection);
  void setState(ConnectionState state);
  void resetSession();
  void flushPendingLazyRequests();
  QString lazyRequestKey(const QString &guildId,
                         const QString &channelId) const;
  void beginConnectAttempt();

  mg_mgr *m_mgr;
  mg_connection *m_connection;
  int m_timerId;
  int m_dnsRetriesLeft;
  QString m_token;
  QString m_sessionId;
  QString m_resumeGatewayUrl;
  QByteArray m_compressedBuffer;
  z_stream m_zstream;
  bool m_zstreamReady;
  int m_sequence;
  int m_heartbeatIntervalMs;
  uint64_t m_nextHeartbeatMs;
  ConnectionState m_state;
  QSet<QString> m_sentLazyRequests;
  QSet<QString> m_pendingLazyRequests;
  QString m_selectedChannelId;
  QStringList m_loadedChannelIds;
  QString m_currentUserId;
  // Guild/channel currently synced for the Members sheet, if open. Used to
  // silently re-send the member-list SYNC after a reconnect (Discord closes
  // the connection with code 4002 - see JsonParser.cpp - or the app itself
  // reconnects for other reasons - the server has no memory of the previous
  // subscription once the socket drops, so without this the Members sheet
  // stays permanently empty until the user closes and reopens it).
  QString m_activeMemberListGuildId;
  QString m_activeMemberListChannelId;
};

#endif /* Gateway_HPP_ */
