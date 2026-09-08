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

  // Diagnostic/tuning for the Simulator vs real-device timer behavior
  // difference - see the comment in Gateway.cpp::timerEvent(). Not a
  // confirmed fix, just a first attempt: gives mg_mgr_poll() a small
  // window instead of a 0ms poll, in case a slow/coalesced Qt timer on
  // the Simulator is causing mongoose to read a TLS/WS handshake
  // response in a partial or misaligned chunk.
  static const int kGatewayPollWaitMs = 5;

  explicit DiscordGateway(QObject *parent = 0);
  virtual ~DiscordGateway();

  Q_INVOKABLE void connectToGateway(const QString &token);
  Q_INVOKABLE void disconnectFromGateway();
  Q_INVOKABLE void sendLazyRequest(const QString &guildId,
                                   const QString &channelId);
  // Same as sendLazyRequest() (same op:14 payload, same
  // buildGuildSubscribePayload()) but SKIPS the m_sentLazyRequests
  // cache entirely — always sends a fresh request. Used specifically
  // for the Members sheet: sendLazyRequest() already consumes the
  // dedup key when the user opens the channel (called earlier by
  // ChatController), so if the Members sheet called sendLazyRequest()
  // again with the same guildId/channelId it would be silently
  // dropped, no new SYNC would come back — this was why the Members
  // sheet showed empty even though the channel was already open. This
  // function doesn't write to m_sentLazyRequests so it doesn't affect
  // dedup for the message lazy-load flow (subscribeToGuildChannel/
  // Chat.cpp) — the two concerns stay fully separate.
  Q_INVOKABLE void sendMemberListSync(const QString &guildId,
                                      const QString &channelId);
  // Call when the Members sheet closes, to stop auto re-sync after a
  // reconnect (see m_activeMemberListGuildId/ChannelId). Not required
  // for sendMemberListSync() to work correctly — only affects whether
  // an auto re-sync fires if the gateway reconnects AFTER the user has
  // already left the tab.
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
