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

  // A dropped UDP packet to the DNS resolver can exhaust mongoose's DNS timeout, so
  // allow a couple of silent reconnects before surfacing "DNS timeout"
  // (see Gateway.cpp/GatewayEvents.cpp).
  static const int kMaxDnsRetries = 2;

  // Tuning for Simulator vs device timer behavior (see Gateway.cpp::timerEvent()); an
  // unconfirmed first attempt: a small poll window instead of 0ms.
  static const int kGatewayPollWaitMs = 5;

  explicit DiscordGateway(QObject *parent = 0);
  virtual ~DiscordGateway();

  Q_INVOKABLE void connectToGateway(const QString &token);
  Q_INVOKABLE void disconnectFromGateway();
  Q_INVOKABLE void sendLazyRequest(const QString &guildId,
                                   const QString &channelId);
  // Same op:14 payload as sendLazyRequest() but skips the m_sentLazyRequests dedup
  // cache and always sends. Used by the Members sheet, whose request would otherwise
  // be dropped (the key is consumed on channel open). Message lazy-load dedup is unaffected.
  Q_INVOKABLE void sendMemberListSync(const QString &guildId,
                                      const QString &channelId);
  // Call when the Members sheet closes to stop the auto re-sync after a reconnect
  // (m_activeMemberListGuildId/ChannelId). Not needed for sendMemberListSync() itself.
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
  qint64 m_lastPollMs; // for the timer-gap diagnostic in timerEvent()
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
  // Guild/channel synced for the open Members sheet. Used to re-send the SYNC after a
  // reconnect (e.g. close code 4002), since the server forgets the subscription and
  // the sheet would stay empty.
  QString m_activeMemberListGuildId;
  QString m_activeMemberListChannelId;
};

#endif /* Gateway_HPP_ */
