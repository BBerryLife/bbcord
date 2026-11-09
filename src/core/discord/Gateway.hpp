#ifndef Gateway_HPP_
#define Gateway_HPP_

#include "StreamScanner.hpp"
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
  // Reconnects to an EXISTING session with op 6 RESUME instead of IDENTIFY, so Discord
  // sends no READY (~5 MB) - only the events missed since `sequence`. Used by the headless
  // service (3 MB heap limit) to take over the session the UI left (ServiceSession.hpp),
  // and to reconnect after a network drop. If Discord refuses the session,
  // sessionInvalid() is emitted and nothing else is sent.
  Q_INVOKABLE void resumeGateway(const QString &token, const QString &sessionId,
                                 const QString &resumeUrl, int sequence);
  // Poll period of the mongoose loop. 10 ms (default) suits the UI; an always-on
  // background service should use a much larger value to stay off the CPU.
  // Closes the socket WITHOUT a WebSocket close frame (after saving the session for the
  // service). A clean close makes Discord invalidate the session, so a RESUME would be
  // refused. Used when the UI shuts down.
  void dropConnection();
  void setPollIntervalMs(int ms);
  // Headless-service mode for a fresh IDENTIFY: the ~5 MB READY is inflated chunk by chunk
  // and only session_id / resume_gateway_url are kept (ReadyScanner); any other message
  // over 128 KB is dropped. Normal-sized messages are handled as usual. Off by default.
  void setLightStreamMode(bool enabled) { m_lightStream = enabled; }
  // True if the last op 9 said "resumable" (d:true); false means the session is gone.
  bool lastInvalidResumable() const { return m_lastInvalidResumable; }
  // "guildId:channelId" of every channel subscribed with op 14 on this session.
  QStringList subscribedKeys() const;
  // Opt-in (the headless service): if a heartbeat is still unanswered when the next one is
  // due, the connection is dead without TCP noticing; drop it so the owner can RESUME.
  void setHeartbeatWatchdog(bool enabled) { m_heartbeatWatchdog = enabled; }
  QString sessionId() const { return m_sessionId; }
  QString resumeGatewayUrl() const { return m_resumeGatewayUrl; }
  int sequence() const { return m_sequence; }
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
  // Discord rejected a RESUME (op 9): the session is gone and only a full IDENTIFY
  // (a new READY) can replace it.
  void sessionInvalid();

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
  void sendResume();
  void handleStreamedChunk(const char *data, int length);
  void consumeInflated(const char *data, int length);
  void finishStreamedMessage();
  void finishStreamedReady();
  void resetStreamState();
  // Saves session_id / url / seq for the service (ServiceSession.hpp). Throttled unless force.
  void persistSession(bool force);
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
  int m_pollIntervalMs;
  bool m_resumeRequested;   // the next HELLO answers with RESUME instead of IDENTIFY
  bool m_heartbeatWatchdog;
  bool m_awaitingAck;
  bool m_lightStream;
  bool m_lastInvalidResumable;
  ReadyScanner m_readyScanner;
  QByteArray m_msgHead;     // light mode: start of the message being inflated
  QByteArray m_zTail;       // light mode: last 4 compressed bytes (message end marker)
  bool m_msgOverflow;
  bool m_msgIsReady;
  int m_msgSequence;
  bool m_abruptClose;       // destructor: drop the socket without a close frame
  qint64 m_lastPersistMs;
  QByteArray m_urlBytes;    // mongoose keeps the pointers, so these must outlive connect
  QByteArray m_hostBytes;
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
