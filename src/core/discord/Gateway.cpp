#include "Gateway.hpp"

#include "DiscordUtils.hpp"
#include "JsonParser.hpp"

#include <QByteArray>
#include <QDebug>

extern "C" {
#include "mongoose.h"
}

#include <string.h>

namespace {
const char *kGatewayUrl =
    "wss://gateway.discord.gg/?v=10&encoding=json&compress=zlib-stream";
const char *kGatewayHost = "gateway.discord.gg";
const int kGatewayPollIntervalMs = 10;

// Raise mongoose's 3s default DNS timeout (see RestClient.cpp); gateway.discord.gg is equally prone to spurious timeouts.
const int kGatewayDnsTimeoutMs = 10000;
} // namespace

DiscordGateway::DiscordGateway(QObject *parent)
    : QObject(parent), m_connection(NULL), m_timerId(0), m_dnsRetriesLeft(0),
      m_lastPollMs(0), m_zstreamReady(false), m_sequence(-1),
      m_heartbeatIntervalMs(0), m_nextHeartbeatMs(0), m_state(Disconnected) {
  m_mgr = new mg_mgr;
  mg_mgr_init(m_mgr);
  m_mgr->dnstimeout = kGatewayDnsTimeoutMs;
  mg_log_set(MG_LL_NONE);
  memset(&m_zstream, 0, sizeof(m_zstream));
  m_zstreamReady = inflateInit(&m_zstream) == Z_OK;
}

DiscordGateway::~DiscordGateway() {
  disconnectFromGateway();
  if (m_zstreamReady) {
    inflateEnd(&m_zstream);
    m_zstreamReady = false;
  }
  mg_mgr_free(m_mgr);
  delete m_mgr;
}

void DiscordGateway::connectToGateway(const QString &token) {
  disconnectFromGateway();
  resetSession();

  m_token = token.trimmed();
  if (m_token.isEmpty()) {
    emit error("Discord token is empty");
    return;
  }

  m_dnsRetriesLeft = kMaxDnsRetries;
  beginConnectAttempt();
}

void DiscordGateway::beginConnectAttempt() {
  setState(Connecting);

  QByteArray headers = DiscordUtils::desktopUserAgentHeader() +
                       QByteArray("Origin: https://discord.com\r\n");

  m_connection = mg_ws_connect(m_mgr, kGatewayUrl, DiscordGateway::eventHandler,
                               this, headers.constData());

  if (m_connection == NULL) {
    setState(Disconnected);
    emit error("Could not create Discord gateway connection");
    return;
  }

  if (m_timerId == 0) {
    m_timerId = startTimer(kGatewayPollIntervalMs);
  }
}

void DiscordGateway::disconnectFromGateway() {
  if (m_timerId != 0) {
    killTimer(m_timerId);
    m_timerId = 0;
  }

  if (m_connection != NULL) {
    m_connection->fn_data = NULL;
    if (!m_connection->is_closing && m_connection->is_websocket) {
      mg_ws_send(m_connection, "", 0, WEBSOCKET_OP_CLOSE);
    }

    if (!m_connection->is_closing) {
      m_connection->is_closing = 1;
    }

    for (int i = 0; i < 10 && m_connection != NULL; ++i) {
      mg_mgr_poll(m_mgr, 50);
    }
  }

  m_connection = NULL;
  setState(Disconnected);
}

DiscordGateway::ConnectionState DiscordGateway::state() const {
  return m_state;
}

void DiscordGateway::sendLazyRequest(const QString &guildId,
                                     const QString &channelId) {
  QString safeGuildId = guildId.trimmed();
  QString safeChannelId = channelId.trimmed();
  if (safeGuildId.isEmpty()) {
    return;
  }

  QString key = lazyRequestKey(safeGuildId, safeChannelId);
  if (m_sentLazyRequests.contains(key)) {
    return;
  }

  if (m_state != Ready || m_connection == NULL || !m_connection->is_websocket ||
      m_connection->is_closing) {
    m_pendingLazyRequests.insert(key);
    qDebug() << "[discord-gateway] subscribe request pending; gateway not ready"
             << "state" << m_state << "guild" << safeGuildId << "channel"
             << safeChannelId;
    return;
  }

  QString errorMessage;
  QByteArray payload = DiscordJsonParser::buildGuildSubscribePayload(
      safeGuildId, safeChannelId, &errorMessage);
  if (!errorMessage.isEmpty()) {
    emit error(
        QString("Gateway subscribe request JSON error: %1").arg(errorMessage));
    return;
  }

  sendJsonText(QString::fromUtf8(payload.constData(), payload.size()));
  m_sentLazyRequests.insert(key);
  m_pendingLazyRequests.remove(key);
  qDebug() << "[discord-gateway] subscribe request sent"
           << "guild" << safeGuildId << "channel" << safeChannelId;
}

void DiscordGateway::sendMemberListSync(const QString &guildId,
                                        const QString &channelId) {
  QString safeGuildId = guildId.trimmed();
  QString safeChannelId = channelId.trimmed();
  if (safeGuildId.isEmpty()) {
    return;
  }

  // Unlike sendLazyRequest(), does not check m_sentLazyRequests: the Members sheet needs a fresh SYNC on every open.
  if (m_state != Ready || m_connection == NULL || !m_connection->is_websocket ||
      m_connection->is_closing) {
    qDebug() << "[discord-gateway] member-list sync request dropped; "
                "gateway not ready"
             << "state" << m_state << "guild" << safeGuildId << "channel"
             << safeChannelId;
    return;
  }

  // Discord will not resend SYNC for a subscribe identical to the existing one
  // (same guild_id and range [0,99], already sent by sendLazyRequest()). Send an
  // "unsubscribe" (no "channels") first so the real payload counts as a change.
  // Both are sent back-to-back; TCP preserves their order.
  QString unsubscribeErrorMessage;
  QByteArray unsubscribePayload = DiscordJsonParser::buildMemberListUnsubscribePayload(
      safeGuildId, &unsubscribeErrorMessage);
  if (unsubscribeErrorMessage.isEmpty() && !unsubscribePayload.isEmpty()) {
    sendJsonText(
        QString::fromUtf8(unsubscribePayload.constData(), unsubscribePayload.size()));
  }

  QString errorMessage;
  QByteArray payload = DiscordJsonParser::buildMemberListSyncPayload(
      safeGuildId, safeChannelId, &errorMessage);
  if (!errorMessage.isEmpty()) {
    emit error(QString("Gateway member-list sync JSON error: %1")
                   .arg(errorMessage));
    return;
  }

  qDebug() << "[discord-gateway] member-list sync payload"
           << QString::fromUtf8(payload.constData(), payload.size());

  sendJsonText(QString::fromUtf8(payload.constData(), payload.size()));
  m_activeMemberListGuildId = safeGuildId;
  m_activeMemberListChannelId = safeChannelId;
  qDebug() << "[discord-gateway] member-list sync request sent"
           << "guild" << safeGuildId << "channel" << safeChannelId;
}

void DiscordGateway::clearMemberListSync() {
  m_activeMemberListGuildId.clear();
  m_activeMemberListChannelId.clear();
}

void DiscordGateway::updateMessageFilterState(
    const QString &selectedChannelId, const QStringList &loadedChannelIds,
    const QString &currentUserId) {
  m_selectedChannelId = selectedChannelId.trimmed();
  m_loadedChannelIds.clear();
  for (int i = 0; i < loadedChannelIds.size(); ++i) {
    QString channelId = loadedChannelIds.at(i).trimmed();
    if (!channelId.isEmpty() && !m_loadedChannelIds.contains(channelId)) {
      m_loadedChannelIds.append(channelId);
    }
  }
  m_currentUserId = currentUserId.trimmed();
  qDebug() << "[discord-gateway] message filter updated"
           << "selected" << m_selectedChannelId << "loaded"
           << m_loadedChannelIds.size() << "hasCurrentUser"
           << !m_currentUserId.isEmpty();
}

void DiscordGateway::timerEvent(QTimerEvent *event) {
  Q_UNUSED(event);

  if (m_connection == NULL) {
    if (m_timerId != 0) {
      killTimer(m_timerId);
      m_timerId = 0;
    }
    return;
  }

  // Diagnostic: on the Simulator (QNX in VirtualBox) the 10ms timer can be delayed,
  // so several polls of TCP data pile up and mongoose may misread a TLS/WS handshake
  // boundary. Log abnormally large gaps between calls.
  qint64 nowMs = mg_millis();
  if (m_lastPollMs != 0) {
    qint64 gapMs = nowMs - m_lastPollMs;
    if (gapMs > kGatewayPollIntervalMs * 3) {
      qDebug() << "[discord-gateway] timer gap" << gapMs
               << "ms (expected ~" << kGatewayPollIntervalMs << "ms) - "
                  "poll may be processing coalesced data";
    }
  }
  m_lastPollMs = nowMs;

  // Raised from 0ms: a 0ms poll returns at once and can cut a trickling handshake
  // response mid-frame; a short window lets mongoose read a full chunk.
  mg_mgr_poll(m_mgr, kGatewayPollWaitMs);

  if (m_connection != NULL && m_connection->is_websocket &&
      m_heartbeatIntervalMs > 0 && mg_millis() >= m_nextHeartbeatMs) {
    sendHeartbeat();
  }
}

void DiscordGateway::sendHeartbeat() {
  QString sequence = m_sequence >= 0 ? QString::number(m_sequence) : "null";
  sendJsonText(QString("{\"op\":1,\"d\":%1}").arg(sequence));
  m_nextHeartbeatMs = mg_millis() + m_heartbeatIntervalMs;
}

void DiscordGateway::sendIdentify() {
  QString errorMessage;
  QByteArray payload =
      DiscordJsonParser::buildIdentifyPayload(m_token, &errorMessage);
  if (!errorMessage.isEmpty()) {
    emit error(QString("Gateway identify JSON error: %1").arg(errorMessage));
    return;
  }

  qDebug() << "[discord-gateway] identify sent"
           << "guild_subscriptions=false"
           << "payloadBytes" << payload.size();
  sendJsonText(QString::fromUtf8(payload.constData(), payload.size()));
}

void DiscordGateway::sendJsonText(const QString &text) {
  if (m_connection == NULL || !m_connection->is_websocket ||
      m_connection->is_closing) {
    return;
  }

  QByteArray payload = text.toUtf8();
  mg_ws_send(m_connection, payload.constData(), payload.size(),
             WEBSOCKET_OP_TEXT);
}

void DiscordGateway::initializeTls(struct mg_connection *connection) {
  if (connection != NULL && connection->is_tls) {
    struct mg_tls_opts opts;
    memset(&opts, 0, sizeof(opts));
    opts.name = mg_str(kGatewayHost);
    opts.skip_verification = true;
    mg_tls_init(connection, &opts);
  }
}

void DiscordGateway::setState(ConnectionState state) {
  if (m_state == state) {
    return;
  }

  m_state = state;
  emit stateChanged(static_cast<int>(m_state));
}

void DiscordGateway::resetSession() {
  m_sequence = -1;
  m_heartbeatIntervalMs = 0;
  m_nextHeartbeatMs = 0;
  m_sessionId.clear();
  m_resumeGatewayUrl.clear();
  m_compressedBuffer.clear();
  if (m_zstreamReady) {
    inflateReset(&m_zstream);
  }
  m_sentLazyRequests.clear();
  m_pendingLazyRequests.clear();
}

void DiscordGateway::flushPendingLazyRequests() {
  if (m_state != Ready || m_pendingLazyRequests.isEmpty()) {
    return;
  }

  QList<QString> pendingRequests = m_pendingLazyRequests.toList();
  m_pendingLazyRequests.clear();
  qDebug() << "[discord-gateway] flushing pending subscribe requests"
           << pendingRequests.size();
  for (int i = 0; i < pendingRequests.size(); ++i) {
    QString key = pendingRequests.at(i);
    int separator = key.indexOf(':');
    if (separator <= 0) {
      continue;
    }
    sendLazyRequest(key.left(separator), key.mid(separator + 1));
  }
}

QString DiscordGateway::lazyRequestKey(const QString &guildId,
                                       const QString &channelId) const {
  return QString("%1:%2").arg(guildId.trimmed()).arg(channelId.trimmed());
}
