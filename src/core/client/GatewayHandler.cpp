#include "GatewayHandler.hpp"
#include "../AppStore.hpp"
#include "../Client.hpp"
#include "../discord/DiscordUtils.hpp"
#include "../models/Models.hpp"

#include <QDateTime>
#include <QDebug>
#include <QMetaObject>
#include <QTimer>

namespace {
const int kMessageQueueFlushThreshold = 10;

QString authorDisplayNameFromPayload(const QVariantMap &payload) {
  QVariantMap author = payload.value("author").toMap();
  QString name = author.value("global_name").toString();
  if (name.isEmpty()) {
    name = author.value("username").toString();
  }
  if (name.isEmpty()) {
    name = author.value("id").toString();
  }
  return name;
}

// mention_roles: array of role IDs (string) @-mentioned in content —
// Discord resolves this field itself based on message content, no need
// for the app to parse text. Returns true if any role in mention_roles
// matches one of our current roles in that guild.
bool payloadMentionsRoleOf(const QVariantMap &payload,
                          const QStringList &myRoleIds) {
  if (myRoleIds.isEmpty()) {
    return false;
  }
  QVariantList mentionRoles = payload.value("mention_roles").toList();
  for (int i = 0; i < mentionRoles.size(); ++i) {
    if (myRoleIds.contains(mentionRoles.at(i).toString())) {
      return true;
    }
  }
  return false;
}

QString guildNameById(AppStore *store, const QString &guildId) {
  if (store == 0 || guildId.isEmpty()) {
    return QString();
  }
  QVariantList guilds = store->guilds();
  for (int i = 0; i < guilds.size(); ++i) {
    QVariantMap guild = guilds.at(i).toMap();
    if (guild.value("id").toString() == guildId) {
      return guild.value("name").toString();
    }
  }
  return QString();
}

// Returns true and fills dmName/isGroup if channelId is found in the
// cached DM channel list in AppStore (the "name"/"isGroup" fields are
// pre-built by ItemMapper::dmChannelToItem — see ItemMapper.cpp).
bool findDmChannel(AppStore *store, const QString &channelId, QString *dmName,
                   bool *isGroup) {
  if (store == 0 || channelId.isEmpty()) {
    return false;
  }
  QVariantList dmChannels = store->dmChannels();
  for (int i = 0; i < dmChannels.size(); ++i) {
    QVariantMap channel = dmChannels.at(i).toMap();
    if (channel.value("id").toString() == channelId) {
      if (dmName != 0) {
        *dmName = channel.value("name").toString();
      }
      if (isGroup != 0) {
        *isGroup = channel.value("isGroup").toBool();
      }
      return true;
    }
  }
  return false;
}

qint64 messageTimestampMsFromPayload(const QVariantMap &payload) {
  DiscordMessage message = DiscordMessage::fromVariantMap(payload);
  qint64 ms = message.timestampMs();
  return ms > 0 ? ms : static_cast<qint64>(QDateTime::currentMSecsSinceEpoch());
}

// Empty "content" isn't automatically a bug — Discord allows sending a
// message that's only an attachment/embed/sticker with no text at all
// (a photo, a file, a pasted link that auto-embeds, a sticker...). The
// old code just concatenated the empty content straight into the preview,
// producing a dangling "Replied: " with nothing after the colon — that
// was the original bug report. This falls back to a short, natural-language
// label when content is empty, so the preview is never blank.
QString previewContentFromPayload(const QString &content,
                                  const QVariantMap &payload) {
  if (!content.trimmed().isEmpty()) {
    return content;
  }

  QVariantList attachments = payload.value("attachments").toList();
  if (!attachments.isEmpty()) {
    QString contentType =
        attachments.first().toMap().value("content_type").toString();
    // Discord's own "voice message" attachments are flagged via the
    // message's `flags` bit 1<<13 (IS_VOICE_MESSAGE) rather than a
    // distinct content_type, so check that first.
    int flags = payload.value("flags").toInt();
    if (flags & (1 << 13)) {
      return QLatin1String("Voice message");
    }
    if (contentType.startsWith(QLatin1String("image/"))) {
      return QLatin1String("Photo");
    }
    if (contentType.startsWith(QLatin1String("video/"))) {
      return QLatin1String("Video");
    }
    if (contentType.startsWith(QLatin1String("audio/"))) {
      return QLatin1String("Audio");
    }
    return QLatin1String("Attachment");
  }

  QVariantList stickers = payload.value("sticker_items").toList();
  if (!stickers.isEmpty()) {
    return QLatin1String("Sticker");
  }

  QVariantList embeds = payload.value("embeds").toList();
  if (!embeds.isEmpty()) {
    return QLatin1String("Link");
  }

  if (payload.contains(QLatin1String("poll"))) {
    return QLatin1String("Poll");
  }

  // No content, no attachment/sticker/embed/poll — none of the known
  // "text-less message" shapes matched. Log which top-level keys the
  // payload actually has (never the values — message text/media are
  // user data and shouldn't hit the log) so a repeat of this can be
  // diagnosed without needing to capture private message content.
  qDebug() << "[Hub] previewContentFromPayload: empty content with no "
              "matching fallback, payload keys="
           << payload.keys();
  return QLatin1String("New message");
}
} // namespace

GatewayHandler::GatewayHandler(DiscordClient *client, AppStore *store,
                               QObject *parent)
    : QObject(parent), m_client(client), m_store(store), m_batchTimer(0) {
  m_batchTimer = new QTimer(this);
  m_batchTimer->setSingleShot(true);
  m_batchTimer->setInterval(100);
  connect(m_batchTimer, SIGNAL(timeout()), this, SLOT(flushMessageQueue()));
}
GatewayHandler::~GatewayHandler() { flushMessageQueue(); }
void GatewayHandler::applyGatewayOrderingEvent(
    const QString &eventName, const QVariantMap &payload,
    QStringList &pendingUnreadGuildIds,
    QVariantMap &pendingMentionCountsByGuildId,
    QVariantMap &pendingMentionCountsByChannelId,
    QStringList &pendingUnreadChannelIds, bool &pendingDmUiUpdate,
    bool &gatewayUiUpdateQueued) {
  if (eventName == "MESSAGE_CREATE") {
    QString guildId = payload.value("guild_id").toString();
    QString channelId = payload.value("channel_id").toString();
    if (shouldApplyChatEvent(channelId)) {
      DiscordMessage message = DiscordMessage::fromVariantMap(payload);
      message.pending = false;
      message.failed = false;
      m_messageQueue.append(message);
      if (m_messageQueue.size() >= kMessageQueueFlushThreshold) {
        flushMessageQueue();
      } else if (!m_batchTimer->isActive()) {
        m_batchTimer->start();
      }
    }
    if (guildId.isEmpty() && !channelId.isEmpty()) {
      QMetaObject::invokeMethod(m_client, "moveDmToTop", Qt::DirectConnection,
                                Q_ARG(QString, channelId),
                                Q_ARG(QString, payload.value("id").toString()));
      pendingDmUiUpdate = true;
    } else if (!guildId.isEmpty() &&
               payload.value("author").toMap().value("id").toString() !=
                   (m_store ? m_store->currentUserId() : QString())) {
      // Fix: confirmed as a real bug - a message arriving in the
      // channel/guild the user is CURRENTLY looking at was still
      // unconditionally marked unread here, same as a message in any
      // other channel. selectChannel() only clears that mark when
      // it's actually called (i.e. when the user switches INTO a
      // channel) - it never re-runs just because a new message
      // arrived while already sitting in that channel, so the
      // unread/mention badges would light up and then never turn
      // off until the user left and came back. Skip marking anything
      // unread for the channel currently open (m_store's
      // selectedChannelId) - the user is looking right at the
      // message, there is nothing to catch up on.
      bool isCurrentlyOpenChannel =
          m_store != 0 && !channelId.isEmpty() &&
          m_store->selectedChannelId() == channelId;

      // Fix: temporary diagnostic logging - needed to pin down a
      // reported bug where a non-mention message's guild badge only
      // seems to "take" after the FIRST mention of a session has been
      // read/cleared, never before. Without this, there was no way to
      // tell from a log whether a given non-mention MESSAGE_CREATE
      // even reached this branch, or what isCurrentlyOpenChannel
      // evaluated to for it.
      qDebug() << "[discord-chat] non-own MESSAGE_CREATE guild" << guildId
               << "channel" << channelId << "selectedChannelId"
               << (m_store ? m_store->selectedChannelId() : QString("<no store>"))
               << "isCurrentlyOpenChannel" << isCurrentlyOpenChannel;

      if (!isCurrentlyOpenChannel) {
        if (!pendingUnreadGuildIds.contains(guildId)) {
          pendingUnreadGuildIds.append(guildId);
        }
      }
      bool mentionsCurrentUser = gatewayMessageMentionsCurrentUser(payload);
      if (!isCurrentlyOpenChannel) {
        if (payload.contains("mention_count")) {
          pendingMentionCountsByGuildId.insert(
              guildId, payload.value("mention_count").toInt());
        } else if (mentionsCurrentUser) {
          int mentionCount = 0;
          if (pendingMentionCountsByGuildId.contains(guildId)) {
            mentionCount = pendingMentionCountsByGuildId.value(guildId).toInt();
          } else {
            QMetaObject::invokeMethod(
                m_client, "guildMentionCount", Qt::DirectConnection,
                Q_RETURN_ARG(int, mentionCount), Q_ARG(QString, guildId));
          }
          pendingMentionCountsByGuildId.insert(guildId, mentionCount + 1);
        }
        if (mentionsCurrentUser && !channelId.isEmpty()) {
          int channelMentionCount =
              pendingMentionCountsByChannelId.value(channelId).toInt();
          pendingMentionCountsByChannelId.insert(channelId,
                                                 channelMentionCount + 1);
        }
        if (!channelId.isEmpty() &&
            !pendingUnreadChannelIds.contains(channelId)) {
          pendingUnreadChannelIds.append(channelId);
        }
      }
    }
    if (!gatewayUiUpdateQueued) {
      gatewayUiUpdateQueued = true;
      QTimer::singleShot(250, m_client, SLOT(flushGatewayUiUpdates()));
    }
    return;
  }
  if (eventName == "MESSAGE_UPDATE") {
    QString channelId = payload.value("channel_id").toString();
    if (shouldApplyChatEvent(channelId)) {
      DiscordMessage message = DiscordMessage::fromVariantMap(payload);
      message.pending = false;
      message.failed = false;
      m_store->updateChatMessage(message);
    }
    return;
  }
  if (eventName == "MESSAGE_DELETE") {
    QString channelId = payload.value("channel_id").toString();
    QString messageId = payload.value("id").toString();
    if (shouldApplyChatEvent(channelId)) {
      m_store->deleteChatMessage(channelId, messageId);
    }
    return;
  }
  if (eventName == "PRESENCE_UPDATE") {
    QMetaObject::invokeMethod(
        m_client, "updateDmPresence", Qt::DirectConnection,
        Q_ARG(QString, payload.value("user").toMap().value("id").toString()),
        Q_ARG(QString, payload.value("status").toString()));
    if (!gatewayUiUpdateQueued) {
      gatewayUiUpdateQueued = true;
      QTimer::singleShot(250, m_client, SLOT(flushGatewayUiUpdates()));
    }
    return;
  }
}

void GatewayHandler::flushMessageQueue() {
  if (m_messageQueue.isEmpty()) {
    return;
  }

  if (m_store != 0) {
    m_store->addOrReplaceChatMessages(m_messageQueue);
  }
  m_messageQueue.clear();
  m_batchTimer->stop();
}

bool GatewayHandler::shouldApplyChatEvent(const QString &channelId) const {
  if (m_store == 0) {
    return false;
  }

  QString safeChannelId = channelId.trimmed();
  if (safeChannelId.isEmpty()) {
    return false;
  }

  return m_store->isChatInitialLoaded(safeChannelId) ||
         m_store->selectedChannelId() == safeChannelId;
}

bool GatewayHandler::gatewayMessageMentionsCurrentUser(
    const QVariantMap &payload) const {

  QString currentUserId = m_store ? m_store->currentUserId() : QString();

  if (currentUserId.isEmpty()) {

    return false;
  }
  if (payload.value("mention_everyone").toBool()) {
    return true;
  }
  QVariantList mentions = payload.value("mentions").toList();
  for (int i = 0; i < mentions.size(); ++i) {
    if (mentions.at(i).toMap().value("id").toString() == currentUserId) {
      return true;
    }
  }
  QStringList guildId = QStringList() << payload.value("guild_id").toString();
  if (m_store != 0 && !guildId.first().isEmpty()) {
    QStringList myRoleIds =
        m_store->currentUserRoleIdsForGuild(guildId.first());
    if (payloadMentionsRoleOf(payload, myRoleIds)) {
      return true;
    }
  }
  return false;
}

MentionNotification GatewayHandler::buildMentionNotification(
    const QVariantMap &payload) const {
  MentionNotification result;

  if (m_store == 0) {
    return result;
  }

  QString currentUserId = m_store->currentUserId();
  QString authorId = payload.value("author").toMap().value("id").toString();
  if (currentUserId.isEmpty() || authorId.isEmpty() ||
      authorId == currentUserId) {
    // Never notify for messages we sent ourselves (even if we
    // @mention or reply to ourselves).
    return result;
  }

  QString channelId = payload.value("channel_id").toString().trimmed();
  QString guildId = payload.value("guild_id").toString().trimmed();
  // Fix: resolve <@id>/<@&id>/<#id> tokens to "@name"/"@role"/"#name"
  // before building the notification preview - otherwise a ping shows
  // its raw numeric id in the Hub notification body (confirmed via a
  // real screenshot: "<@921017648869957654>" instead of a name).
  // mentionRoles isn't resolved to real names here either (same
  // limitation as DiscordMessage::resolveMentions() itself - Discord's
  // "mention_roles" is a bare id array with no name attached, and
  // resolving it needs the guild's role list) - falls back to the
  // same generic "@role" DiscordMessage::resolveMentions() uses.
  QString content = DiscordMessage::resolveMentions(
      payload.value("content").toString(), payload.value("mentions").toList(),
      payload.value("mention_roles").toList(),
      payload.value("mention_channels").toList());
  if (channelId.isEmpty()) {
    return result;
  }

  qint64 timestampMs = messageTimestampMsFromPayload(payload);

  if (!guildId.isEmpty()) {
    // ----- Guild: direct mention / @everyone / @here / role mention -----
    bool mentionsMe = gatewayMessageMentionsCurrentUser(payload);
    if (!mentionsMe) {
      return result;
    }

    QString serverName = guildNameById(m_store, guildId);
    if (serverName.isEmpty()) {
      serverName = QString("Server");
    }
    QString authorName = authorDisplayNameFromPayload(payload);

    result.shouldNotify = true;
    result.sourceId = channelId;
    result.title = serverName;
    result.preview =
        QString("%1: %2").arg(authorName, previewContentFromPayload(content, payload));
    result.timestampMs = timestampMs;
    return result;
  }

  // ----- DM / Group DM -----
  QString dmName;
  bool isGroup = false;
  bool foundDm = findDmChannel(m_store, channelId, &dmName, &isGroup);

  if (isGroup) {
    // Group DM: only worth notifying if the message is a reply to one
    // of our own messages. DiscordMessage::fromVariantMap() already
    // parses referenced_message -> replyAuthor/replyContent, but to
    // know if that reply targets US specifically (not just "is a
    // reply") we need author.id straight from referenced_message in
    // the raw payload, since replyAuthor only stores the display name
    // (not reliable for id comparison — two people can share a
    // display name in the same group).
    QVariantMap reference = payload.value("referenced_message").toMap();
    QString repliedToAuthorId =
        reference.value("author").toMap().value("id").toString();
    if (repliedToAuthorId != currentUserId) {
      return result;
    }

    QString authorName = authorDisplayNameFromPayload(payload);
    QString title = !dmName.isEmpty() ? dmName : authorName;

    result.shouldNotify = true;
    result.sourceId = channelId;
    result.title = title;
    result.preview =
        QString("Replied: %1").arg(previewContentFromPayload(content, payload));
    result.timestampMs = timestampMs;
    return result;
  }

  // 1-1 DM (or a channel not yet cached in dmChannels — still treated
  // as a 1-1 DM as a safe default, since guildId was confirmed empty
  // above so this can't be a guild channel): notify for every new message.
  Q_UNUSED(foundDm);
  QString authorName = authorDisplayNameFromPayload(payload);
  QString title = !dmName.isEmpty() ? dmName : authorName;

  result.shouldNotify = true;
  result.sourceId = channelId;
  result.title = title;
  result.preview =
      QString("Replied: %1").arg(previewContentFromPayload(content, payload));
  result.timestampMs = timestampMs;
  return result;
}
