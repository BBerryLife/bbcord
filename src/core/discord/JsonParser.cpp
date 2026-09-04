#include "JsonParser.hpp"

#include <bb/data/JsonDataAccess>

DiscordJsonParser::GatewayPayload
DiscordJsonParser::parseGatewayPayload(const QByteArray &bytes) {
  GatewayPayload payload;

  bb::data::JsonDataAccess json;
  QVariant parsed = json.loadFromBuffer(bytes);

  if (json.hasError()) {
    payload.errorMessage = json.error().errorMessage();
    return payload;
  }

  QVariantMap root = parsed.toMap();
  payload.op = valueToInt(root.value("op"), -1);
  payload.sequence = valueToInt(root.value("s"), -1);
  payload.eventName = root.value("t").toString();
  payload.data = root.value("d").toMap();
  payload.valid = true;

  return payload;
}

QVariantMap DiscordJsonParser::parseObject(const QByteArray &bytes,
                                           QString *errorMessage) {
  bb::data::JsonDataAccess json;
  QVariant parsed = json.loadFromBuffer(bytes);

  if (json.hasError()) {
    if (errorMessage != 0) {
      *errorMessage = json.error().errorMessage();
    }
    return QVariantMap();
  }

  if (errorMessage != 0) {
    errorMessage->clear();
  }
  return parsed.toMap();
}

QVariantList DiscordJsonParser::parseArray(const QByteArray &bytes,
                                           QString *errorMessage) {
  bb::data::JsonDataAccess json;
  QVariant parsed = json.loadFromBuffer(bytes);

  if (json.hasError()) {
    if (errorMessage != 0) {
      *errorMessage = json.error().errorMessage();
    }
    return QVariantList();
  }

  if (errorMessage != 0) {
    errorMessage->clear();
  }
  return parsed.toList();
}

QByteArray DiscordJsonParser::buildIdentifyPayload(const QString &token,
                                                   QString *errorMessage) {
  QVariantMap properties;
  properties["os"] = "Windows";
  properties["browser"] = "Discord Client";
  properties["device"] = "";
  properties["system_locale"] = "en-US";
  properties["browser_user_agent"] =
      "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, "
      "like Gecko) Chrome/149.0.0.0 Safari/537.36";
  properties["browser_version"] = "149.0.0.0";
  properties["os_version"] = "10";
  properties["referrer"] = "";
  properties["referring_domain"] = "";
  properties["referrer_current"] = "";
  properties["referring_domain_current"] = "";
  properties["release_channel"] = "stable";
  properties["client_build_number"] = 375000;
  properties["client_event_source"] = QVariant();

  QVariantMap data;
  data["token"] = token;
  data["capabilities"] = 30717;
  data["properties"] = properties;
  data["large_threshold"] = 250;
  data["guild_subscriptions"] = false;
  QVariantMap presence;
  presence["status"] = "online";
  presence["since"] = QVariant();
  presence["activities"] = QVariantList();
  presence["afk"] = false;
  data["presence"] = presence;

  QVariantMap clientState;
  clientState["guild_versions"] = QVariantMap();
  clientState["highest_last_message_id"] = "0";
  clientState["read_state_version"] = 0;
  clientState["user_guild_settings_version"] = -1;
  clientState["user_settings_version"] = -1;
  clientState["private_channels_version"] = "0";
  clientState["api_code_version"] = 0;
  data["client_state"] = clientState;

  QVariantMap root;
  root["op"] = 2;
  root["d"] = data;

  bb::data::JsonDataAccess json;
  QByteArray payload;
  json.saveToBuffer(root, &payload);

  if (json.hasError()) {
    if (errorMessage != 0) {
      *errorMessage = json.error().errorMessage();
    }
    return QByteArray();
  }

  if (errorMessage != 0) {
    errorMessage->clear();
  }
  return payload;
}

QByteArray DiscordJsonParser::buildGuildSubscribePayload(
    const QString &guildId, const QString &channelId, QString *errorMessage) {
  QVariantMap data;
  QString safeGuildId = guildId.trimmed();
  QString safeChannelId = channelId.trimmed();
  data["guild_id"] = safeGuildId;
  data["typing"] = true;
  data["activities"] = true;
  data["threads"] = true;

  if (!safeChannelId.isEmpty()) {
    QVariantList range;
    range.append(0);
    range.append(99);

    QVariantList ranges;
    ranges.append(range);

    QVariantMap channels;
    channels[safeChannelId] = ranges;
    data["channels"] = channels;
  }

  QVariantMap guildSubscriptions;
  guildSubscriptions[safeGuildId] = QVariantList();
  data["guild_subscriptions"] = guildSubscriptions;

  QVariantMap root;
  root["op"] = 14;
  root["d"] = data;

  bb::data::JsonDataAccess json;
  QByteArray payload;
  json.saveToBuffer(root, &payload);

  if (json.hasError()) {
    if (errorMessage != 0) {
      *errorMessage = json.error().errorMessage();
    }
    return QByteArray();
  }

  if (errorMessage != 0) {
    errorMessage->clear();
  }
  return payload;
}

QByteArray DiscordJsonParser::buildMemberListSyncPayload(
    const QString &guildId, const QString &channelId,
    QString *errorMessage) {
  QVariantMap data;
  QString safeGuildId = guildId.trimmed();
  QString safeChannelId = channelId.trimmed();
  data["guild_id"] = safeGuildId;
  data["typing"] = true;
  data["activities"] = true;
  data["threads"] = true;

  if (!safeChannelId.isEmpty()) {
    QVariantList range;
    range.append(0);
    range.append(99);

    QVariantList ranges;
    ranges.append(range);

    QVariantMap channels;
    channels[safeChannelId] = ranges;
    data["channels"] = channels;
  }

  // KHÔNG có "guild_subscriptions" — khác buildGuildSubscribePayload()
  // (dành cho lazy-load message). Xem giải thích trong JsonParser.hpp.

  QVariantMap root2;
  root2["op"] = 14;
  root2["d"] = data;

  bb::data::JsonDataAccess json2;
  QByteArray payload2;
  json2.saveToBuffer(root2, &payload2);

  if (json2.hasError()) {
    if (errorMessage != 0) {
      *errorMessage = json2.error().errorMessage();
    }
    return QByteArray();
  }

  if (errorMessage != 0) {
    errorMessage->clear();
  }
  return payload2;
}

int DiscordJsonParser::valueToInt(const QVariant &value, int fallback) {
  bool ok = false;
  int result = value.toInt(&ok);
  return ok ? result : fallback;
}

bool DiscordJsonParser::hasJsonToken(const QByteArray &bytes,
                                     const char *compactToken,
                                     const char *spacedToken) {
  return bytes.indexOf(compactToken) >= 0 || bytes.indexOf(spacedToken) >= 0;
}

bool DiscordJsonParser::isLargeReadyPayload(const QByteArray &bytes) {
  return bytes.size() > 262144 &&
         hasJsonToken(bytes, "\"op\":0", "\"op\": 0") &&
         hasJsonToken(bytes, "\"t\":\"READY\"", "\"t\": \"READY\"");
}

QString DiscordJsonParser::extractStringField(const QByteArray &bytes,
                                              const char *fieldName) {
  QByteArray marker = QByteArray("\"") + fieldName + QByteArray("\"");
  int pos = bytes.indexOf(marker);
  if (pos < 0) {
    return QString();
  }

  pos = bytes.indexOf(':', pos + marker.size());
  if (pos < 0) {
    return QString();
  }

  ++pos;
  while (pos < bytes.size() &&
         (bytes.at(pos) == ' ' || bytes.at(pos) == '\t' ||
          bytes.at(pos) == '\r' || bytes.at(pos) == '\n')) {
    ++pos;
  }

  if (pos >= bytes.size() || bytes.at(pos) != '"') {
    return QString();
  }

  ++pos;
  QByteArray value;
  bool escaped = false;
  for (; pos < bytes.size(); ++pos) {
    char ch = bytes.at(pos);
    if (escaped) {
      value.append(ch);
      escaped = false;
    } else if (ch == '\\') {
      escaped = true;
    } else if (ch == '"') {
      break;
    } else {
      value.append(ch);
    }
  }

  return QString::fromUtf8(value.constData(), value.size());
}

bool DiscordJsonParser::extractBoolField(const QByteArray &bytes,
                                         const char *fieldName, bool fallback) {
  QByteArray marker = QByteArray("\"") + fieldName + QByteArray("\"");
  int pos = bytes.indexOf(marker);
  if (pos < 0) {
    return fallback;
  }

  pos = bytes.indexOf(':', pos + marker.size());
  if (pos < 0) {
    return fallback;
  }

  ++pos;
  while (pos < bytes.size() &&
         (bytes.at(pos) == ' ' || bytes.at(pos) == '\t' ||
          bytes.at(pos) == '\r' || bytes.at(pos) == '\n')) {
    ++pos;
  }

  if (bytes.mid(pos, 4) == "true") {
    return true;
  }
  if (bytes.mid(pos, 5) == "false") {
    return false;
  }
  return fallback;
}

QByteArray DiscordJsonParser::extractObjectField(const QByteArray &bytes,
                                                 const char *fieldName) {
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

  if (pos >= bytes.size() || bytes.at(pos) != '{') {
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
    } else if (ch == '{') {
      ++depth;
    } else if (ch == '}') {
      --depth;
      if (depth == 0) {
        return bytes.mid(start, pos - start + 1);
      }
    }
  }

  return QByteArray();
}

QVariantList DiscordJsonParser::extractArrayField(const QByteArray &bytes,
                                                  const char *fieldName) {
  QByteArray marker = QByteArray("\"") + fieldName + QByteArray("\"");
  int pos = bytes.indexOf(marker);
  if (pos < 0) {
    return QVariantList();
  }

  pos = bytes.indexOf(':', pos + marker.size());
  if (pos < 0) {
    return QVariantList();
  }

  ++pos;
  while (pos < bytes.size() &&
         (bytes.at(pos) == ' ' || bytes.at(pos) == '\t' ||
          bytes.at(pos) == '\r' || bytes.at(pos) == '\n')) {
    ++pos;
  }

  if (pos >= bytes.size() || bytes.at(pos) != '[') {
    return QVariantList();
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
        QByteArray arrayBytes = bytes.mid(start, pos - start + 1);
        return parseArray(arrayBytes);
      }
    }
  }

  return QVariantList();
}
