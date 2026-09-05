#include "Chat.hpp"

#include "../AppStore.hpp"
#include "../Client.hpp"
#include "../discord/GatewayWorker.hpp"
#include "../discord/NetworkWorker.hpp"
#include "../models/Models.hpp"
#include "FeatureConstants.hpp"

#include <QDebug>
#include <QMetaObject>

namespace {
QList<DiscordMessage>
variantMessagesToDiscordMessages(const QVariantList &items) {
  QList<DiscordMessage> messages;
  for (int i = 0; i < items.size(); ++i) {
    DiscordMessage message =
        DiscordMessage::fromVariantMap(items.at(i).toMap());
    if (!message.id.isEmpty() && !message.channelId.isEmpty()) {
      messages.append(message);
    }
  }
  return messages;
}
} // namespace

void DiscordClient::subscribeToGuildChannel(const QString &channelId,
                                            const QString &guildId) {
  QString safeChannelId = channelId.trimmed();
  QString safeGuildId = guildId.trimmed();
  if (safeChannelId.isEmpty() || safeGuildId.isEmpty()) {
    qDebug() << "[discord-chat] subscribe skipped"
             << "guild" << safeGuildId << "channel" << safeChannelId;
    return;
  }

  m_chatGuildByChannelId.insert(safeChannelId, safeGuildId);
  qDebug() << "[discord-chat] subscribe guild channel"
           << "guild" << safeGuildId << "channel" << safeChannelId;
  if (m_gatewayWorker != 0) {
    QMetaObject::invokeMethod(m_gatewayWorker, "sendLazyRequest",
                              Qt::QueuedConnection, Q_ARG(QString, safeGuildId),
                              Q_ARG(QString, safeChannelId));
  }
}

void DiscordClient::requestMemberListSync(const QString &channelId,
                                          const QString &guildId) {
  QString safeChannelId = channelId.trimmed();
  QString safeGuildId = guildId.trimmed();
  if (safeChannelId.isEmpty() || safeGuildId.isEmpty()) {
    qDebug() << "[discord-chat] member-list sync skipped"
             << "guild" << safeGuildId << "channel" << safeChannelId;
    return;
  }

  qDebug() << "[discord-chat] member-list sync requested"
           << "guild" << safeGuildId << "channel" << safeChannelId;
  m_pendingMemberListChannelId = safeChannelId;
  if (m_gatewayWorker != 0) {
    QMetaObject::invokeMethod(m_gatewayWorker, "sendMemberListSync",
                              Qt::QueuedConnection, Q_ARG(QString, safeGuildId),
                              Q_ARG(QString, safeChannelId));
  }
}

void DiscordClient::clearMemberListSync() {
  qDebug() << "[discord-chat] member-list sync cleared";
  m_pendingMemberListChannelId.clear();
  if (m_gatewayWorker != 0) {
    QMetaObject::invokeMethod(m_gatewayWorker, "clearMemberListSync",
                              Qt::QueuedConnection);
  }
}

void DiscordClient::loadInitialChatMessages(const QString &channelId,
                                            const QString &guildId) {
  QString safeChannelId = channelId.trimmed();
  if (safeChannelId.isEmpty() || m_token.trimmed().isEmpty()) {
    if (m_store) {
      m_store->setChatLoadingInitial(safeChannelId, false);
    }
    return;
  }

  QString safeGuildId = guildId.trimmed();
  if (!safeGuildId.isEmpty()) {
    qDebug() << "[discord-chat] initial load subscribe"
             << "guild" << safeGuildId << "channel" << safeChannelId;
    subscribeToGuildChannel(safeChannelId, safeGuildId);
  } else {
    qDebug() << "[discord-chat] initial load without guild id"
             << "channel" << safeChannelId;
  }

  if (m_store) {
    m_store->setChatLoadingInitial(safeChannelId, true);
  }
  if (m_networkWorker != 0) {
    QMetaObject::invokeMethod(
        m_networkWorker, "fetchChannelMessages", Qt::QueuedConnection,
        Q_ARG(QString, m_token), Q_ARG(QString, safeChannelId),
        Q_ARG(int, kChatPageSize), Q_ARG(QString, QString()));
  }
}

void DiscordClient::loadOlderChatMessages(const QString &channelId,
                                          const QString &beforeMessageId) {
  QString safeChannelId = channelId.trimmed();
  QString safeBeforeMessageId = beforeMessageId.trimmed();
  if (safeChannelId.isEmpty() || safeBeforeMessageId.isEmpty() ||
      m_token.trimmed().isEmpty()) {
    if (m_store) {
      m_store->setChatLoadingBefore(safeChannelId, false);
    }
    return;
  }

  if (m_store) {
    m_store->setChatLoadingBefore(safeChannelId, true);
  }
  if (m_networkWorker != 0) {
    QMetaObject::invokeMethod(
        m_networkWorker, "fetchChannelMessages", Qt::QueuedConnection,
        Q_ARG(QString, m_token), Q_ARG(QString, safeChannelId),
        Q_ARG(int, kChatPageSize), Q_ARG(QString, safeBeforeMessageId));
  }
}

void DiscordClient::sendChatMessage(const QString &channelId,
                                    const QString &content,
                                    const QString &nonce,
                                    const QString &replyMessageId,
                                    const QStringList &attachmentPaths) {
  QString safeChannelId = channelId.trimmed();
  QString safeNonce = nonce.trimmed();
  if (safeChannelId.isEmpty() || safeNonce.isEmpty() ||
      m_token.trimmed().isEmpty()) {
    if (m_store) {
      m_store->markPendingChatMessageFailed(safeChannelId, safeNonce);
    }
    return;
  }

  if (m_networkWorker != 0) {
    QMetaObject::invokeMethod(
        m_networkWorker, "sendChannelMessage", Qt::QueuedConnection,
        Q_ARG(QString, m_token), Q_ARG(QString, safeChannelId),
        Q_ARG(QString, content), Q_ARG(QString, safeNonce),
        Q_ARG(QString, replyMessageId), Q_ARG(QStringList, attachmentPaths));
  }
}

void DiscordClient::editChatMessage(const QString &channelId,
                                    const QString &messageId,
                                    const QString &content) {
  QString safeChannelId = channelId.trimmed();
  QString safeMessageId = messageId.trimmed();
  if (safeChannelId.isEmpty() || safeMessageId.isEmpty() ||
      m_token.trimmed().isEmpty()) {
    return;
  }

  if (m_networkWorker != 0) {
    QMetaObject::invokeMethod(
        m_networkWorker, "editChannelMessage", Qt::QueuedConnection,
        Q_ARG(QString, m_token), Q_ARG(QString, safeChannelId),
        Q_ARG(QString, safeMessageId), Q_ARG(QString, content));
  }
}

void DiscordClient::deleteChatMessage(const QString &channelId,
                                      const QString &messageId) {
  QString safeChannelId = channelId.trimmed();
  QString safeMessageId = messageId.trimmed();
  if (safeChannelId.isEmpty() || safeMessageId.isEmpty() ||
      m_token.trimmed().isEmpty()) {
    return;
  }

  if (m_networkWorker != 0) {
    QMetaObject::invokeMethod(m_networkWorker, "deleteChannelMessage",
                              Qt::QueuedConnection, Q_ARG(QString, m_token),
                              Q_ARG(QString, safeChannelId),
                              Q_ARG(QString, safeMessageId));
  }
}

void DiscordClient::onChannelMessagesLoaded(const QString &channelId,
                                            const QString &beforeMessageId,
                                            const QVariantList &messages) {
  QString safeChannelId = channelId.trimmed();
  if (safeChannelId.isEmpty() || m_store == 0) {
    return;
  }

  if (safeChannelId != m_store->selectedChannelId()) {
    if (beforeMessageId.trimmed().isEmpty()) {
      m_store->setChatLoadingInitial(safeChannelId, false);
    } else {
      m_store->setChatLoadingBefore(safeChannelId, false);
    }
    return;
  }

  QList<DiscordMessage> parsedMessages =
      variantMessagesToDiscordMessages(messages);
  bool hasMoreBefore = messages.size() >= kChatPageSize;
  if (beforeMessageId.trimmed().isEmpty()) {
    m_store->setInitialChatMessages(safeChannelId,
                                    m_chatGuildByChannelId.value(safeChannelId),
                                    parsedMessages, hasMoreBefore);
    syncGatewayMessageFilterStateToWorker();
  } else {
    m_store->prependOlderChatMessages(safeChannelId, parsedMessages,
                                      hasMoreBefore);
  }
}

void DiscordClient::onChannelMessageSent(const QString &channelId,
                                         const QString &nonce,
                                         const QVariantMap &message) {
  Q_UNUSED(channelId);
  DiscordMessage parsedMessage = DiscordMessage::fromVariantMap(message);
  if (parsedMessage.nonce.isEmpty()) {
    parsedMessage.nonce = nonce;
  }
  if (!parsedMessage.channelId.isEmpty() && m_store != 0) {
    m_store->addOrReplaceChatMessage(parsedMessage);
  }
}

void DiscordClient::onChannelMessageEdited(const QString &channelId,
                                           const QVariantMap &message) {
  Q_UNUSED(channelId);
  DiscordMessage parsedMessage = DiscordMessage::fromVariantMap(message);
  if (!parsedMessage.channelId.isEmpty() && m_store != 0) {
    m_store->updateChatMessage(parsedMessage);
  }
}

void DiscordClient::onChannelMessageDeleted(const QString &channelId,
                                            const QString &messageId) {
  if (m_store != 0) {
    m_store->deleteChatMessage(channelId, messageId);
  }
}

void DiscordClient::onChatRequestFailed(const QString &operation,
                                        const QString &channelId,
                                        const QString &nonce,
                                        const QString &message) {
  qDebug() << "[discord-client] chat request failed" << operation << message;
  if (m_store == 0) {
    return;
  }

  if (operation == "messages") {
    if (channelId.trimmed() != m_store->selectedChannelId()) {
      m_store->setChatLoadingInitial(channelId, false);
      m_store->setChatLoadingBefore(channelId, false);
      return;
    }
    m_store->setChatLoadingInitial(channelId, false);
    m_store->setChatLoadingBefore(channelId, false);
  } else if (operation == "send") {
    m_store->markPendingChatMessageFailed(channelId, nonce);
  }
  setStatusText(message);
}
