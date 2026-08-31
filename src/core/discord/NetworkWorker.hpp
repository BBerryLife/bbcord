#ifndef NetworkWorker_HPP_
#define NetworkWorker_HPP_

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include "RestClient.hpp"

class DiscordNetworkWorker : public QObject {
  Q_OBJECT

public:
  explicit DiscordNetworkWorker(QObject *parent = 0);
  virtual ~DiscordNetworkWorker();

public Q_SLOTS:
  void loginWithToken(const QString &token);
  void loginWithPassword(const QString &email, const QString &password);
  void submitMfaCode(const QString &ticket, const QString &loginInstanceId,
                     const QString &code);
  void fetchGuilds(const QString &token, int limit, const QString &afterId);
  void fetchDmChannels(const QString &token, int limit, const QString &afterId);
  void fetchGuildChannels(const QString &token, const QString &guildId,
                          int limit, const QString &afterId);
  void fetchChannelMessages(const QString &token, const QString &channelId,
                            int limit, const QString &beforeMessageId);
  void sendChannelMessage(const QString &token, const QString &channelId,
                          const QString &content, const QString &nonce,
                          const QString &replyMessageId,
                          const QStringList &attachmentPaths);
  void editChannelMessage(const QString &token, const QString &channelId,
                          const QString &messageId, const QString &content);
  void deleteChannelMessage(const QString &token, const QString &channelId,
                            const QString &messageId);
  void downloadAvatar(const QString &userId, const QString &avatarHash,
                      const QString &outputPath);
  void downloadAvatar2(const QString &userId, const QString &avatarHash,
                       const QString &outputPath);
  void downloadGuildIcon(const QString &guildId, const QString &iconHash,
                         const QString &outputPath);
  void downloadGuildIcon2(const QString &guildId, const QString &iconHash,
                          const QString &outputPath);
  void cancelAll();

Q_SIGNALS:
  void loginSucceeded(const QVariantMap &user);
  void loginFailed(const QString &message);
  void mfaRequired(const QString &ticket, const QString &loginInstanceId);
  void guildsLoaded(const QVariantList &guilds);
  void dmChannelsLoaded(const QVariantList &channels);
  void guildChannelsLoaded(const QString &guildId,
                           const QVariantList &channels);
  void channelMessagesLoaded(const QString &channelId,
                             const QString &beforeMessageId,
                             const QVariantList &messages);
  void channelMessageSent(const QString &channelId, const QString &nonce,
                          const QVariantMap &message);
  void channelMessageEdited(const QString &channelId,
                            const QVariantMap &message);
  void channelMessageDeleted(const QString &channelId,
                             const QString &messageId);
  void chatRequestFailed(const QString &operation, const QString &channelId,
                         const QString &nonce, const QString &message);
  void requestFailed(const QString &message);
  void avatarDownloaded(const QString &userId, const QString &localPath);
  void avatarDownloadFailed(const QString &userId, const QString &message);
  void guildIconDownloaded(const QString &guildId, const QString &localPath);
  void guildIconDownloadFailed(const QString &guildId, const QString &message);
  void cancelAllFinished();

private:
  DiscordRestClient m_loginClient;
  DiscordRestClient m_dataClient;
  DiscordRestClient m_chatReadClient;
  DiscordRestClient m_chatWriteClient;
  DiscordRestClient m_avatarClient;
  DiscordRestClient m_avatarClient2;
  DiscordRestClient m_guildIconClient;
  DiscordRestClient m_guildIconClient2;
};

#endif /* NetworkWorker_HPP_ */
