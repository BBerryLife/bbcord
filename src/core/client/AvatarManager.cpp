#include "AvatarManager.hpp"

#include "../AvatarCacheWorker.hpp"
#include "../Client.hpp"
#include "../discord/NetworkWorker.hpp"

#include <QDir>
#include <QMetaObject>

AvatarManager::AvatarManager(DiscordClient *client,
                             DiscordNetworkWorker *networkWorker,
                             AvatarCacheWorker *avatarCacheWorker,
                             QObject *parent)
    : QObject(parent), m_enabled(true), m_client(client), m_networkWorker(networkWorker),
      m_avatarCacheWorker(avatarCacheWorker) {}

AvatarManager::~AvatarManager() {}

void AvatarManager::setWorkers(DiscordNetworkWorker *networkWorker,
                               AvatarCacheWorker *avatarCacheWorker) {
  m_networkWorker = networkWorker;
  m_avatarCacheWorker = avatarCacheWorker;
}

void AvatarManager::loadCurrentUserAvatar(const DiscordUser &user,
                                          QVariantMap &avatarCacheRequests,
                                          QString &loadingAvatarUserId,
                                          QString &loadingAvatarUserId2,
                                          QQueue<QVariantMap> &pendingAvatars,
                                          QStringList &queuedAvatarUserIds) {
  if (!m_enabled) {
    return;
  }
  if (user.id.isEmpty() || user.avatarHash.isEmpty()) {
    return;
  }

  QString path = avatarCachePath(user);
  avatarCacheRequests.insert(user.id, user.avatarHash);
  if (m_avatarCacheWorker != 0) {
    QMetaObject::invokeMethod(m_avatarCacheWorker, "checkAvatar",
                              Qt::QueuedConnection, Q_ARG(QString, user.id),
                              Q_ARG(QString, path));
    return;
  }

  if (loadingAvatarUserId.isEmpty()) {
    loadingAvatarUserId = user.id;
    if (m_networkWorker != 0) {
      QMetaObject::invokeMethod(m_networkWorker, "downloadAvatar",
                                Qt::QueuedConnection, Q_ARG(QString, user.id),
                                Q_ARG(QString, user.avatarHash),
                                Q_ARG(QString, path));
    }
  } else if (loadingAvatarUserId2.isEmpty()) {
    loadingAvatarUserId2 = user.id;
    if (m_networkWorker != 0) {
      QMetaObject::invokeMethod(m_networkWorker, "downloadAvatar2",
                                Qt::QueuedConnection, Q_ARG(QString, user.id),
                                Q_ARG(QString, user.avatarHash),
                                Q_ARG(QString, path));
    }
  } else {
    QVariantMap request;
    request["channelId"] = QString();
    request["userId"] = user.id;
    request["avatarHash"] = user.avatarHash;
    request["path"] = path;
    pendingAvatars.enqueue(request);
    queuedAvatarUserIds.append(user.id);
  }
}

void AvatarManager::queueDmAvatar(
    const QString &channelId, const QString &userId, const QString &avatarHash,
    QStringList &loadedAvatarUserIds, QVariantMap &avatarCacheRequests,
    QStringList &queuedAvatarUserIds, QString &loadingAvatarUserId,
    QString &loadingAvatarUserId2, QQueue<QVariantMap> &pendingAvatars) {
  if (!m_enabled) {
    return;
  }
  QString safeChannelId = channelId.trimmed();
  QString safeUserId = userId.trimmed();
  QString safeAvatarHash = avatarHash.trimmed();
  if (safeChannelId.isEmpty() || safeUserId.isEmpty() ||
      safeAvatarHash.isEmpty()) {
    return;
  }

  if (loadedAvatarUserIds.contains(safeUserId) ||
      avatarCacheRequests.contains(safeUserId) ||
      queuedAvatarUserIds.contains(safeUserId) ||
      loadingAvatarUserId == safeUserId || loadingAvatarUserId2 == safeUserId) {
    return;
  }

  DiscordUser user;
  user.id = safeUserId;
  user.avatarHash = safeAvatarHash;
  QString path = avatarCachePath(user);
  avatarCacheRequests.insert(safeUserId, safeAvatarHash);
  if (m_avatarCacheWorker != 0) {
    QMetaObject::invokeMethod(m_avatarCacheWorker, "checkAvatar",
                              Qt::QueuedConnection, Q_ARG(QString, safeUserId),
                              Q_ARG(QString, path));
    return;
  }

  QVariantMap request;
  request["channelId"] = safeChannelId;
  request["userId"] = safeUserId;
  request["avatarHash"] = safeAvatarHash;
  request["path"] = path;
  pendingAvatars.enqueue(request);
  queuedAvatarUserIds.append(safeUserId);
}

void AvatarManager::loadNextAvatar(QString &loadingAvatarUserId,
                                   QString &loadingAvatarUserId2,
                                   QQueue<QVariantMap> &pendingAvatars,
                                   QStringList &queuedAvatarUserIds) {
  if (loadingAvatarUserId.isEmpty() && !pendingAvatars.isEmpty()) {
    QVariantMap request = pendingAvatars.dequeue();
    loadingAvatarUserId = request.value("userId").toString();
    queuedAvatarUserIds.removeAll(loadingAvatarUserId);
    if (m_networkWorker != 0) {
      QMetaObject::invokeMethod(
          m_networkWorker, "downloadAvatar", Qt::QueuedConnection,
          Q_ARG(QString, loadingAvatarUserId),
          Q_ARG(QString, request.value("avatarHash").toString()),
          Q_ARG(QString, request.value("path").toString()));
    }
  }

  if (loadingAvatarUserId2.isEmpty() && !pendingAvatars.isEmpty()) {
    QVariantMap request = pendingAvatars.dequeue();
    loadingAvatarUserId2 = request.value("userId").toString();
    queuedAvatarUserIds.removeAll(loadingAvatarUserId2);
    if (m_networkWorker != 0) {
      QMetaObject::invokeMethod(
          m_networkWorker, "downloadAvatar2", Qt::QueuedConnection,
          Q_ARG(QString, loadingAvatarUserId2),
          Q_ARG(QString, request.value("avatarHash").toString()),
          Q_ARG(QString, request.value("path").toString()));
    }
  }
}

void AvatarManager::queueGuildIcon(
    const QString &guildId, const QString &iconHash,
    QStringList &loadedGuildIconIds, QVariantMap &guildIconCacheRequests,
    QStringList &queuedGuildIconIds, QString &loadingGuildIconId,
    QString &loadingGuildIconId2, QQueue<QVariantMap> &pendingGuildIcons) {
  if (!m_enabled) {
    return;
  }
  QString safeGuildId = guildId.trimmed();
  QString safeIconHash = iconHash.trimmed();
  if (safeGuildId.isEmpty() || safeIconHash.isEmpty()) {
    return;
  }

  if (loadedGuildIconIds.contains(safeGuildId) ||
      guildIconCacheRequests.contains(safeGuildId) ||
      queuedGuildIconIds.contains(safeGuildId) ||
      loadingGuildIconId == safeGuildId || loadingGuildIconId2 == safeGuildId) {
    return;
  }

  QString path = guildIconCachePath(safeGuildId, safeIconHash);
  guildIconCacheRequests.insert(safeGuildId, safeIconHash);
  if (m_avatarCacheWorker != 0) {
    QMetaObject::invokeMethod(m_avatarCacheWorker, "checkGuildIcon",
                              Qt::QueuedConnection, Q_ARG(QString, safeGuildId),
                              Q_ARG(QString, path));
    return;
  }

  QVariantMap request;
  request["guildId"] = safeGuildId;
  request["iconHash"] = safeIconHash;
  request["path"] = path;
  pendingGuildIcons.enqueue(request);
  queuedGuildIconIds.append(safeGuildId);
}

void AvatarManager::loadNextGuildIcon(QString &loadingGuildIconId,
                                      QString &loadingGuildIconId2,
                                      QQueue<QVariantMap> &pendingGuildIcons,
                                      QStringList &queuedGuildIconIds) {
  if (loadingGuildIconId.isEmpty() && !pendingGuildIcons.isEmpty()) {
    QVariantMap request = pendingGuildIcons.dequeue();
    loadingGuildIconId = request.value("guildId").toString();
    queuedGuildIconIds.removeAll(loadingGuildIconId);
    if (m_networkWorker != 0) {
      QMetaObject::invokeMethod(
          m_networkWorker, "downloadGuildIcon", Qt::QueuedConnection,
          Q_ARG(QString, loadingGuildIconId),
          Q_ARG(QString, request.value("iconHash").toString()),
          Q_ARG(QString, request.value("path").toString()));
    }
  }

  if (loadingGuildIconId2.isEmpty() && !pendingGuildIcons.isEmpty()) {
    QVariantMap request = pendingGuildIcons.dequeue();
    loadingGuildIconId2 = request.value("guildId").toString();
    queuedGuildIconIds.removeAll(loadingGuildIconId2);
    if (m_networkWorker != 0) {
      QMetaObject::invokeMethod(
          m_networkWorker, "downloadGuildIcon2", Qt::QueuedConnection,
          Q_ARG(QString, loadingGuildIconId2),
          Q_ARG(QString, request.value("iconHash").toString()),
          Q_ARG(QString, request.value("path").toString()));
    }
  }
}

QString AvatarManager::avatarCachePath(const DiscordUser &user) const {
  QDir dir(QDir::homePath());
  return dir.absoluteFilePath(QString("cache/avatar-cache/%1_%2.png")
                                  .arg(user.id)
                                  .arg(user.avatarHash));
}

QString AvatarManager::guildIconCachePath(const QString &guildId,
                                          const QString &iconHash) const {
  QDir dir(QDir::homePath());
  return dir.absoluteFilePath(
      QString("cache/guild-icon-cache/%1_%2.png").arg(guildId).arg(iconHash));
}

QString AvatarManager::avatarSourceForPath(const QString &path) const {
  QString cleanPath = QDir::fromNativeSeparators(path);
  if (cleanPath.startsWith("/")) {
    return QString("file://%1").arg(cleanPath);
  }

  return QString("file:///%1").arg(cleanPath);
}
