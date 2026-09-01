#include "NetworkWorker.hpp"

#include <QMetaObject>
#include <QThread>

namespace {
bool isInObjectThread(const QObject *object) {
  return object != 0 && object->thread() == QThread::currentThread();
}
} // namespace

DiscordNetworkWorker::DiscordNetworkWorker(QObject *parent)
    : QObject(parent), m_loginClient(this), m_dataClient(this),
      m_chatReadClient(this), m_chatWriteClient(this), m_avatarClient(this),
      m_avatarClient2(this), m_guildIconClient(this), m_guildIconClient2(this) {
  connect(&m_loginClient, SIGNAL(loginSucceeded(QVariantMap)), this,
          SIGNAL(loginSucceeded(QVariantMap)));
  connect(&m_loginClient, SIGNAL(loginFailed(QString)), this,
          SIGNAL(loginFailed(QString)));
  connect(&m_loginClient, SIGNAL(mfaRequired(QString, QString)), this,
          SIGNAL(mfaRequired(QString, QString)));
  connect(&m_loginClient,
          SIGNAL(captchaRequired(QString, QString, QString, QString)), this,
          SIGNAL(captchaRequired(QString, QString, QString, QString)));

  connect(&m_dataClient, SIGNAL(guildsLoaded(QVariantList)), this,
          SIGNAL(guildsLoaded(QVariantList)));
  connect(&m_dataClient, SIGNAL(dmChannelsLoaded(QVariantList)), this,
          SIGNAL(dmChannelsLoaded(QVariantList)));
  connect(&m_dataClient, SIGNAL(guildChannelsLoaded(QString, QVariantList)),
          this, SIGNAL(guildChannelsLoaded(QString, QVariantList)));
  connect(&m_dataClient, SIGNAL(requestFailed(QString)), this,
          SIGNAL(requestFailed(QString)));

  connect(&m_chatReadClient,
          SIGNAL(channelMessagesLoaded(QString, QString, QVariantList)), this,
          SIGNAL(channelMessagesLoaded(QString, QString, QVariantList)));
  connect(&m_chatReadClient,
          SIGNAL(chatRequestFailed(QString, QString, QString, QString)), this,
          SIGNAL(chatRequestFailed(QString, QString, QString, QString)));

  connect(&m_chatWriteClient,
          SIGNAL(channelMessageSent(QString, QString, QVariantMap)), this,
          SIGNAL(channelMessageSent(QString, QString, QVariantMap)));
  connect(&m_chatWriteClient,
          SIGNAL(channelMessageEdited(QString, QVariantMap)), this,
          SIGNAL(channelMessageEdited(QString, QVariantMap)));
  connect(&m_chatWriteClient, SIGNAL(channelMessageDeleted(QString, QString)),
          this, SIGNAL(channelMessageDeleted(QString, QString)));
  connect(&m_chatWriteClient,
          SIGNAL(chatRequestFailed(QString, QString, QString, QString)), this,
          SIGNAL(chatRequestFailed(QString, QString, QString, QString)));

  connect(&m_avatarClient, SIGNAL(avatarDownloaded(QString, QString)), this,
          SIGNAL(avatarDownloaded(QString, QString)));
  connect(&m_avatarClient2, SIGNAL(avatarDownloaded(QString, QString)), this,
          SIGNAL(avatarDownloaded(QString, QString)));
  connect(&m_avatarClient, SIGNAL(avatarDownloadFailed(QString, QString)), this,
          SIGNAL(avatarDownloadFailed(QString, QString)));
  connect(&m_avatarClient2, SIGNAL(avatarDownloadFailed(QString, QString)),
          this, SIGNAL(avatarDownloadFailed(QString, QString)));

  connect(&m_guildIconClient, SIGNAL(guildIconDownloaded(QString, QString)),
          this, SIGNAL(guildIconDownloaded(QString, QString)));
  connect(&m_guildIconClient2, SIGNAL(guildIconDownloaded(QString, QString)),
          this, SIGNAL(guildIconDownloaded(QString, QString)));
  connect(&m_guildIconClient, SIGNAL(guildIconDownloadFailed(QString, QString)),
          this, SIGNAL(guildIconDownloadFailed(QString, QString)));
  connect(&m_guildIconClient2,
          SIGNAL(guildIconDownloadFailed(QString, QString)), this,
          SIGNAL(guildIconDownloadFailed(QString, QString)));
}

DiscordNetworkWorker::~DiscordNetworkWorker() { cancelAll(); }

void DiscordNetworkWorker::loginWithToken(const QString &token) {
  if (!isInObjectThread(this)) {
    QMetaObject::invokeMethod(this, "loginWithToken", Qt::QueuedConnection,
                              Q_ARG(QString, token));
    return;
  }

  m_loginClient.loginWithToken(token);
}

void DiscordNetworkWorker::loginWithPassword(const QString &email,
                                             const QString &password) {
  if (!isInObjectThread(this)) {
    QMetaObject::invokeMethod(this, "loginWithPassword", Qt::QueuedConnection,
                              Q_ARG(QString, email), Q_ARG(QString, password));
    return;
  }

  m_loginClient.loginWithPassword(email, password);
}

void DiscordNetworkWorker::submitMfaCode(const QString &ticket,
                                         const QString &loginInstanceId,
                                         const QString &code) {
  if (!isInObjectThread(this)) {
    QMetaObject::invokeMethod(this, "submitMfaCode", Qt::QueuedConnection,
                              Q_ARG(QString, ticket),
                              Q_ARG(QString, loginInstanceId),
                              Q_ARG(QString, code));
    return;
  }

  m_loginClient.submitMfaCode(ticket, loginInstanceId, code);
}

void DiscordNetworkWorker::submitCaptchaKey(const QString &captchaKey) {
  if (!isInObjectThread(this)) {
    QMetaObject::invokeMethod(this, "submitCaptchaKey", Qt::QueuedConnection,
                              Q_ARG(QString, captchaKey));
    return;
  }

  m_loginClient.submitCaptchaKey(captchaKey);
}

void DiscordNetworkWorker::fetchGuilds(const QString &token, int limit,
                                       const QString &afterId) {
  if (!isInObjectThread(this)) {
    QMetaObject::invokeMethod(this, "fetchGuilds", Qt::QueuedConnection,
                              Q_ARG(QString, token), Q_ARG(int, limit),
                              Q_ARG(QString, afterId));
    return;
  }

  m_dataClient.fetchGuilds(token, limit, afterId);
}

void DiscordNetworkWorker::fetchDmChannels(const QString &token, int limit,
                                           const QString &afterId) {
  if (!isInObjectThread(this)) {
    QMetaObject::invokeMethod(this, "fetchDmChannels", Qt::QueuedConnection,
                              Q_ARG(QString, token), Q_ARG(int, limit),
                              Q_ARG(QString, afterId));
    return;
  }

  m_dataClient.fetchDmChannels(token, limit, afterId);
}

void DiscordNetworkWorker::fetchGuildChannels(const QString &token,
                                              const QString &guildId, int limit,
                                              const QString &afterId) {
  if (!isInObjectThread(this)) {
    QMetaObject::invokeMethod(this, "fetchGuildChannels", Qt::QueuedConnection,
                              Q_ARG(QString, token), Q_ARG(QString, guildId),
                              Q_ARG(int, limit), Q_ARG(QString, afterId));
    return;
  }

  m_dataClient.fetchGuildChannels(token, guildId, limit, afterId);
}

void DiscordNetworkWorker::fetchChannelMessages(
    const QString &token, const QString &channelId, int limit,
    const QString &beforeMessageId) {
  if (!isInObjectThread(this)) {
    QMetaObject::invokeMethod(this, "fetchChannelMessages",
                              Qt::QueuedConnection, Q_ARG(QString, token),
                              Q_ARG(QString, channelId), Q_ARG(int, limit),
                              Q_ARG(QString, beforeMessageId));
    return;
  }

  m_chatReadClient.removeQueuedChannelMessageRequestsExcept(channelId);
  m_chatReadClient.fetchChannelMessages(token, channelId, limit,
                                        beforeMessageId);
}

void DiscordNetworkWorker::sendChannelMessage(
    const QString &token, const QString &channelId, const QString &content,
    const QString &nonce, const QString &replyMessageId,
    const QStringList &attachmentPaths) {
  if (!isInObjectThread(this)) {
    QMetaObject::invokeMethod(this, "sendChannelMessage", Qt::QueuedConnection,
                              Q_ARG(QString, token), Q_ARG(QString, channelId),
                              Q_ARG(QString, content), Q_ARG(QString, nonce),
                              Q_ARG(QString, replyMessageId),
                              Q_ARG(QStringList, attachmentPaths));
    return;
  }

  m_chatWriteClient.sendChannelMessage(token, channelId, content, nonce,
                                       replyMessageId, attachmentPaths);
}

void DiscordNetworkWorker::editChannelMessage(const QString &token,
                                              const QString &channelId,
                                              const QString &messageId,
                                              const QString &content) {
  if (!isInObjectThread(this)) {
    QMetaObject::invokeMethod(this, "editChannelMessage", Qt::QueuedConnection,
                              Q_ARG(QString, token), Q_ARG(QString, channelId),
                              Q_ARG(QString, messageId),
                              Q_ARG(QString, content));
    return;
  }

  m_chatWriteClient.editChannelMessage(token, channelId, messageId, content);
}

void DiscordNetworkWorker::deleteChannelMessage(const QString &token,
                                                const QString &channelId,
                                                const QString &messageId) {
  if (!isInObjectThread(this)) {
    QMetaObject::invokeMethod(this, "deleteChannelMessage",
                              Qt::QueuedConnection, Q_ARG(QString, token),
                              Q_ARG(QString, channelId),
                              Q_ARG(QString, messageId));
    return;
  }

  m_chatWriteClient.deleteChannelMessage(token, channelId, messageId);
}

void DiscordNetworkWorker::downloadAvatar(const QString &userId,
                                          const QString &avatarHash,
                                          const QString &outputPath) {
  if (!isInObjectThread(this)) {
    QMetaObject::invokeMethod(
        this, "downloadAvatar", Qt::QueuedConnection, Q_ARG(QString, userId),
        Q_ARG(QString, avatarHash), Q_ARG(QString, outputPath));
    return;
  }

  m_avatarClient.downloadAvatar(userId, avatarHash, outputPath);
}

void DiscordNetworkWorker::downloadAvatar2(const QString &userId,
                                           const QString &avatarHash,
                                           const QString &outputPath) {
  if (!isInObjectThread(this)) {
    QMetaObject::invokeMethod(
        this, "downloadAvatar2", Qt::QueuedConnection, Q_ARG(QString, userId),
        Q_ARG(QString, avatarHash), Q_ARG(QString, outputPath));
    return;
  }

  m_avatarClient2.downloadAvatar(userId, avatarHash, outputPath);
}

void DiscordNetworkWorker::downloadGuildIcon(const QString &guildId,
                                             const QString &iconHash,
                                             const QString &outputPath) {
  if (!isInObjectThread(this)) {
    QMetaObject::invokeMethod(this, "downloadGuildIcon", Qt::QueuedConnection,
                              Q_ARG(QString, guildId), Q_ARG(QString, iconHash),
                              Q_ARG(QString, outputPath));
    return;
  }

  m_guildIconClient.downloadGuildIcon(guildId, iconHash, outputPath);
}

void DiscordNetworkWorker::downloadGuildIcon2(const QString &guildId,
                                              const QString &iconHash,
                                              const QString &outputPath) {
  if (!isInObjectThread(this)) {
    QMetaObject::invokeMethod(this, "downloadGuildIcon2", Qt::QueuedConnection,
                              Q_ARG(QString, guildId), Q_ARG(QString, iconHash),
                              Q_ARG(QString, outputPath));
    return;
  }

  m_guildIconClient2.downloadGuildIcon(guildId, iconHash, outputPath);
}

void DiscordNetworkWorker::cancelAll() {
  if (!isInObjectThread(this)) {
    QMetaObject::invokeMethod(this, "cancelAll", Qt::QueuedConnection);
    return;
  }

  m_loginClient.cancel();
  m_dataClient.cancel();
  m_chatReadClient.cancel();
  m_chatWriteClient.cancel();
  m_avatarClient.cancel();
  m_avatarClient2.cancel();
  m_guildIconClient.cancel();
  m_guildIconClient2.cancel();
  emit cancelAllFinished();
}
