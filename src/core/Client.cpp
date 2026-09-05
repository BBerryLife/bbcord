#include "Client.hpp"

#include "AppStore.hpp"
#include "HubIntegration.hpp"
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
      m_hubIntegration(0),
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
      m_hubIntegration(0),
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

void DiscordClient::onRestLoginSucceeded(const QVariantMap &user,
                                         const QString &token) {
  qDebug() << "[discord-client] REST login succeeded"
           << user.value("id").toString();

  // Client::m_token was previously only ever assigned in login(token) (the
  // "paste a token directly" path) - a password+MFA login never set it,
  // leaving it empty here and making the connectGateway() call below fail
  // with "Discord token is empty" despite the REST login having just
  // succeeded. token now comes from RestClient::m_token via the
  // loginSucceeded signal chain (see succeedWithUser() in Login.cpp).
  QString trimmedToken = token.trimmed();
  if (!trimmedToken.isEmpty()) {
    m_token = trimmedToken;
  }

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

  if (eventName == "GUILD_CREATE") {
    QString guildId = payload.value("id").toString().trimmed();
    QString currentUserId = m_store ? m_store->currentUserId() : QString();
    if (!guildId.isEmpty() && !currentUserId.isEmpty()) {
      // GUILD_CREATE mang theo "members": mảng đầy đủ guild member object
      // (không phải 1 field "member" số ít riêng cho self — đó là hành vi
      // của bot gateway với intent hạn chế, KHÔNG áp dụng cho user-account
      // gateway mà BBCord dùng). Tìm phần tử có user.id khớp chính mình để
      // lấy roles, dùng cho tính năng thông báo Hub khi bị ping qua role.
      QVariantList members = payload.value("members").toList();
      for (int i = 0; i < members.size(); ++i) {
        QVariantMap member = members.at(i).toMap();
        if (member.value("user").toMap().value("id").toString() ==
            currentUserId) {
          QVariantList roleVariants = member.value("roles").toList();
          QStringList roleIds;
          for (int j = 0; j < roleVariants.size(); ++j) {
            QString roleId = roleVariants.at(j).toString().trimmed();
            if (!roleId.isEmpty()) {
              roleIds.append(roleId);
            }
          }
          if (m_store) {
            m_store->setCurrentUserRoleIdsForGuild(guildId, roleIds);
          }
          break;
        }
      }
    }

    if (!guildId.isEmpty() && m_store) {
      // "roles": mảng role object đầy đủ của guild (id/name/color/
      // position/hoist/...), tách biệt hoàn toàn với "members" ở trên.
      // color == 0 nghĩa là role không có màu riêng — Discord client
      // thật không hiển thị màu đen cho trường hợp này mà dùng màu chữ
      // mặc định, nên ta để color rỗng thay vì "#000000".
      QVariantList roleVariants = payload.value("roles").toList();
      QVariantList parsedRoles;
      for (int i = 0; i < roleVariants.size(); ++i) {
        QVariantMap roleRaw = roleVariants.at(i).toMap();
        QString roleId = roleRaw.value("id").toString().trimmed();
        if (roleId.isEmpty()) {
          continue;
        }

        DiscordRole role;
        role.id = roleId;
        role.guildId = guildId;
        role.name = roleRaw.value("name").toString();
        role.position = roleRaw.value("position").toInt();
        role.hoisted = roleRaw.value("hoist").toBool();

        bool colorOk = false;
        qint64 colorValue = roleRaw.value("color").toLongLong(&colorOk);
        if (colorOk && colorValue > 0) {
          role.color =
              "#" + QString("%1")
                        .arg(colorValue, 6, 16, QLatin1Char('0'))
                        .toUpper();
        }

        QVariantMap roleMap;
        roleMap["id"] = role.id;
        roleMap["guildId"] = role.guildId;
        roleMap["name"] = role.name;
        roleMap["color"] = role.color;
        roleMap["position"] = role.position;
        roleMap["hoisted"] = role.hoisted;
        parsedRoles.append(roleMap);
      }
      m_store->setGuildRoles(guildId, parsedRoles);
    }
  }

  if (eventName == "GUILD_MEMBER_LIST_UPDATE") {
    // Payload chuẩn Discord (không phải bot-gateway restricted intent):
    //   guild_id: string
    //   id: string (list id, thường "everyone" cho list mặc định)
    //   ops: [ { op: "SYNC"|"INSERT"|"UPDATE"|"DELETE"|"INVALIDATE",
    //            range: [start,end] (chỉ có ở SYNC),
    //            items: [ {group:{id,count}} | {member:{...}} ] } ]
    // Chỉ xử lý op "SYNC" (snapshot đầy đủ, luôn là response đầu tiên
    // sau khi gửi guild_subscribe với 1 channel range — xem
    // sendLazyRequest()/buildGuildSubscribePayload() trong Gateway.cpp).
    // INSERT/UPDATE/DELETE (thay đổi tức thời khi sheet Members đang mở)
    // CHƯA được xử lý — xem comment ở AppStore::setMemberListForChannel().
    QString guildId = payload.value("guild_id").toString().trimmed();
    // Payload chuẩn của op:14/GUILD_MEMBER_LIST_UPDATE (giao thức không
    // chính thức, không có trong docs bot chính thức của Discord) không
    // có field "channel_id" ở cấp root - field đó chỉ tồn tại cho các
    // event channel thông thường (MESSAGE_CREATE, v.v.). Với single-
    // channel subscribe (channels: {"<id>": [[0,99]]} - xem
    // buildMemberListSyncPayload() trong JsonParser.cpp), Discord trả về
    // channel id đó trong field "id" ở cấp root thay vào đó. Đọc
    // "channel_id" trước (phòng trường hợp Discord đổi format), fallback
    // "id" nếu rỗng - trước đây code chỉ đọc "channel_id" nên luôn nhận
    // chuỗi rỗng và vứt bỏ toàn bộ member list đã parse.
    QString channelId = payload.value("channel_id").toString().trimmed();
    if (channelId.isEmpty()) {
      channelId = payload.value("id").toString().trimmed();
    }
    // "id" thường là list id logic ("everyone") chứ không phải channel
    // id thật, nên không đáng tin cậy 100%. Vì luồng hiện tại chỉ theo
    // dõi member list của đúng 1 channel tại một thời điểm, fallback về
    // channel vừa được yêu cầu qua requestMemberListSync() nếu 2 nguồn
    // trên đều rỗng hoặc không khớp channel đang chờ dữ liệu.
    if (channelId.isEmpty() || channelId == "everyone") {
      channelId = m_pendingMemberListChannelId;
    }
    // SYNC ĐẦU TIÊN cho 1 channel luôn tới ngay sau khi channel đó được
    // subscribe (sendLazyRequest() trong Gateway.cpp - gọi khi user MỞ
    // channel, không phải khi mở sheet Members). Tại thời điểm đó,
    // m_pendingMemberListChannelId còn rỗng (sheet Members chưa mở lần
    // nào), nên fallback ở trên không đủ - dữ liệu member list THẬT SỰ
    // đã có trong SYNC này nhưng bị vứt bỏ nếu không xác định được
    // channel. Discord không gửi thêm SYNC nào nữa khi sheet Members mở
    // sau đó và gửi lại đúng request cũ (hành vi đã biết của gateway:
    // request subscribe trùng với subscription hiện có không được trả
    // lời) - nên đây LÀ CƠ HỘI DUY NHẤT để lấy dữ liệu. Fallback về
    // channel đang mở trong khung chat (selectedChannelId) khi vẫn chưa
    // xác định được channel nào khác.
    if (channelId.isEmpty() && m_store) {
      channelId = m_store->selectedChannelId();
    }
    qDebug() << "[discord-gateway] GUILD_MEMBER_LIST_UPDATE received guild"
             << guildId << "channel" << channelId << "ops"
             << payload.value("ops").toList().size();

    if (!guildId.isEmpty() && m_store) {
      QVariantList roles = m_store->guildRolesForGuild(guildId);
      // Map roleId -> position, chỉ giữ role hoisted (chỉ role hoisted
      // mới tạo nhóm/heading trong sheet Members). Role có position cao
      // nhất trong số role member sở hữu quyết định nhóm/màu tên hiển
      // thị, đúng hành vi Discord client thật.
      QMap<QString, int> hoistedRolePosition;
      for (int i = 0; i < roles.size(); ++i) {
        QVariantMap role = roles.at(i).toMap();
        if (role.value("hoisted").toBool()) {
          hoistedRolePosition.insert(role.value("id").toString(),
                                     role.value("position").toInt());
        }
      }

      QVariantList ops = payload.value("ops").toList();
      for (int opIndex = 0; opIndex < ops.size(); ++opIndex) {
        QVariantMap op = ops.at(opIndex).toMap();
        if (op.value("op").toString() != "SYNC") {
          continue;
        }

        QVariantList items = op.value("items").toList();
        QVariantList parsedMembers;
        for (int i = 0; i < items.size(); ++i) {
          QVariantMap item = items.at(i).toMap();
          if (!item.contains("member")) {
            // Phần tử "group" (heading role) — sheet Members tự tính
            // heading từ primaryRoleId của từng member ở tầng QML, không
            // cần lưu riêng "group" item ở đây để tránh trùng lặp logic
            // giữa C++ và QML.
            continue;
          }

          QVariantMap memberRaw = item.value("member").toMap();
          QVariantMap user = memberRaw.value("user").toMap();
          QString userId = user.value("id").toString().trimmed();
          if (userId.isEmpty()) {
            continue;
          }

          DiscordMember member;
          member.userId = userId;
          member.displayName = memberRaw.value("nick").toString();
          if (member.displayName.isEmpty()) {
            member.displayName = user.value("global_name").toString();
          }
          if (member.displayName.isEmpty()) {
            member.displayName = user.value("username").toString();
          }

          QString avatarHash = memberRaw.value("avatar").toString();
          QString userAvatarHash = user.value("avatar").toString();
          QString effectiveAvatarHash =
              !avatarHash.isEmpty() ? avatarHash : userAvatarHash;
          if (!effectiveAvatarHash.isEmpty()) {
            // Hardcode cdn.discordapp.com thay vì dùng
            // DiscordRestClient::cdnBaseUrl() — không có instance
            // RestClient tiện dụng ở scope này, và cdn.discordapp.com là
            // hostname ổn định của Discord CDN (khác biệt với tùy chỉnh
            // API URL trong Settings, vốn nhắm tới API proxy chứ không
            // phải CDN). Cùng format "%1/%2.png?size=128" với
            // RestClientRequests.cpp::sendAvatarRequest() để nhất quán.
            member.avatarUrl = QString("https://cdn.discordapp.com/avatars/"
                                       "%1/%2.png?size=128")
                                    .arg(userId)
                                    .arg(effectiveAvatarHash);
          }

          QVariantMap presence = item.value("presence").toMap();
          member.status = presence.value("status").toString();
          if (member.status.isEmpty()) {
            member.status = "offline";
          }

          QVariantList memberRoleIds = memberRaw.value("roles").toList();
          int bestPosition = -1;
          for (int j = 0; j < memberRoleIds.size(); ++j) {
            QString roleId = memberRoleIds.at(j).toString();
            if (hoistedRolePosition.contains(roleId)) {
              int position = hoistedRolePosition.value(roleId);
              if (position > bestPosition) {
                bestPosition = position;
                member.primaryRoleId = roleId;
              }
            }
          }

          QVariantMap memberMap;
          memberMap["userId"] = member.userId;
          memberMap["displayName"] = member.displayName;
          memberMap["avatarUrl"] = member.avatarUrl;
          memberMap["status"] = member.status;
          memberMap["primaryRoleId"] = member.primaryRoleId;
          parsedMembers.append(memberMap);
        }

        if (!channelId.isEmpty()) {
          m_store->setMemberListForChannel(channelId, parsedMembers);
          qDebug() << "[discord-gateway] GUILD_MEMBER_LIST_UPDATE SYNC parsed"
                   << parsedMembers.size() << "members for channel"
                   << channelId;
        }
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

  if (eventName == "MESSAGE_CREATE" && m_gatewayHandler != 0 &&
      m_hubIntegration != 0) {
    // Đẩy vào BlackBerry Hub bất kể kênh đó đang mở hay không — user có
    // thể đang xem 1 channel khác, hoặc app ở background/đã tắt màn hình,
    // Hub là kênh thông báo độc lập với UI trong app. Không giới hạn theo
    // selectedOrLoaded như block log phía trên.
    MentionNotification notification =
        m_gatewayHandler->buildMentionNotification(payload);
    if (notification.shouldNotify) {
      m_hubIntegration->upsertThreadItem(
          notification.sourceId, notification.title, notification.preview,
          notification.timestampMs);
      // Âm thanh ping.m4a cho mọi tin nhắn đáng thông báo (giống Zalo10) —
      // cùng điều kiện shouldNotify với dòng Hub ở trên, nhưng gọi độc lập
      // vì playPingSound() không phụ thuộc UDS/init() (xem HubIntegration.cpp).
      m_hubIntegration->playPingSound();
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
