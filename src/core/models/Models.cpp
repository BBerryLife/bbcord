#include "Models.hpp"

#include "../../utils/EmojiUtils.hpp"
#include "../../utils/MarkdownParser.hpp"

#include <QDateTime>
#include <QRegExp>
#include <QStringList>

namespace {
QDateTime parseDiscordTimestamp(const QString &timestamp) {
  QString value = timestamp.trimmed();
  if (value.isEmpty()) {
    return QDateTime();
  }

  bool utc = false;
  if (value.endsWith("Z")) {
    utc = true;
    value.chop(1);
  }

  int plusIndex = value.indexOf('+', 10);
  int minusIndex = value.indexOf('-', 10);
  int zoneIndex = plusIndex >= 0 ? plusIndex : minusIndex;
  QString zone;
  if (zoneIndex >= 0) {
    zone = value.mid(zoneIndex);
    value = value.left(zoneIndex);
  }

  int dotIndex = value.indexOf('.');
  QString base = dotIndex >= 0 ? value.left(dotIndex) : value;
  QString msecs;
  if (dotIndex >= 0) {
    msecs = value.mid(dotIndex + 1);
    while (msecs.length() > 3) {
      msecs.chop(1);
    }
    while (msecs.length() < 3) {
      msecs.append('0');
    }
  }

  QDateTime parsed = QDateTime::fromString(base, "yyyy-MM-dd'T'HH:mm:ss");
  if (!parsed.isValid()) {
    parsed = QDateTime::fromString(base, "yyyy-MM-dd'T'HH:mm");
  }
  if (!parsed.isValid()) {
    return QDateTime();
  }

  if (!msecs.isEmpty()) {
    QTime time = parsed.time();
    time = QTime(time.hour(), time.minute(), time.second(), msecs.toInt());
    parsed.setTime(time);
  }

  if (utc || zone == "+00:00" || zone == "+0000") {
    parsed.setTimeSpec(Qt::UTC);
  } else {
    parsed.setTimeSpec(Qt::LocalTime);
  }

  return parsed;
}
} // namespace

QString DiscordUser::displayName() const {
  if (!globalName.isEmpty()) {
    return globalName;
  }

  if (!username.isEmpty()) {
    return username;
  }

  return id;
}

bool DiscordAttachment::isImage() const {
  QString type = contentType.toLower();
  if (type.startsWith("image/")) {
    return true;
  }

  QString name = filename.toLower();
  return name.endsWith(".png") || name.endsWith(".jpg") ||
         name.endsWith(".jpeg") || name.endsWith(".gif") ||
         name.endsWith(".webp") || name.endsWith(".bmp");
}

QVariantMap DiscordAttachment::toVariantMap() const {
  QVariantMap data;
  data["id"] = id;
  data["filename"] = filename;
  data["url"] = url;
  data["proxyUrl"] = proxyUrl;
  data["contentType"] = contentType;
  data["size"] = size;
  data["width"] = width;
  data["height"] = height;
  data["isImage"] = isImage();
  return data;
}

DiscordAttachment DiscordAttachment::fromVariantMap(const QVariantMap &data) {
  DiscordAttachment attachment;
  attachment.id = data.value("id").toString();
  attachment.filename = data.value("filename").toString();
  attachment.url = data.value("url").toString();
  attachment.proxyUrl =
      data.value("proxy_url", data.value("proxyUrl")).toString();
  attachment.contentType =
      data.value("content_type", data.value("contentType")).toString();
  attachment.size = data.value("size").toInt();
  attachment.width = data.value("width").toInt();
  attachment.height = data.value("height").toInt();
  return attachment;
}

bool DiscordMessage::isEdited() const { return !editedTimestamp.isEmpty(); }

qint64 DiscordMessage::timestampMs() const {
  QDateTime parsed = parseDiscordTimestamp(timestamp);
  if (!parsed.isValid()) {
    return 0;
  }
  return parsed.toMSecsSinceEpoch();
}

QString DiscordMessage::displayTime() const {
  QDateTime parsed = parseDiscordTimestamp(timestamp);
  if (!parsed.isValid()) {
    return pending ? QString("Sending...") : QString();
  }

  return parsed.toLocalTime().toString("hh:mm");
}

QString DiscordMessage::authorInitials() const {
  QString name = author.displayName().trimmed();
  if (name.isEmpty()) {
    return QString();
  }

  return name.left(1).toUpper();
}

QString DiscordMessage::resolveMentions(const QString &rawContent,
                                        const QVariantList &mentions,
                                        const QVariantList &mentionRoles,
                                        const QVariantList &mentionChannels) {
  Q_UNUSED(mentionRoles);
  if (rawContent.isEmpty()) {
    return rawContent;
  }

  QString result = rawContent;

  // User mentions "<@id>" and "<@!id>" resolve the same way. displayName() prefers
  // server nickname/global name over the username, like the official client.
  for (int i = 0; i < mentions.size(); ++i) {
    QVariantMap mention = mentions.at(i).toMap();
    QString userId = mention.value("id").toString().trimmed();
    if (userId.isEmpty()) {
      continue;
    }
    DiscordUser user;
    user.id = userId;
    user.username = mention.value("username").toString();
    user.globalName =
        mention.value("global_name", mention.value("globalName")).toString();
    QString displayName = user.displayName();
    if (displayName.isEmpty()) {
      continue;
    }
    result.replace(QString("<@!%1>").arg(userId), "@" + displayName);
    result.replace(QString("<@%1>").arg(userId), "@" + displayName);
  }

  // Channel mentions "<#id>": mention_channels entries carry "name" directly (roles do not).
  for (int i = 0; i < mentionChannels.size(); ++i) {
    QVariantMap channel = mentionChannels.at(i).toMap();
    QString channelId = channel.value("id").toString().trimmed();
    QString channelName = channel.value("name").toString();
    if (channelId.isEmpty() || channelName.isEmpty()) {
      continue;
    }
    result.replace(QString("<#%1>").arg(channelId), "#" + channelName);
  }

  // Role mentions "<@&id>": mention_roles is a bare id array and role names need
  // AppStore (not available in this static method), so fall back to a generic
  // "@role". mentionRoles stays a parameter so callers with names can pass them.
  QRegExp roleMentionPattern("<@&(\\d+)>");
  int searchIndex = 0;
  while ((searchIndex = roleMentionPattern.indexIn(result, searchIndex)) !=
         -1) {
    result.replace(searchIndex, roleMentionPattern.matchedLength(), "@role");
    searchIndex += QString("@role").length();
  }

  return result;
}

QVariantMap DiscordMessage::toVariantMap() const {
  QVariantMap data;
  data["id"] = id;
  data["channelId"] = channelId;
  data["guildId"] = guildId;
  data["authorId"] = author.id;
  data["author"] = author.displayName();
  data["username"] = author.username;
  data["avatarHash"] = author.avatarHash;
  data["initials"] = authorInitials();
  data["nonce"] = nonce;
  data["message"] = content;
  // Resolve mentions before markdown/HTML conversion: toHtml() escapes and wraps
  // text, so resolving afterwards could mangle inserted tags. content stays raw
  // (needed for editing).
  QString resolvedContent =
      resolveMentions(content, mentions, mentionRoles, mentionChannels);
  data["messageHtml"] = MarkdownParser::toHtml(resolvedContent);
  // Label(TextFormat.Html) has no <img>, so custom emoji get a ":name:" text fallback
  // in messageHtml and are also exposed as ordered segments for MessageBubble.qml to
  // draw as ImageViews. Built from raw "content", since mention resolution never
  // touches emoji tokens.
  QVariantList emojiSegments;
  QList<EmojiUtils::EmojiToken> emojiTokens = EmojiUtils::findTokens(content);
  for (int i = 0; i < emojiTokens.size(); ++i) {
    const EmojiUtils::EmojiToken &token = emojiTokens.at(i);
    QVariantMap segment;
    segment["name"] = token.name;
    segment["id"] = token.id;
    segment["animated"] = token.animated;
    segment["url"] = EmojiUtils::cdnUrl(token.id, token.animated);
    emojiSegments.append(segment);
  }
  data["emojiSegments"] = emojiSegments;
  // Content that is only custom emoji (and whitespace) renders as jumbo emoji; see
  // EmojiUtils::isEmojiOnly() (capped at 27 like Discord).
  data["emojiOnly"] = EmojiUtils::isEmojiOnly(content);
  // Raw mention data for ChatController::prepareMessageForModel() to compute
  // "mentionsCurrentUser" (needs AppStore, unavailable in this const method).
  // mentionRoles is flattened back to a bare id list.
  QStringList mentionRoleIds;
  for (int i = 0; i < mentionRoles.size(); ++i) {
    QString roleId = mentionRoles.at(i).toMap().value("id").toString();
    if (!roleId.isEmpty()) {
      mentionRoleIds.append(roleId);
    }
  }
  data["mentions"] = mentions;
  data["mentionRoleIds"] = mentionRoleIds;
  data["mentionEveryone"] = mentionEveryone;
  data["content"] = content;
  data["timestamp"] = timestamp;
  data["timestampMs"] = timestampMs();
  data["time"] = displayTime();
  data["editedTimestamp"] = editedTimestamp;
  data["edited"] = isEdited();
  data["replyMessageId"] = replyMessageId;
  data["replyAuthor"] = replyAuthor;
  data["replyMessage"] = replyContent;
  QString resolvedReplyContent = resolveMentions(
      replyContent, replyMentions, replyMentionRoles, replyMentionChannels);
  data["replyMessageHtml"] = MarkdownParser::toHtml(resolvedReplyContent);
  data["pending"] = pending;
  data["failed"] = failed;
  data["isGroupStart"] = isGroupStart;
  data["isGroupEnd"] = isGroupEnd;
  data["showAvatar"] = showAvatar;
  data["showUsername"] = showUsername;
  data["showTimestamp"] = showTimestamp;

  QVariantList attachmentList;
  for (int i = 0; i < attachments.size(); ++i) {
    attachmentList.append(attachments.at(i).toVariantMap());
  }
  data["attachments"] = attachmentList;

  if (!attachments.isEmpty() && attachments.first().isImage()) {
    const DiscordAttachment &image = attachments.first();
    data["image"] = QString();
    data["imageWidth"] = image.width;
    data["imageHeight"] = image.height;
    data["attachmentUrl"] = image.url;
    data["attachmentName"] = image.filename;
    data["attachmentIsImage"] = true;
  } else {
    data["image"] = QString();
    data["imageWidth"] = 0;
    data["imageHeight"] = 0;
    if (!attachments.isEmpty()) {
      const DiscordAttachment &attachment = attachments.first();
      data["attachmentUrl"] = attachment.url;
      data["attachmentName"] = attachment.filename;
      data["attachmentIsImage"] = false;
    } else {
      data["attachmentUrl"] = QString();
      data["attachmentName"] = QString();
      data["attachmentIsImage"] = false;
    }
  }

  return data;
}

DiscordMessage DiscordMessage::fromVariantMap(const QVariantMap &data) {
  DiscordMessage message;
  message.id = data.value("id").toString();
  message.channelId =
      data.value("channel_id", data.value("channelId")).toString();
  message.guildId = data.value("guild_id", data.value("guildId")).toString();
  message.content = data.value("content", data.value("message")).toString();
  // Raw arrays used by resolveMentions(). "mention_roles" is a plain id array, so it
  // is normalized to {"id": ...} like the other two.
  message.mentions = data.value("mentions").toList();
  message.mentionChannels = data.value("mention_channels").toList();
  message.mentionEveryone =
      data.value("mention_everyone", data.value("mentionEveryone")).toBool();
  QVariantList rawMentionRoleIds = data.value("mention_roles").toList();
  QVariantList mentionRoles;
  for (int i = 0; i < rawMentionRoleIds.size(); ++i) {
    QVariantMap roleEntry;
    roleEntry["id"] = rawMentionRoleIds.at(i).toString();
    mentionRoles.append(roleEntry);
  }
  message.mentionRoles = mentionRoles;
  message.nonce = data.value("nonce").toString();
  message.timestamp = data.value("timestamp").toString();
  message.editedTimestamp =
      data.value("edited_timestamp", data.value("editedTimestamp")).toString();
  message.pending = data.value("pending").toBool();
  message.failed = data.value("failed").toBool();

  QVariantMap authorData = data.value("author").toMap();
  if (!authorData.isEmpty()) {
    message.author.id = authorData.value("id").toString();
    message.author.username = authorData.value("username").toString();
    message.author.globalName =
        authorData.value("global_name", authorData.value("globalName"))
            .toString();
    message.author.discriminator = authorData.value("discriminator").toString();
    message.author.avatarHash = authorData.value("avatar").toString();
    message.author.bot = authorData.value("bot").toBool();
  } else {
    message.author.id = data.value("authorId").toString();
    message.author.username =
        data.value("username", data.value("author")).toString();
    message.author.globalName = data.value("author").toString();
    message.author.avatarHash = data.value("avatarHash").toString();
  }

  QVariantMap reference = data.value("referenced_message").toMap();
  if (!reference.isEmpty()) {
    message.replyMessageId = reference.value("id").toString();
    QVariantMap refAuthor = reference.value("author").toMap();
    DiscordUser user;
    user.id = refAuthor.value("id").toString();
    user.username = refAuthor.value("username").toString();
    user.globalName =
        refAuthor.value("global_name", refAuthor.value("globalName"))
            .toString();
    message.replyAuthor = user.displayName();
    message.replyContent = reference.value("content").toString();
    message.replyMentions = reference.value("mentions").toList();
    message.replyMentionChannels = reference.value("mention_channels").toList();
    QVariantList rawReplyMentionRoleIds =
        reference.value("mention_roles").toList();
    QVariantList replyMentionRoles;
    for (int i = 0; i < rawReplyMentionRoleIds.size(); ++i) {
      QVariantMap roleEntry;
      roleEntry["id"] = rawReplyMentionRoleIds.at(i).toString();
      replyMentionRoles.append(roleEntry);
    }
    message.replyMentionRoles = replyMentionRoles;
  } else {
    QVariantMap messageReference = data.value("message_reference").toMap();
    message.replyMessageId =
        data.value("replyMessageId", messageReference.value("message_id"))
            .toString();
    message.replyAuthor = data.value("replyAuthor").toString();
    message.replyContent =
        data.value("replyMessage", data.value("replyContent")).toString();
  }

  QVariantList attachmentList = data.value("attachments").toList();
  for (int i = 0; i < attachmentList.size(); ++i) {
    message.attachments.append(
        DiscordAttachment::fromVariantMap(attachmentList.at(i).toMap()));
  }

  return message;
}
