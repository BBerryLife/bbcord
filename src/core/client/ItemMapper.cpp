#include "ItemMapper.hpp"
#include "../discord/DiscordUtils.hpp"
#include "../models/Models.hpp"

namespace {

QString userDisplayName(const QVariantMap &user) {
  QString name = user.value("global_name").toString();
  if (name.isEmpty()) {
    name = user.value("username").toString();
  }
  if (name.isEmpty()) {
    name = user.value("id").toString();
  }
  return name;
}
QString presenceColor(const QString &status) {
  if (status == "online") {
    return "#23A55A";
  }
  if (status == "idle") {
    return "#F0B232";
  }
  if (status == "dnd" || status == "busy") {
    return "#F23F43";
  }
  return "#80848E";
}
int presenceRank(const QString &status) {
  if (status == "online") {
    return 3;
  }
  if (status == "dnd" || status == "busy") {
    return 2;
  }
  if (status == "idle") {
    return 1;
  }
  return 0;
}
} // namespace
ItemMapper::ItemMapper(QObject *parent) : QObject(parent) {}
ItemMapper::~ItemMapper() {}
QVariantMap ItemMapper::guildToItem(const QVariantMap &guild) const {

  QVariantMap item;
  QString name = guild.value("name").toString();
  QString guildId = guild.value("id").toString();
  QString iconHash = guild.value("icon").toString();
  item["type"] = "server";
  item["id"] = guildId;
  item["name"] = name;
  item["mentionCount"] = guild.value("mention_count").toInt();
  item["unread"] = guild.value("unread").toBool();
  if (guild.contains("position")) {
    item["position"] = guild.value("position").toInt();
  }
  item["initials"] = DiscordUtils::firstTwoWordLetters(name);
  item["iconHash"] = iconHash;
  item["icon"] = QString();
  return item;
}
QVariantMap
ItemMapper::dmChannelToItem(const QVariantMap &channel,
                            const QVariantMap &avatarSourcesByUserId,
                            const QVariantMap &dmPresenceByUserId) const {

  QVariantMap item;
  QVariantList recipients = channel.value("recipients").toList();
  QVariantMap recipient =
      recipients.isEmpty() ? QVariantMap() : recipients.first().toMap();
  QVariantMap recipient2 =
      recipients.size() > 1 ? recipients.at(1).toMap() : QVariantMap();
  int channelType = channel.value("type").toInt();
  bool groupDm = channelType == DiscordChannel::GroupDm;
  QString name = channel.value("name").toString();
  if (name.isEmpty() && groupDm) {
    QStringList names;
    for (int i = 0; i < recipients.size() && names.size() < 3; ++i) {
      QString recipientName = userDisplayName(recipients.at(i).toMap());
      if (!recipientName.isEmpty()) {
        names.append(recipientName);
      }
    }
    name = names.join(", ");
  }
  if (name.isEmpty()) {
    name = userDisplayName(recipient);
  }
  item["type"] = "dm";
  item["id"] = channel.value("id").toString();
  item["name"] = name;
  item["isGroup"] = groupDm;
  item["lastMessageId"] = channel.value("last_message_id").toString();
  item["initials"] = DiscordUtils::firstLetter(name);
  item["initials2"] = DiscordUtils::firstLetter(userDisplayName(recipient2));
  item["avatarColor"] = "#5865F2";
  item["avatarColor2"] = "#3F4147";
  item["avatarUserId"] = recipient.value("id").toString();
  item["avatarHash"] = recipient.value("avatar").toString();
  item["avatar"] =
      avatarSourcesByUserId.value(item.value("avatarUserId").toString())
          .toString();
  item["avatarUserId2"] = recipient2.value("id").toString();
  item["avatarHash2"] = recipient2.value("avatar").toString();
  item["avatar2"] =
      avatarSourcesByUserId.value(item.value("avatarUserId2").toString())
          .toString();
  QVariantList recipientIds;
  QVariantList cachedRecipients;
  for (int i = 0; i < recipients.size(); ++i) {
    QVariantMap rawRecipient = recipients.at(i).toMap();
    QString recipientId = rawRecipient.value("id").toString();
    if (!recipientId.isEmpty()) {
      recipientIds.append(recipientId);
      QVariantMap cachedRecipient;
      cachedRecipient["id"] = recipientId;
      cachedRecipient["username"] = rawRecipient.value("username").toString();
      cachedRecipient["global_name"] =
          rawRecipient.value("global_name").toString();
      cachedRecipient["discriminator"] =
          rawRecipient.value("discriminator").toString();
      cachedRecipient["avatar"] = rawRecipient.value("avatar").toString();
      cachedRecipients.append(cachedRecipient);
    }
  }
  item["recipientIds"] = recipientIds;
  item["recipients"] = cachedRecipients;
  QString status = dmStatusForRecipients(recipientIds, dmPresenceByUserId);
  item["status"] = status;
  item["statusColor"] = presenceColor(status);
  return item;
}
QString
ItemMapper::dmStatusForRecipients(const QVariantList &userIds,
                                  const QVariantMap &dmPresenceByUserId) const {

  QString bestStatus = "offline";
  int bestRank = 0;
  for (int i = 0; i < userIds.size(); ++i) {
    QString userId = userIds.at(i).toString().trimmed();
    QString status = dmPresenceByUserId.value(userId).toString();
    int rank = presenceRank(status);
    if (rank > bestRank) {
      bestRank = rank;
      bestStatus = status;
    }
  }
  return bestStatus;
}
QVariantMap ItemMapper::guildChannelToItem(const QVariantMap &channel) const {

  QVariantMap item;
  int type = channel.value("type").toInt();
  bool isThread = type == DiscordChannel::AnnouncementThread ||
                  type == DiscordChannel::PublicThread ||
                  type == DiscordChannel::PrivateThread;
  item["id"] = channel.value("id").toString();
  item["name"] = channel.value("name").toString();
  item["type"] = type == DiscordChannel::GuildCategory ? "category" : "channel";
  item["icon"] = "asset:///images/icons/hash.png";
  if (type == DiscordChannel::GuildVoice) {
    item["icon"] = "asset:///images/icons/speaker.png";
  } else if (type == DiscordChannel::GuildForum ||
             type == DiscordChannel::GuildMedia) {
    item["icon"] = "asset:///images/icons/ic_chat_multiperson.png";
  } else if (isThread) {
    // No dedicated thread icon yet in assets/images/icons — reuse the
    // "multiperson" icon from forum/media instead of the default
    // hash.png, so threads stand out from regular text channels.
    // TODO: add a proper thread icon (branch/thread style) if available.
    item["icon"] = "asset:///images/icons/ic_chat_multiperson.png";
  }
  // Threads SHARE ChatController with regular text channels (see
  // DiscordClient::selectChannel() — no special-casing by channelType).
  // GuildForum/GuildMedia have no messages of their own to open via
  // ChatCard (each forum "post" IS a thread, parentId points back here) -
  // used to be "implemented: false" to block this outright, now true
  // since ServerList.qml knows how to open the right UI (ThreadList.qml,
  // showing that channel's posts/threads) instead of trying ChatCard.
  item["implemented"] = true;
  item["isForumLike"] = type == DiscordChannel::GuildForum ||
                        type == DiscordChannel::GuildMedia;
  item["isThread"] = isThread;
  item["position"] = channel.value("position").toInt();
  item["channelType"] = type;
  item["parentId"] = channel.value("parent_id").toString();
  item["unread"] = channel.value("unread").toBool();
  item["mentionCount"] = channel.value("mention_count").toInt();
  item["accessible"] =
      !channel.contains("accessible") || channel.value("accessible").toBool();
  return item;
}
