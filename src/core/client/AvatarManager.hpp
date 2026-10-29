#ifndef AVATARMANAGER_HPP_
#define AVATARMANAGER_HPP_

#include "../models/Models.hpp"
#include <QObject>
#include <QQueue>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class DiscordClient;
class DiscordNetworkWorker;
class AvatarCacheWorker;
class AvatarManager : public QObject {
  Q_OBJECT
public:
  explicit AvatarManager(DiscordClient *client,
                         DiscordNetworkWorker *networkWorker,
                         AvatarCacheWorker *avatarCacheWorker,
                         QObject *parent = 0);

  virtual ~AvatarManager();
  // Disabled in the headless service: nobody looks at avatars there, and fetching a few
  // hundred of them in the background only costs battery and data.
  void setEnabled(bool enabled) { m_enabled = enabled; }
  void setWorkers(DiscordNetworkWorker *networkWorker,
                  AvatarCacheWorker *avatarCacheWorker);
  void loadCurrentUserAvatar(const DiscordUser &user,
                             QVariantMap &avatarCacheRequests,
                             QString &loadingAvatarUserId,
                             QString &loadingAvatarUserId2,
                             QQueue<QVariantMap> &pendingAvatars,
                             QStringList &queuedAvatarUserIds);
  void queueDmAvatar(const QString &channelId, const QString &userId,
                     const QString &avatarHash,
                     QStringList &loadedAvatarUserIds,
                     QVariantMap &avatarCacheRequests,
                     QStringList &queuedAvatarUserIds,
                     QString &loadingAvatarUserId,
                     QString &loadingAvatarUserId2,
                     QQueue<QVariantMap> &pendingAvatars);
  void loadNextAvatar(QString &loadingAvatarUserId,
                      QString &loadingAvatarUserId2,
                      QQueue<QVariantMap> &pendingAvatars,
                      QStringList &queuedAvatarUserIds);
  void queueGuildIcon(const QString &guildId, const QString &iconHash,
                      QStringList &loadedGuildIconIds,
                      QVariantMap &guildIconCacheRequests,
                      QStringList &queuedGuildIconIds,
                      QString &loadingGuildIconId, QString &loadingGuildIconId2,
                      QQueue<QVariantMap> &pendingGuildIcons);
  void loadNextGuildIcon(QString &loadingGuildIconId,
                         QString &loadingGuildIconId2,
                         QQueue<QVariantMap> &pendingGuildIcons,
                         QStringList &queuedGuildIconIds);

  QString avatarSourceForPath(const QString &path) const;

private:
  QString avatarCachePath(const DiscordUser &user) const;
  QString guildIconCachePath(const QString &guildId,
                             const QString &iconHash) const;

  bool m_enabled;
  DiscordClient *m_client;
  DiscordNetworkWorker *m_networkWorker;
  AvatarCacheWorker *m_avatarCacheWorker;
};

#endif /* AVATARMANAGER_HPP_ */
