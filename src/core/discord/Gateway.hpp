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

  mg_mgr *m_mgr;
  mg_connection *m_connection;
  int m_timerId;
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
};

#endif /* Gateway_HPP_ */
