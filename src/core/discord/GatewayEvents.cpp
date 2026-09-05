#include "Gateway.hpp"

#include "JsonParser.hpp"

#include <QByteArray>
#include <QDebug>
#include <QVariantList>

extern "C" {
#include "mongoose.h"
}

namespace {
const int kMaxCompressedBufferSize = 2 * 1024 * 1024;

int extractSequence(const QByteArray &bytes) {
  int pos = bytes.indexOf("\"s\"");
  if (pos < 0) {
    return -1;
  }

  pos = bytes.indexOf(':', pos + 3);
  if (pos < 0) {
    return -1;
  }

  ++pos;
  while (pos < bytes.size() &&
         (bytes.at(pos) == ' ' || bytes.at(pos) == '\t' ||
          bytes.at(pos) == '\r' || bytes.at(pos) == '\n')) {
    ++pos;
  }

  if (pos < bytes.size() && bytes.mid(pos, 4) == "null") {
    return -1;
  }

  bool negative = false;
  if (pos < bytes.size() && bytes.at(pos) == '-') {
    negative = true;
    ++pos;
  }

  int value = 0;
  bool foundDigit = false;
  while (pos < bytes.size() && bytes.at(pos) >= '0' && bytes.at(pos) <= '9') {
    foundDigit = true;
    value = value * 10 + (bytes.at(pos) - '0');
    ++pos;
  }

  if (!foundDigit) {
    return -1;
  }
  return negative ? -value : value;
}

bool shouldParseDispatch(const QString &eventName) {
  return eventName == "MESSAGE_CREATE" || eventName == "MESSAGE_UPDATE" ||
         eventName == "MESSAGE_DELETE" || eventName == "READY" ||
         eventName == "GUILD_CREATE" || eventName == "GUILD_DELETE" ||
         eventName == "USER_SETTINGS_PROTO_UPDATE" ||
         eventName == "PRESENCE_UPDATE" ||
         eventName == "GUILD_MEMBER_LIST_UPDATE";
}

QByteArray extractArrayBytes(const QByteArray &bytes, const char *fieldName) {
  QByteArray marker = QByteArray("\"") + fieldName + QByteArray("\"");
  int pos = bytes.indexOf(marker);
  if (pos < 0) {
    return QByteArray();
  }

  pos = bytes.indexOf(':', pos + marker.size());
  if (pos < 0) {
    return QByteArray();
  }

  ++pos;
  while (pos < bytes.size() &&
         (bytes.at(pos) == ' ' || bytes.at(pos) == '\t' ||
          bytes.at(pos) == '\r' || bytes.at(pos) == '\n')) {
    ++pos;
  }

  if (pos >= bytes.size() || bytes.at(pos) != '[') {
    return QByteArray();
  }

  int start = pos;
  int depth = 0;
  bool inString = false;
  bool escaped = false;
  for (; pos < bytes.size(); ++pos) {
    char ch = bytes.at(pos);
    if (inString) {
      if (escaped) {
        escaped = false;
      } else if (ch == '\\') {
        escaped = true;
      } else if (ch == '"') {
        inString = false;
      }
      continue;
    }

    if (ch == '"') {
      inString = true;
    } else if (ch == '[') {
      ++depth;
    } else if (ch == ']') {
      --depth;
      if (depth == 0) {
        return bytes.mid(start, pos - start + 1);
      }
    }
  }

  return QByteArray();
}

bool mentionsArrayContainsUserId(const QByteArray &bytes,
                                 const QString &userId) {
  if (userId.isEmpty()) {
    return false;
  }

  QByteArray mentionsBytes = extractArrayBytes(bytes, "mentions");
  if (mentionsBytes.isEmpty()) {
    return false;
  }

  QByteArray compact = QByteArray("\"id\":\"") + userId.toUtf8() + "\"";
  QByteArray spaced = QByteArray("\"id\": \"") + userId.toUtf8() + "\"";
  return mentionsBytes.indexOf(compact) >= 0 ||
         mentionsBytes.indexOf(spaced) >= 0;
}

QVariantMap buildLightMessageCreatePayload(const QByteArray &bytes,
                                           const QString &currentUserId) {
  QByteArray dataBytes = DiscordJsonParser::extractObjectField(bytes, "d");
  QVariantMap payload;
  payload["id"] = DiscordJsonParser::extractStringField(dataBytes, "id");
  payload["channel_id"] =
      DiscordJsonParser::extractStringField(dataBytes, "channel_id");
  payload["guild_id"] =
      DiscordJsonParser::extractStringField(dataBytes, "guild_id");
  payload["mention_everyone"] =
      DiscordJsonParser::extractBoolField(dataBytes, "mention_everyone", false);

  QByteArray authorBytes =
      DiscordJsonParser::extractObjectField(dataBytes, "author");
  QVariantMap author;
  author["id"] = DiscordJsonParser::extractStringField(authorBytes, "id");
  payload["author"] = author;

  if (!payload.value("mention_everyone").toBool() &&
      mentionsArrayContainsUserId(dataBytes, currentUserId)) {
    payload["mention_count"] = 1;
  }
  return payload;
}

QVariantMap buildLightReadyPayload(const QByteArray &bytes) {
  QByteArray dataBytes = DiscordJsonParser::extractObjectField(bytes, "d");
  QVariantMap payload;
  payload["session_id"] =
      DiscordJsonParser::extractStringField(dataBytes, "session_id");
  payload["resume_gateway_url"] =
      DiscordJsonParser::extractStringField(dataBytes, "resume_gateway_url");
  payload["user_settings_proto"] =
      DiscordJsonParser::extractStringField(dataBytes, "user_settings_proto");
  payload["guild_folders"] =
      DiscordJsonParser::extractArrayField(dataBytes, "guild_folders");

  QByteArray settingsBytes =
      DiscordJsonParser::extractObjectField(dataBytes, "settings");
  if (!settingsBytes.isEmpty()) {
    QVariantMap settings;
    settings["proto"] =
        DiscordJsonParser::extractStringField(settingsBytes, "proto");
    payload["settings"] = settings;
  }

  return payload;
}
} // namespace

void DiscordGateway::eventHandler(struct mg_connection *connection, int event,
                                  void *eventData) {
  DiscordGateway *gateway = static_cast<DiscordGateway *>(connection->fn_data);
  if (gateway != NULL) {
    gateway->handleEvent(connection, event, eventData);
  }
}

void DiscordGateway::handleEvent(struct mg_connection *connection, int event,
                                 void *eventData) {
  if (event != MG_EV_POLL && event != MG_EV_READ && event != MG_EV_WRITE &&
      event != MG_EV_WS_MSG) {
    qDebug() << "[discord-gateway] event" << event << "fd=" << connection->fd
             << "tls=" << connection->is_tls
             << "ws=" << connection->is_websocket
             << "closing=" << connection->is_closing;
  }

  switch (event) {
  case MG_EV_CONNECT:
    initializeTls(connection);
    break;

  case MG_EV_RESOLVE:
    qDebug() << "[discord-gateway] resolved host";
    break;

  case MG_EV_WS_OPEN:
    setState(Connected);
    break;

  case MG_EV_WS_MSG: {
    struct mg_ws_message *message =
        static_cast<struct mg_ws_message *>(eventData);
    handleCompressedMessage(message->data.buf,
                            static_cast<int>(message->data.len));
    break;
  }

  case MG_EV_WS_CTL: {
    struct mg_ws_message *message =
        static_cast<struct mg_ws_message *>(eventData);
    int opcode = message != NULL ? (message->flags & 0x0f) : -1;
    int closeCode = -1;
    if (opcode == WEBSOCKET_OP_CLOSE && message != NULL &&
        message->data.len >= 2) {
      const unsigned char *bytes =
          reinterpret_cast<const unsigned char *>(message->data.buf);
      closeCode =
          (static_cast<int>(bytes[0]) << 8) | static_cast<int>(bytes[1]);
    }
    qDebug() << "[discord-gateway] control frame opcode" << opcode
             << "closeCode" << closeCode << "payloadBytes"
             << (message != NULL ? static_cast<int>(message->data.len) : 0);
    break;
  }

  case MG_EV_ERROR: {
    QString message = QString::fromUtf8(
        eventData ? static_cast<char *>(eventData) : "Gateway error");

    qDebug() << "[discord-gateway] error" << message;

    // A dropped UDP packet to the DNS resolver on a flaky mobile/Wi-Fi
    // connection surfaces here as "DNS timeout" even though the network is
    // otherwise fine (the REST login that just ran a few seconds earlier is
    // proof of that). Retry the resolve/connect a couple of times before
    // giving up and telling the user - see kMaxDnsRetries. Other
    // errors (TLS failures, refused connections, etc.) are surfaced
    // immediately, same as before.
    if (message.contains("DNS timeout", Qt::CaseInsensitive) &&
        m_dnsRetriesLeft > 0) {
      // (see kMaxDnsRetries in Gateway.hpp)
      --m_dnsRetriesLeft;
      qDebug() << "[discord-gateway] retrying after DNS timeout, retries left"
               << m_dnsRetriesLeft;
      // Don't reconnect here: MG_EV_CLOSE always follows MG_EV_ERROR for a
      // failed connect, and that's where mongoose finishes tearing down
      // this connection. Reconnecting now would race that teardown.
      break;
    }

    emit error(message);
    break;
  }

  case MG_EV_CLOSE: {
    bool wasCurrentConnection = (m_connection == connection);
    if (wasCurrentConnection) {
      m_connection = NULL;
    }
    if (m_timerId != 0) {
      killTimer(m_timerId);
      m_timerId = 0;
    }

    if (wasCurrentConnection && m_state != Ready &&
        m_dnsRetriesLeft < kMaxDnsRetries && !m_token.isEmpty()) {
      // We consumed a retry in MG_EV_ERROR above; the failed connection has
      // now fully torn down, so it's safe to open a fresh one.
      beginConnectAttempt();
      break;
    }

    setState(Disconnected);
    emit closed();
    break;
  }
  }
}

void DiscordGateway::handleCompressedMessage(const char *data, int length) {
  if (!m_zstreamReady || data == NULL || length <= 0) {
    handleTextMessage(data, length);
    return;
  }

  m_compressedBuffer.append(data, length);
  if (m_compressedBuffer.size() > kMaxCompressedBufferSize) {
    emit error("Gateway compressed buffer overflow");
    if (m_zstreamReady) {
      inflateReset(&m_zstream);
    }
    m_compressedBuffer.clear();
    return;
  }

  if (m_compressedBuffer.size() < 4 ||
      static_cast<unsigned char>(
          m_compressedBuffer.at(m_compressedBuffer.size() - 4)) != 0x00 ||
      static_cast<unsigned char>(
          m_compressedBuffer.at(m_compressedBuffer.size() - 3)) != 0x00 ||
      static_cast<unsigned char>(
          m_compressedBuffer.at(m_compressedBuffer.size() - 2)) != 0xff ||
      static_cast<unsigned char>(
          m_compressedBuffer.at(m_compressedBuffer.size() - 1)) != 0xff) {
    return;
  }

  QByteArray inflated;
  char output[32768];
  m_zstream.next_in = reinterpret_cast<Bytef *>(m_compressedBuffer.data());
  m_zstream.avail_in = static_cast<uInt>(m_compressedBuffer.size());

  int result = Z_OK;
  do {
    m_zstream.next_out = reinterpret_cast<Bytef *>(output);
    m_zstream.avail_out = sizeof(output);
    result = inflate(&m_zstream, Z_SYNC_FLUSH);
    if (result != Z_OK && result != Z_BUF_ERROR) {
      emit error(QString("Gateway zlib inflate error %1").arg(result));
      inflateReset(&m_zstream);
      m_compressedBuffer.clear();
      return;
    }

    int produced = static_cast<int>(sizeof(output) - m_zstream.avail_out);
    if (produced > 0) {
      inflated.append(output, produced);
    }
  } while (m_zstream.avail_out == 0);

  m_compressedBuffer.clear();
  if (!inflated.isEmpty()) {
    handleTextMessage(inflated.constData(), inflated.size());
  }
}

void DiscordGateway::handleTextMessage(const char *data, int length) {
  QByteArray bytes(data, length);

  QString fastEventName = DiscordJsonParser::extractStringField(bytes, "t");
  if (!fastEventName.isEmpty() && !shouldParseDispatch(fastEventName)) {
    int sequence = extractSequence(bytes);
    if (sequence >= 0) {
      m_sequence = sequence;
    }
    return;
  }

  if (fastEventName == "MESSAGE_CREATE") {
    int sequence = extractSequence(bytes);
    if (sequence >= 0) {
      m_sequence = sequence;
    }

    QString channelId =
        DiscordJsonParser::extractStringField(bytes, "channel_id").trimmed();
    QString guildId =
        DiscordJsonParser::extractStringField(bytes, "guild_id").trimmed();
    QString messageId =
        DiscordJsonParser::extractStringField(bytes, "id").trimmed();
    bool shouldBuildFullMessage =
        !channelId.isEmpty() && (channelId == m_selectedChannelId ||
                                 m_loadedChannelIds.contains(channelId));

    if (shouldBuildFullMessage) {
      qDebug() << "[discord-gateway] selected MESSAGE_CREATE"
               << "seq" << sequence << "guild" << guildId << "channel"
               << channelId << "message" << messageId;
    }

    if (!shouldBuildFullMessage) {
      QVariantMap lightPayload =
          buildLightMessageCreatePayload(bytes, m_currentUserId);
      bool shouldForwardLightEvent =
          guildId.isEmpty() ||
          lightPayload.value("mention_everyone").toBool() ||
          lightPayload.contains("mention_count");
      if (shouldForwardLightEvent) {
        emit dispatchReceived("MESSAGE_CREATE", lightPayload);
      }
      return;
    }

    QByteArray dataBytes = DiscordJsonParser::extractObjectField(bytes, "d");
    QString errorMessage;
    QVariantMap messagePayload =
        DiscordJsonParser::parseObject(dataBytes, &errorMessage);
    if (!errorMessage.isEmpty()) {
      emit error(QString("Gateway MESSAGE_CREATE JSON parse error: %1")
                     .arg(errorMessage));
      return;
    }

    handleDispatch("MESSAGE_CREATE", messagePayload, sequence);
    return;
  }

  if (fastEventName == "MESSAGE_UPDATE" || fastEventName == "MESSAGE_DELETE") {
    int sequence = extractSequence(bytes);
    if (sequence >= 0) {
      m_sequence = sequence;
    }

    QString channelId =
        DiscordJsonParser::extractStringField(bytes, "channel_id").trimmed();
    QString guildId =
        DiscordJsonParser::extractStringField(bytes, "guild_id").trimmed();
    QString messageId =
        DiscordJsonParser::extractStringField(bytes, "id").trimmed();
    bool shouldParseMessageEvent =
        !channelId.isEmpty() && (channelId == m_selectedChannelId ||
                                 m_loadedChannelIds.contains(channelId));
    if (shouldParseMessageEvent) {
      qDebug() << "[discord-gateway] selected" << fastEventName << "seq"
               << sequence << "guild" << guildId << "channel" << channelId
               << "message" << messageId;
    }
    if (!shouldParseMessageEvent) {
      return;
    }

    QByteArray dataBytes = DiscordJsonParser::extractObjectField(bytes, "d");
    QString errorMessage;
    QVariantMap messagePayload =
        DiscordJsonParser::parseObject(dataBytes, &errorMessage);
    if (!errorMessage.isEmpty()) {
      emit error(QString("Gateway %1 JSON parse error: %2")
                     .arg(fastEventName)
                     .arg(errorMessage));
      return;
    }

    handleDispatch(fastEventName, messagePayload, sequence);
    return;
  }

  if (fastEventName == "READY") {
    int sequence = extractSequence(bytes);
    if (sequence >= 0) {
      m_sequence = sequence;
    }

    QVariantMap readyPayload = buildLightReadyPayload(bytes);
    qDebug() << "[discord-gateway] READY fast-path"
             << "seq" << sequence << "payloadBytes" << bytes.size();
    handleDispatch("READY", readyPayload, sequence);
    return;
  }

  DiscordJsonParser::GatewayPayload payload =
      DiscordJsonParser::parseGatewayPayload(bytes);
  if (!payload.valid) {
    emit error(
        QString("Gateway JSON parse error: %1").arg(payload.errorMessage));
    return;
  }
  if (payload.sequence >= 0) {
    m_sequence = payload.sequence;
  }

  switch (payload.op) {
  case 0:
    // qDebug() << "[discord-gateway] parsed dispatch" << payload.eventName
    //          << "seq" << payload.sequence;
    if (payload.eventName == "READY") {
      qDebug() << "[discord-gateway] READY presences"
               << payload.data.value("presences").toList().size()
               << "payloadBytes" << bytes.size();
    }
    handleDispatch(payload.eventName, payload.data, payload.sequence);
    break;
  case 10:
    handleHello(payload.data);
    break;
  case 11:
    break;
  case 7:
    emit error("Discord gateway requested reconnect");
    break;
  case 9:
    emit error("Discord gateway invalid session");
    break;
  default:
    qDebug() << "[discord] unhandled gateway opcode" << payload.op;
    break;
  }
}

void DiscordGateway::handleHello(const QVariantMap &data) {
  int interval =
      DiscordJsonParser::valueToInt(data.value("heartbeat_interval"), 0);
  if (interval <= 0) {
    emit error("Discord gateway HELLO did not include heartbeat interval");
    return;
  }

  m_heartbeatIntervalMs = interval;
  m_nextHeartbeatMs = mg_millis() + m_heartbeatIntervalMs;
  sendHeartbeat();
  sendIdentify();
}

void DiscordGateway::handleDispatch(const QString &eventName,
                                    const QVariantMap &data, int sequence) {
  Q_UNUSED(sequence);

  if (eventName == "READY") {
    m_sessionId = data.value("session_id").toString();
    m_resumeGatewayUrl = data.value("resume_gateway_url").toString();
    setState(Ready);
    emit ready(m_sessionId);
    flushPendingLazyRequests();

    // Nếu sheet Members đang mở lúc gateway bị đóng/reconnect (xem
    // m_activeMemberListGuildId trong Gateway.hpp), Discord không nhớ
    // subscription cũ sau khi socket đứt - phải gửi lại SYNC thủ công,
    // nếu không sheet sẽ trống vĩnh viễn cho tới khi user tự đóng/mở lại.
    if (!m_activeMemberListGuildId.isEmpty()) {
      qDebug() << "[discord-gateway] re-sending member-list sync after "
                  "reconnect"
               << "guild" << m_activeMemberListGuildId << "channel"
               << m_activeMemberListChannelId;
      sendMemberListSync(m_activeMemberListGuildId,
                         m_activeMemberListChannelId);
    }
  }

  emit dispatchReceived(eventName, data);
}
