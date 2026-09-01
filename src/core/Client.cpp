#include "Client.hpp"

#include "AppStore.hpp"
#include "discord/DiscordUtils.hpp"
#include "discord/GatewayWorker.hpp"
#include "discord/NetworkWorker.hpp"

#include "client/AvatarManager.hpp"
#include "client/CacheManager.hpp"
#include "client/GatewayHandler.hpp"

#include <QDebug>
#include <QMetaObject>
#include <QSettings>
#include <QTimer>

DiscordClient::DiscordClient(QObject *parent)
    : QObject(parent), m_store(0), m_networkThread(0), m_networkWorker(0),
      m_gatewayThread(0), m_gatewayWorker(0), m_avatarCacheThread(0),
      m_avatarCacheWorker(0), m_cacheManager(0), m_avatarManager(0),
      m_gatewayHandler(0), m_itemMapper(0), m_sortUtils(0),
      m_pendingAvatars(m_avatarState.pendingAvatars),
      m_pendingGuildIcons(m_avatarState.pendingGuildIcons),
      m_avatarCacheRequests(m_avatarState.avatarCacheRequests),
      m_guildIconCacheRequests(m_avatarState.guildIconCacheRequests),
      m_avatarSourcesByUserId(m_avatarState.avatarSourcesByUserId),
      m_dmChannelsById(m_state.dmChannelsById),
      m_dmChannelIdsByRecipientId(m_state.dmChannelIdsByRecipientId),
      m_allDmChannelIndexById(m_state.allDmChannelIndexById),
      m_visibleDmChannelIndexById(m_state.visibleDmChannelIndexById),
      m_loadingAvatarUserId(m_avatarState.loadingAvatarUserId),
      m_loadingAvatarUserId2(m_avatarState.loadingAvatarUserId2),
      m_loadingGuildIconId(m_avatarState.loadingGuildIconId),
      m_loadingGuildIconId2(m_avatarState.loadingGuildIconId2),
      m_queuedAvatarUserIds(m_avatarState.queuedAvatarUserIds),
      m_loadedAvatarUserIds(m_avatarState.loadedAvatarUserIds),
      m_queuedGuildIconIds(m_avatarState.queuedGuildIconIds),
      m_loadedGuildIconIds(m_avatarState.loadedGuildIconIds),
      m_orderedGuildIds(m_state.orderedGuildIds),
      m_guildFolders(m_state.guildFolders),
      m_dmPresenceByUserId(m_state.dmPresenceByUserId),
      m_lastGuildId(m_state.lastGuildId),
      m_lastDmChannelId(m_state.lastDmChannelId),
      m_selectedGuildId(m_state.selectedGuildId), m_guilds(m_state.guilds),
      m_allDmChannels(m_state.allDmChannels), m_dmChannels(m_state.dmChannels),
      m_allGuildChannels(m_state.allGuildChannels),
      m_visibleGuildChannels(m_state.visibleGuildChannels),
      m_pendingMentionCountsByGuildId(m_state.pendingMentionCountsByGuildId),
      m_pendingMentionCountsByChannelId(
          m_state.pendingMentionCountsByChannelId),
      m_pendingUnreadGuildIds(m_state.pendingUnreadGuildIds),
      m_pendingUnreadChannelIds(m_state.pendingUnreadChannelIds),
      m_pendingDmPresenceUserIds(m_state.pendingDmPresenceUserIds),
      m_visibleDmChannelCount(m_state.visibleDmChannelCount),
      m_visibleGuildChannelCount(m_state.visibleGuildChannelCount),
      m_loadingGuilds(m_state.loadingGuilds),
      m_loadingDmChannels(m_state.loadingDmChannels),
      m_loadingGuildChannels(m_state.loadingGuildChannels),
      m_guildsHasMore(m_state.guildsHasMore),
      m_dmChannelsHasMore(m_state.dmChannelsHasMore),
      m_guildChannelsHasMore(m_state.guildChannelsHasMore), m_loggedIn(false),
      m_busy(false), m_gatewayUiUpdateQueued(m_state.gatewayUiUpdateQueued),
      m_pendingDmUiUpdate(m_state.pendingDmUiUpdate),
      m_guildsCacheSaveQueued(m_state.guildsCacheSaveQueued),
      m_dmCacheSaveQueued(m_state.dmCacheSaveQueued),
      m_bootstrapCacheLoaded(m_state.bootstrapCacheLoaded),
      m_statusText("Disconnected") {
  initializeNetworkWorker();
  initializeGatewayWorker();
  initializeAvatarCacheWorker();
  initializeManagers();

  QSettings settings;
  m_token = settings.value("auth/token").toString();
}

DiscordClient::DiscordClient(AppStore *store, QObject *parent)
    : QObject(parent), m_store(store), m_networkThread(0), m_networkWorker(0),
      m_gatewayThread(0), m_gatewayWorker(0), m_avatarCacheThread(0),
      m_avatarCacheWorker(0), m_cacheManager(0), m_avatarManager(0),
      m_gatewayHandler(0), m_itemMapper(0), m_sortUtils(0),
      m_pendingAvatars(m_avatarState.pendingAvatars),
      m_pendingGuildIcons(m_avatarState.pendingGuildIcons),
      m_avatarCacheRequests(m_avatarState.avatarCacheRequests),
      m_guildIconCacheRequests(m_avatarState.guildIconCacheRequests),
      m_avatarSourcesByUserId(m_avatarState.avatarSourcesByUserId),
      m_dmChannelsById(m_state.dmChannelsById),
      m_dmChannelIdsByRecipientId(m_state.dmChannelIdsByRecipientId),
      m_allDmChannelIndexById(m_state.allDmChannelIndexById),
      m_visibleDmChannelIndexById(m_state.visibleDmChannelIndexById),
      m_loadingAvatarUserId(m_avatarState.loadingAvatarUserId),
      m_loadingAvatarUserId2(m_avatarState.loadingAvatarUserId2),
      m_loadingGuildIconId(m_avatarState.loadingGuildIconId),
      m_loadingGuildIconId2(m_avatarState.loadingGuildIconId2),
      m_queuedAvatarUserIds(m_avatarState.queuedAvatarUserIds),
      m_loadedAvatarUserIds(m_avatarState.loadedAvatarUserIds),
      m_queuedGuildIconIds(m_avatarState.queuedGuildIconIds),
      m_loadedGuildIconIds(m_avatarState.loadedGuildIconIds),
      m_orderedGuildIds(m_state.orderedGuildIds),
      m_guildFolders(m_state.guildFolders),
      m_dmPresenceByUserId(m_state.dmPresenceByUserId),
      m_lastGuildId(m_state.lastGuildId),
      m_lastDmChannelId(m_state.lastDmChannelId),
      m_selectedGuildId(m_state.selectedGuildId), m_guilds(m_state.guilds),
      m_allDmChannels(m_state.allDmChannels), m_dmChannels(m_state.dmChannels),
      m_allGuildChannels(m_state.allGuildChannels),
      m_visibleGuildChannels(m_state.visibleGuildChannels),
      m_pendingMentionCountsByGuildId(m_state.pendingMentionCountsByGuildId),
      m_pendingMentionCountsByChannelId(
          m_state.pendingMentionCountsByChannelId),
      m_pendingUnreadGuildIds(m_state.pendingUnreadGuildIds),
      m_pendingUnreadChannelIds(m_state.pendingUnreadChannelIds),
      m_pendingDmPresenceUserIds(m_state.pendingDmPresenceUserIds),
      m_visibleDmChannelCount(m_state.visibleDmChannelCount),
      m_visibleGuildChannelCount(m_state.visibleGuildChannelCount),
      m_loadingGuilds(m_state.loadingGuilds),
      m_loadingDmChannels(m_state.loadingDmChannels),
      m_loadingGuildChannels(m_state.loadingGuildChannels),
      m_guildsHasMore(m_state.guildsHasMore),
      m_dmChannelsHasMore(m_state.dmChannelsHasMore),
      m_guildChannelsHasMore(m_state.guildChannelsHasMore), m_loggedIn(false),
      m_busy(false), m_gatewayUiUpdateQueued(m_state.gatewayUiUpdateQueued),
      m_pendingDmUiUpdate(m_state.pendingDmUiUpdate),
      m_guildsCacheSaveQueued(m_state.guildsCacheSaveQueued),
      m_dmCacheSaveQueued(m_state.dmCacheSaveQueued),
      m_bootstrapCacheLoaded(m_state.bootstrapCacheLoaded),
      m_statusText("Disconnected") {
  initializeNetworkWorker();
  initializeGatewayWorker();
  initializeAvatarCacheWorker();
  initializeManagers();

  QSettings settings;
  m_token = settings.value("auth/token").toString();
}

DiscordClient::~DiscordClient() {
  shutdownAvatarCacheWorker();
  shutdownGatewayWorker();
  shutdownNetworkWorker();
}

void DiscordClient::login(const QString &token) {
  QString trimmedToken = token.trimmed();
  if (trimmedToken.isEmpty()) {
    setStatusText("Token is empty");
    emit loginFailed(m_statusText);
    return;
  }

  setLoggedIn(false);
  setBusy(true);
  setStatusText("Checking Discord token...");

  m_token = trimmedToken;
  if (m_networkWorker == 0 || m_networkThread == 0) {
    initializeNetworkWorker();
  }
  if (m_gatewayWorker == 0 || m_gatewayThread == 0) {
    initializeGatewayWorker();
  }
  if (m_networkWorker != 0) {
    QMetaObject::invokeMethod(m_networkWorker, "loginWithToken",
                              Qt::QueuedConnection,
                              Q_ARG(QString, trimmedToken));
  }
}

void DiscordClient::loginWithPassword(const QString &email,
                                      const QString &password) {
  QString trimmedEmail = email.trimmed();
  if (trimmedEmail.isEmpty() || password.isEmpty()) {
    setStatusText("Email/phone number and password are required");
    emit loginFailed(m_statusText);
    return;
  }

  setLoggedIn(false);
  setBusy(true);
  setStatusText("Logging in...");

  if (m_networkWorker == 0 || m_networkThread == 0) {
    initializeNetworkWorker();
  }
  if (m_gatewayWorker == 0 || m_gatewayThread == 0) {
    initializeGatewayWorker();
  }
  if (m_networkWorker != 0) {
    QMetaObject::invokeMethod(m_networkWorker, "loginWithPassword",
                              Qt::QueuedConnection, Q_ARG(QString, trimmedEmail),
                              Q_ARG(QString, password));
  }
}

void DiscordClient::submitMfaCode(const QString &ticket,
                                  const QString &loginInstanceId,
                                  const QString &code) {
  QString trimmedTicket = ticket.trimmed();
  QString trimmedCode = code.trimmed();
  if (trimmedTicket.isEmpty() || trimmedCode.isEmpty()) {
    setStatusText("Verification code is required");
    emit loginFailed(m_statusText);
    return;
  }

  setBusy(true);
  setStatusText("Verifying code...");

  if (m_networkWorker == 0 || m_networkThread == 0) {
    initializeNetworkWorker();
  }
  if (m_networkWorker != 0) {
    QMetaObject::invokeMethod(
        m_networkWorker, "submitMfaCode", Qt::QueuedConnection,
        Q_ARG(QString, trimmedTicket), Q_ARG(QString, loginInstanceId.trimmed()),
        Q_ARG(QString, trimmedCode));
  }
}

void DiscordClient::submitCaptchaKey(const QString &captchaKey) {
  QString trimmedKey = captchaKey.trimmed();
  if (trimmedKey.isEmpty()) {
    setStatusText("CAPTCHA was not completed");
    emit loginFailed(m_statusText);
    return;
  }

  setBusy(true);
  setStatusText("Verifying CAPTCHA...");

  if (m_networkWorker == 0 || m_networkThread == 0) {
    initializeNetworkWorker();
  }
  if (m_networkWorker != 0) {
    QMetaObject::invokeMethod(m_networkWorker, "submitCaptchaKey",
                              Qt::QueuedConnection,
                              Q_ARG(QString, trimmedKey));
  }
}

void DiscordClient::autoLogin() {
  if (m_loggedIn || m_busy || m_token.trimmed().isEmpty()) {
    return;
  }

  qDebug() << "[discord-client] auto login with saved token";
  login(m_token);
}

void DiscordClient::logout() {
  clearSavedToken();
  m_cacheManager->clearBootstrapCache();
  shutdownGatewayWorker();
  shutdownNetworkWorker();
  m_avatarCacheRequests.clear();
  m_guildIconCacheRequests.clear();
  m_avatarSourcesByUserId.clear();
  m_dmChannelsById.clear();
  m_dmChannelIdsByRecipientId.clear();
  m_allDmChannelIndexById.clear();
  m_visibleDmChannelIndexById.clear();
  m_pendingAvatars.clear();
  m_pendingGuildIcons.clear();
  m_loadingAvatarUserId.clear();
  m_loadingAvatarUserId2.clear();
  m_loadingGuildIconId.clear();
  m_loadingGuildIconId2.clear();
  m_queuedAvatarUserIds.clear();
  m_loadedAvatarUserIds.clear();
  m_queuedGuildIconIds.clear();
  m_loadedGuildIconIds.clear();
  m_orderedGuildIds.clear();
  m_guildFolders.clear();
  m_dmPresenceByUserId.clear();
  m_pendingDmPresenceUserIds.clear();
  m_guilds.clear();
  m_allDmChannels.clear();
  m_dmChannels.clear();
  m_allGuildChannels.clear();
  m_visibleGuildChannels.clear();
  m_pendingUnreadGuildIds.clear();
  m_pendingMentionCountsByGuildId.clear();
  m_pendingUnreadChannelIds.clear();
  m_gatewayUiUpdateQueued = false;
  m_pendingDmUiUpdate = false;
  m_guildsCacheSaveQueued = false;
  m_dmCacheSaveQueued = false;
  m_bootstrapCacheLoaded = false;
  m_lastGuildId.clear();
  m_lastDmChannelId.clear();
  m_selectedGuildId.clear();
  m_visibleDmChannelCount = 0;
  m_visibleGuildChannelCount = 0;
  m_loadingGuilds = false;
  m_loadingDmChannels = false;
  m_loadingGuildChannels = false;
  m_guildsHasMore = true;
  m_dmChannelsHasMore = true;
  m_guildChannelsHasMore = false;
  if (m_store) {
    m_store->clearSession();
  }
  setBusy(false);
  setLoggedIn(false);
  setStatusText("Disconnected");
}

void DiscordClient::clearAvatarCacheState() {
  m_avatarCacheRequests.clear();
  m_guildIconCacheRequests.clear();
  m_avatarSourcesByUserId.clear();
  m_pendingAvatars.clear();
  m_pendingGuildIcons.clear();
  m_loadingAvatarUserId.clear();
  m_loadingAvatarUserId2.clear();
  m_loadingGuildIconId.clear();
  m_loadingGuildIconId2.clear();
  m_queuedAvatarUserIds.clear();
  m_loadedAvatarUserIds.clear();
  m_queuedGuildIconIds.clear();
  m_loadedGuildIconIds.clear();
}

bool DiscordClient::loggedIn() const { return m_loggedIn; }

bool DiscordClient::busy() const { return m_busy; }

QString DiscordClient::statusText() const { return m_statusText; }

void DiscordClient::onRestLoginSucceeded(const QVariantMap &user) {
  qDebug() << "[discord-client] REST login succeeded"
           << user.value("id").toString();

  if (m_store) {
    DiscordUser currentUser;
    currentUser.id = user.value("id").toString();
    currentUser.username = user.value("username").toString();
    currentUser.discriminator = user.value("discriminator").toString();
    currentUser.globalName = user.value("global_name").toString();
    currentUser.avatarHash = user.value("avatar").toString();
    currentUser.bot = user.value("bot").toBool();
    m_store->setCurrentUser(currentUser);
    m_avatarManager->loadCurrentUserAvatar(
        currentUser, m_avatarCacheRequests, m_loadingAvatarUserId,
        m_loadingAvatarUserId2, m_pendingAvatars, m_queuedAvatarUserIds);
    syncGatewayMessageFilterStateToWorker();
  }

  m_cacheManager->loadBootstrapCache(
      m_guilds, m_allDmChannels, m_avatarSourcesByUserId, m_selectedGuildId,
      m_bootstrapCacheLoaded, m_guildsHasMore, m_lastGuildId,
      m_visibleDmChannelCount, m_lastDmChannelId, m_dmChannelsHasMore);
  if (m_bootstrapCacheLoaded && !m_allDmChannels.isEmpty()) {
    m_dmChannels.clear();
    m_dmChannelsById.clear();
    m_allDmChannelIndexById.clear();
    m_visibleDmChannelIndexById.clear();
    rebuildDmRecipientIndex();
    rebuildDmChannelIndexes();
  }

  setStatusText("Connecting gateway...");
  if (m_gatewayWorker != 0) {
    syncGatewayOrderingStateToWorker();
    syncGatewayMessageFilterStateToWorker();
    QMetaObject::invokeMethod(m_gatewayWorker, "connectGateway",
                              Qt::QueuedConnection, Q_ARG(QString, m_token));
  }
}

void DiscordClient::onRestLoginFailed(const QString &message) {
  qDebug() << "[discord-client] REST login failed" << message;
  setBusy(false);
  setLoggedIn(false);
  if (m_gatewayWorker != 0) {
    QMetaObject::invokeMethod(m_gatewayWorker, "disconnectGateway",
                              Qt::QueuedConnection);
  }
  setStatusText(message);
  emit loginFailed(message);
}

void DiscordClient::onRestMfaRequired(const QString &ticket,
                                      const QString &loginInstanceId) {
  qDebug() << "[discord-client] MFA required";
  setBusy(false);
  setStatusText("Enter your authenticator code");
  emit mfaRequired(ticket, loginInstanceId);
}

void DiscordClient::onRestCaptchaRequired(const QString &requestKind,
                                          const QString &sitekey,
                                          const QString &rqdata,
                                          const QString &rqtoken) {
  qDebug() << "[discord-client] CAPTCHA required for" << requestKind;
  setBusy(false);
  setStatusText("Please complete the CAPTCHA");
  emit captchaRequired(requestKind, sitekey, rqdata, rqtoken);
}

void DiscordClient::onDataRequestFailed(const QString &message) {
  m_loadingGuilds = false;
  m_loadingDmChannels = false;
  m_loadingGuildChannels = false;
  updateDataLoading();
  qDebug() << "[discord-client] data request failed" << message;
  setStatusText(message);
}

void DiscordClient::onAvatarDownloaded(const QString &userId,
                                       const QString &localPath) {
  QString source = m_avatarManager->avatarSourceForPath(localPath);
  m_avatarSourcesByUserId.insert(userId, source);
  if (m_store) {
    m_store->notifyChatAvatarChanged(userId, source);
  }
  if (m_store && userId == m_store->currentUserId()) {
    m_store->setCurrentUserAvatarSource(source);
  }

  for (int i = 0; i < m_allDmChannels.size(); ++i) {
    QVariantMap channel = m_allDmChannels.at(i).toMap();
    if (channel.value("avatarUserId").toString() == userId) {
      channel["avatar"] = source;
      m_allDmChannels.replace(i, channel);
      QString channelId = channel.value("id").toString();
      if (!channelId.isEmpty()) {
        m_dmChannelsById.insert(channelId, channel);
      }
    } else if (channel.value("avatarUserId2").toString() == userId) {
      channel["avatar2"] = source;
      m_allDmChannels.replace(i, channel);
      QString channelId = channel.value("id").toString();
      if (!channelId.isEmpty()) {
        m_dmChannelsById.insert(channelId, channel);
      }
    }
  }

  for (int i = 0; i < m_dmChannels.size(); ++i) {
    QVariantMap channel = m_dmChannels.at(i).toMap();
    if (channel.value("avatarUserId").toString() == userId) {
      channel["avatar"] = source;
      m_dmChannels.replace(i, channel);
      if (m_store) {
        m_store->updateDmAvatar(channel.value("id").toString(), source);
      }
    } else if (channel.value("avatarUserId2").toString() == userId) {
      channel["avatar2"] = source;
      m_dmChannels.replace(i, channel);
      if (m_store) {
        m_store->updateDmAvatar2(channel.value("id").toString(), source);
      }
    }
  }

  if (!m_loadedAvatarUserIds.contains(userId)) {
    m_loadedAvatarUserIds.append(userId);
  }
  if (m_loadingAvatarUserId == userId) {
    m_loadingAvatarUserId.clear();
  }
  if (m_loadingAvatarUserId2 == userId) {
    m_loadingAvatarUserId2.clear();
  }
  scheduleDmChannelsCacheSave();
  m_avatarManager->loadNextAvatar(m_loadingAvatarUserId, m_loadingAvatarUserId2,
                                  m_pendingAvatars, m_queuedAvatarUserIds);
}

void DiscordClient::onAvatarDownloadFailed(const QString &userId,
                                           const QString &message) {
  qDebug() << "[discord-client] avatar download failed" << userId << message;
  m_queuedAvatarUserIds.removeAll(userId);
  if (m_loadingAvatarUserId == userId) {
    m_loadingAvatarUserId.clear();
  }
  if (m_loadingAvatarUserId2 == userId) {
    m_loadingAvatarUserId2.clear();
  }
  m_avatarManager->loadNextAvatar(m_loadingAvatarUserId, m_loadingAvatarUserId2,
                                  m_pendingAvatars, m_queuedAvatarUserIds);
}

void DiscordClient::onGuildIconDownloaded(const QString &guildId,
                                          const QString &localPath) {
  QString source = m_avatarManager->avatarSourceForPath(localPath);
  for (int i = 0; i < m_guilds.size(); ++i) {
    QVariantMap guild = m_guilds.at(i).toMap();
    if (guild.value("id").toString() == guildId) {
      guild["icon"] = source;
      m_guilds.replace(i, guild);
      if (m_store) {
        m_store->updateGuildIcon(guildId, source);
      }
      break;
    }
  }

  if (!m_loadedGuildIconIds.contains(guildId)) {
    m_loadedGuildIconIds.append(guildId);
  }
  if (m_loadingGuildIconId == guildId) {
    m_loadingGuildIconId.clear();
  }
  if (m_loadingGuildIconId2 == guildId) {
    m_loadingGuildIconId2.clear();
  }
  scheduleGuildsCacheSave();
  m_avatarManager->loadNextGuildIcon(m_loadingGuildIconId,
                                     m_loadingGuildIconId2, m_pendingGuildIcons,
                                     m_queuedGuildIconIds);
}

void DiscordClient::onGuildIconDownloadFailed(const QString &guildId,
                                              const QString &message) {
  qDebug() << "[discord-client] guild icon download failed" << guildId
           << message;
  m_queuedGuildIconIds.removeAll(guildId);
  if (m_loadingGuildIconId == guildId) {
    m_loadingGuildIconId.clear();
  }
  if (m_loadingGuildIconId2 == guildId) {
    m_loadingGuildIconId2.clear();
  }
  m_avatarManager->loadNextGuildIcon(m_loadingGuildIconId,
                                     m_loadingGuildIconId2, m_pendingGuildIcons,
                                     m_queuedGuildIconIds);
}

void DiscordClient::onAvatarCacheHit(const QString &userId,
                                     const QString &path) {
  m_avatarCacheRequests.remove(userId);
  QString source = m_avatarManager->avatarSourceForPath(path);
  m_avatarSourcesByUserId.insert(userId, source);
  if (m_store) {
    m_store->notifyChatAvatarChanged(userId, source);
  }
  if (m_store && userId == m_store->currentUserId()) {
    m_store->setCurrentUserAvatarSource(source);
  }

  for (int i = 0; i < m_allDmChannels.size(); ++i) {
    QVariantMap channel = m_allDmChannels.at(i).toMap();
    if (channel.value("avatarUserId").toString() == userId) {
      channel["avatar"] = source;
      m_allDmChannels.replace(i, channel);
      QString channelId = channel.value("id").toString();
      if (!channelId.isEmpty()) {
        m_dmChannelsById.insert(channelId, channel);
      }
    } else if (channel.value("avatarUserId2").toString() == userId) {
      channel["avatar2"] = source;
      m_allDmChannels.replace(i, channel);
      QString channelId = channel.value("id").toString();
      if (!channelId.isEmpty()) {
        m_dmChannelsById.insert(channelId, channel);
      }
    }
  }

  for (int i = 0; i < m_dmChannels.size(); ++i) {
    QVariantMap channel = m_dmChannels.at(i).toMap();
    if (channel.value("avatarUserId").toString() == userId) {
      channel["avatar"] = source;
      m_dmChannels.replace(i, channel);
      if (m_store) {
        m_store->updateDmAvatar(channel.value("id").toString(), source);
      }
    } else if (channel.value("avatarUserId2").toString() == userId) {
      channel["avatar2"] = source;
      m_dmChannels.replace(i, channel);
      if (m_store) {
        m_store->updateDmAvatar2(channel.value("id").toString(), source);
      }
    }
  }

  if (!m_loadedAvatarUserIds.contains(userId)) {
    m_loadedAvatarUserIds.append(userId);
  }
}

void DiscordClient::onAvatarCacheMiss(const QString &userId,
                                      const QString &path) {
  QString avatarHash = m_avatarCacheRequests.value(userId).toString();
  m_avatarCacheRequests.remove(userId);
  if (userId.trimmed().isEmpty() || avatarHash.trimmed().isEmpty()) {
    return;
  }

  QVariantMap request;
  request["channelId"] = QString();
  request["userId"] = userId;
  request["avatarHash"] = avatarHash;
  request["path"] = path;
  m_pendingAvatars.enqueue(request);
  if (!m_queuedAvatarUserIds.contains(userId)) {
    m_queuedAvatarUserIds.append(userId);
  }
  m_avatarManager->loadNextAvatar(m_loadingAvatarUserId, m_loadingAvatarUserId2,
                                  m_pendingAvatars, m_queuedAvatarUserIds);
}

void DiscordClient::onGuildIconCacheHit(const QString &guildId,
                                        const QString &path) {
  m_guildIconCacheRequests.remove(guildId);
  QString source = m_avatarManager->avatarSourceForPath(path);
  for (int i = 0; i < m_guilds.size(); ++i) {
    QVariantMap guild = m_guilds.at(i).toMap();
    if (guild.value("id").toString() == guildId) {
      guild["icon"] = source;
      m_guilds.replace(i, guild);
      if (m_store) {
        m_store->updateGuildIcon(guildId, source);
      }
      break;
    }
  }
  if (!m_loadedGuildIconIds.contains(guildId)) {
    m_loadedGuildIconIds.append(guildId);
  }
}

void DiscordClient::onGuildIconCacheMiss(const QString &guildId,
                                         const QString &path) {
  QString iconHash = m_guildIconCacheRequests.value(guildId).toString();
  m_guildIconCacheRequests.remove(guildId);
  if (guildId.trimmed().isEmpty() || iconHash.trimmed().isEmpty()) {
    return;
  }

  QVariantMap request;
  request["guildId"] = guildId;
  request["iconHash"] = iconHash;
  request["path"] = path;
  m_pendingGuildIcons.enqueue(request);
  if (!m_queuedGuildIconIds.contains(guildId)) {
    m_queuedGuildIconIds.append(guildId);
  }
  m_avatarManager->loadNextGuildIcon(m_loadingGuildIconId,
                                     m_loadingGuildIconId2, m_pendingGuildIcons,
                                     m_queuedGuildIconIds);
}

void DiscordClient::onGatewayDispatch(const QString &eventName,
                                      const QVariantMap &payload) {
  if (eventName == "READY" || eventName == "USER_SETTINGS_PROTO_UPDATE") {
    QVariantList folders = payload.value("guild_folders").toList();
    if (folders.isEmpty()) {
      folders = payload.value("user_settings")
                    .toMap()
                    .value("guild_folders")
                    .toList();
    }
    if (folders.isEmpty()) {
      folders = DiscordUtils::guildFoldersFromUserSettingsProto(
          payload.value("user_settings_proto").toString());
    }
    if (folders.isEmpty()) {
      QVariantMap settings = payload.value("settings").toMap();
      folders = DiscordUtils::guildFoldersFromUserSettingsProto(
          settings.value("proto").toString());
    }
    if (!folders.isEmpty()) {
      m_guildFolders = folders;
      if (m_store) {
        m_store->setGuildFolders(m_guildFolders);
      }
    }
  }

  if (eventName == "MESSAGE_CREATE" || eventName == "MESSAGE_UPDATE" ||
      eventName == "MESSAGE_DELETE") {
    QString channelId = payload.value("channel_id").toString().trimmed();
    bool selectedOrLoaded =
        m_store != 0 && (m_store->selectedChannelId() == channelId ||
                         m_store->isChatInitialLoaded(channelId));
    if (selectedOrLoaded || payload.value("guild_id").toString().isEmpty()) {
      qDebug() << "[discord-client] gateway dispatch" << eventName << "guild"
               << payload.value("guild_id").toString() << "channel" << channelId
               << "message" << payload.value("id").toString();
    }
  }
  m_gatewayHandler->applyGatewayOrderingEvent(
      eventName, payload, m_pendingUnreadGuildIds,
      m_pendingMentionCountsByGuildId, m_pendingMentionCountsByChannelId,
      m_pendingUnreadChannelIds, m_pendingDmUiUpdate, m_gatewayUiUpdateQueued);
}

void DiscordClient::onGatewayReady(const QString &sessionId) {
  Q_UNUSED(sessionId);

  if (!m_loggedIn) {
    saveToken();
    setLoggedIn(true);
    loadGuilds();
    emit loginSucceeded();
  }

  setBusy(false);
  setStatusText("Connected");
}

void DiscordClient::onGatewayError(const QString &message) {
  qDebug() << "[discord-client] gateway error" << message;
  if (m_busy && !m_loggedIn) {
    setBusy(false);
    setLoggedIn(false);
    setStatusText(message);
    emit loginFailed(message);
    return;
  }

  setStatusText(message);
}

void DiscordClient::onGatewayClosed() {
  qDebug() << "[discord-client] gateway closed";
  if (m_busy && !m_loggedIn) {
    QString message = "Discord gateway connection closed";
    setBusy(false);
    setLoggedIn(false);
    setStatusText(message);
    emit loginFailed(message);
    return;
  }

  if (m_loggedIn && !m_token.trimmed().isEmpty() && m_gatewayWorker != 0) {
    setStatusText("Reconnecting gateway...");
    syncGatewayOrderingStateToWorker();
    syncGatewayMessageFilterStateToWorker();
    QMetaObject::invokeMethod(m_gatewayWorker, "connectGateway",
                              Qt::QueuedConnection, Q_ARG(QString, m_token));
  }
}

void DiscordClient::onGatewayGuildsAndDmsReady(
    const QVariantList &guilds, const QVariantList &allDmChannels,
    const QVariantList &visibleDmChannels, const QStringList &orderedGuildIds,
    const QVariantMap &dmPresenceByUserId) {
  QVariantList updatedGuilds = m_guilds;
  QVariantMap guildsById;

  for (int i = 0; i < updatedGuilds.size(); ++i) {
    QVariantMap guild = updatedGuilds.at(i).toMap();
    QString guildId = guild.value("id").toString();
    if (!guildId.isEmpty()) {
      guildsById.insert(guildId, i);
    }
  }

  const QVariantList guildsFromGateway = guilds;
  for (int i = 0; i < guildsFromGateway.size(); ++i) {
    QVariantMap gatewayGuild = guildsFromGateway.at(i).toMap();
    QString guildId = gatewayGuild.value("id").toString();
    if (guildId.isEmpty() || !guildsById.contains(guildId)) {
      continue;
    }

    int guildIndex = guildsById.value(guildId).toInt();
    QVariantMap existingGuild = updatedGuilds.at(guildIndex).toMap();
    existingGuild["unread"] = gatewayGuild.value("unread").toBool();
    existingGuild["mentionCount"] = gatewayGuild.value("mention_count").toInt();
    updatedGuilds.replace(guildIndex, existingGuild);
  }

  m_guilds = updatedGuilds;
  m_orderedGuildIds = orderedGuildIds;
  sortGuilds();
  m_dmPresenceByUserId = dmPresenceByUserId;
  for (QVariantMap::const_iterator it = m_dmPresenceByUserId.constBegin();
       it != m_dmPresenceByUserId.constEnd(); ++it) {
    if (!it.key().isEmpty() && !m_pendingDmPresenceUserIds.contains(it.key())) {
      m_pendingDmPresenceUserIds.append(it.key());
    }
  }

  if (!allDmChannels.isEmpty() || !visibleDmChannels.isEmpty()) {
    m_allDmChannels = allDmChannels;
    m_dmChannels = visibleDmChannels;
  }

  rebuildDmRecipientIndex();
  rebuildDmChannelIndexes();
  applyPendingDmPresences();
  updateStoreWithGuildsAndDms();
  scheduleGuildsCacheSave();
  scheduleDmChannelsCacheSave();
}

void DiscordClient::setLoggedIn(bool loggedIn) {
  if (m_loggedIn == loggedIn) {
    return;
  }

  m_loggedIn = loggedIn;
  if (m_store) {
    m_store->setLoggedIn(m_loggedIn);
  }
  emit loggedInChanged(m_loggedIn);
}

void DiscordClient::setBusy(bool busy) {
  if (m_busy == busy) {
    return;
  }

  m_busy = busy;
  if (m_store) {
    m_store->setBusy(m_busy);
  }
  emit busyChanged(m_busy);
}

void DiscordClient::setStatusText(const QString &statusText) {
  if (m_statusText == statusText) {
    return;
  }

  m_statusText = statusText;
  if (m_store) {
    m_store->setStatusText(m_statusText);
  }
  emit statusTextChanged(m_statusText);
}

void DiscordClient::saveToken() {
  if (m_token.trimmed().isEmpty()) {
    return;
  }

  QSettings settings;
  settings.setValue("auth/token", m_token.trimmed());
  settings.sync();
}

void DiscordClient::clearSavedToken() {
  QSettings settings;
  settings.remove("auth/token");
  settings.sync();
  m_token.clear();
}

void DiscordClient::saveGuildsCache() const {
  m_cacheManager->saveGuildsCache(m_guilds);
}

void DiscordClient::saveDmChannelsCache() const {
  m_cacheManager->saveDmChannelsCache(m_allDmChannels);
}

void DiscordClient::scheduleGuildsCacheSave() {
  if (m_guildsCacheSaveQueued) {
    return;
  }

  m_guildsCacheSaveQueued = true;
  QTimer::singleShot(3000, this, SLOT(savePendingGuildsCache()));
}

void DiscordClient::scheduleDmChannelsCacheSave() {
  if (m_dmCacheSaveQueued) {
    return;
  }

  m_dmCacheSaveQueued = true;
  QTimer::singleShot(3000, this, SLOT(savePendingDmChannelsCache()));
}

void DiscordClient::savePendingGuildsCache() {
  m_guildsCacheSaveQueued = false;
  if (!m_loggedIn) {
    return;
  }
  saveGuildsCache();
}

void DiscordClient::savePendingDmChannelsCache() {
  m_dmCacheSaveQueued = false;
  if (!m_loggedIn) {
    return;
  }
  saveDmChannelsCache();
}

void DiscordClient::updateStoreWithGuildsAndDms() {
  if (m_store) {
    m_store->setGuildFolders(m_guildFolders);
    m_store->reorderGuilds(m_guilds);
    m_store->setDmChannels(m_dmChannels);
  }
}

void DiscordClient::flushGatewayUiUpdates() {
  m_gatewayUiUpdateQueued = false;

  QStringList guildIds = m_pendingUnreadGuildIds;
  QStringList channelIds = m_pendingUnreadChannelIds;
  QVariantMap mentionCounts = m_pendingMentionCountsByGuildId;
  QVariantMap channelMentionCounts = m_pendingMentionCountsByChannelId;
  bool dmChanged = m_pendingDmUiUpdate;
  m_pendingUnreadGuildIds.clear();
  m_pendingUnreadChannelIds.clear();
  m_pendingMentionCountsByGuildId.clear();
  m_pendingMentionCountsByChannelId.clear();
  m_pendingDmUiUpdate = false;

  applyPendingDmPresences();

  for (int i = 0; i < guildIds.size(); ++i) {
    updateGuildUnread(guildIds.at(i), true);
    if (mentionCounts.contains(guildIds.at(i))) {
      updateGuildMentionCount(guildIds.at(i),
                              mentionCounts.value(guildIds.at(i)).toInt());
    }
  }
  for (int i = 0; i < channelIds.size(); ++i) {
    updateGuildChannelUnread(channelIds.at(i), true);
  }
  QStringList mentionChannelIds = channelMentionCounts.keys();
  for (int i = 0; i < mentionChannelIds.size(); ++i) {
    QString mentionChannelId = mentionChannelIds.at(i);
    int mentionCount = 0;
    for (int j = 0; j < m_allGuildChannels.size(); ++j) {
      QVariantMap channel = m_allGuildChannels.at(j).toMap();
      if (channel.value("id").toString() == mentionChannelId) {
        mentionCount = channel.value("mentionCount").toInt();
        break;
      }
    }
    updateGuildChannelMentionCount(
        mentionChannelId,
        mentionCount + channelMentionCounts.value(mentionChannelId).toInt());
  }

  if (!guildIds.isEmpty()) {
    sortGuilds();
  }

  if (m_store) {
    if (!guildIds.isEmpty()) {
      m_store->setGuilds(m_guilds);
    }
    if (dmChanged) {
      m_store->setDmChannels(m_dmChannels);
    }
    if (!channelIds.isEmpty() || !mentionChannelIds.isEmpty()) {
      m_store->setGuildChannels(m_visibleGuildChannels);
    }
  }

  if (!guildIds.isEmpty()) {
    scheduleGuildsCacheSave();
  }
  if (dmChanged) {
    scheduleDmChannelsCacheSave();
    syncGatewayOrderingStateToWorker();
  }
}

void DiscordClient::updateDataLoading() {
  if (m_store) {
    m_store->setDataLoading(m_loadingGuilds || m_loadingDmChannels ||
                            m_loadingGuildChannels);
  }
}
