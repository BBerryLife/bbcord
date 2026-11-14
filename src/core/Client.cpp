#include "Client.hpp"

#include "AppStore.hpp"
#include "HubIntegration.hpp"
#include "discord/DiscordUtils.hpp"
#include "discord/GatewayWorker.hpp"
#include "discord/NetworkWorker.hpp"

#include "client/AvatarManager.hpp"
#include "client/CacheManager.hpp"
#include "client/GatewayHandler.hpp"
// Full definition needed: Client.cpp calls m_itemMapper->guildChannelToItem()
// (Client.hpp only forward-declares ItemMapper).
#include "client/ItemMapper.hpp"

#include <QDateTime>
#include <QDebug>
#include <QMetaObject>
#include <QSettings>
#include <QTimer>

// Role id array of a raw member object ("roles") as a QStringList.
namespace {
QStringList roleIdsFromVariantList(const QVariantList &rawRoles) {
  QStringList roleIds;
  for (int i = 0; i < rawRoles.size(); ++i) {
    QString roleId = rawRoles.at(i).toString().trimmed();
    if (!roleId.isEmpty()) {
      roleIds.append(roleId);
    }
  }
  return roleIds;
}
} // namespace

DiscordClient::DiscordClient(QObject *parent)
    : QObject(parent), m_store(0), m_networkThread(0), m_networkWorker(0),
      m_gatewayThread(0), m_gatewayWorker(0), m_gatewayReconnectTimer(0),
      m_gatewayReconnectAttempts(0), m_gatewayReadyAtMs(0),
      m_avatarCacheThread(0),
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
      m_rawSelectedGuildChannels(m_state.rawSelectedGuildChannels),
      m_visibleGuildChannels(m_state.visibleGuildChannels),
      m_channelThreadsByParentId(m_state.channelThreadsByParentId),
      m_activeThreadChannelId(m_state.activeThreadChannelId),
      m_activeThreadParentId(m_state.activeThreadParentId),
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
      m_gatewayThread(0), m_gatewayWorker(0), m_gatewayReconnectTimer(0),
      m_gatewayReconnectAttempts(0), m_gatewayReadyAtMs(0),
      m_avatarCacheThread(0),
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
      m_rawSelectedGuildChannels(m_state.rawSelectedGuildChannels),
      m_visibleGuildChannels(m_state.visibleGuildChannels),
      m_channelThreadsByParentId(m_state.channelThreadsByParentId),
      m_activeThreadChannelId(m_state.activeThreadChannelId),
      m_activeThreadParentId(m_state.activeThreadParentId),
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
  m_requestedMemberRoleKeys.clear();
  m_pendingMemberRoleUserIds.clear();
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

  // The token comes from RestClient::m_token via loginSucceeded, so
  // password/MFA logins also have it for connectGateway().
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

  // Gateway only pushes MESSAGE_CREATE for channels subscribed via OP 14, so
  // re-subscribe the last-open channel on startup. Sends only the request (no UI
  // change); it is flushed once the gateway is Ready.
  if (m_store) {
    QString cachedGuildId = m_store->selectedGuildId().trimmed();
    QString cachedChannelId = m_store->selectedChannelId().trimmed();
    if (!cachedGuildId.isEmpty() && !cachedChannelId.isEmpty() &&
        m_gatewayWorker != 0) {
      m_chatGuildByChannelId.insert(cachedChannelId, cachedGuildId);
      QMetaObject::invokeMethod(m_gatewayWorker, "sendLazyRequest",
                                Qt::QueuedConnection,
                                Q_ARG(QString, cachedGuildId),
                                Q_ARG(QString, cachedChannelId));
    }
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
  if (eventName == "TYPING_START") {
    QString typingChannelId = payload.value("channel_id").toString().trimmed();
    QString typingUserId = payload.value("user_id").toString().trimmed();
    if (typingChannelId.isEmpty() || typingUserId.isEmpty()) {
      return;
    }
    QVariantMap typingMember = payload.value("member").toMap();
    QVariantMap typingUser = typingMember.value("user").toMap();
    QString typingName = typingMember.value("nick").toString().trimmed();
    if (typingName.isEmpty()) {
      typingName = typingUser.value("global_name").toString().trimmed();
    }
    if (typingName.isEmpty()) {
      typingName = typingUser.value("username").toString().trimmed();
    }
    emit typingStarted(typingChannelId, typingUserId, typingName);
    return;
  }

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

    // User-token gateways never send GUILD_CREATE on initial load; guild data
    // (threads, roles) comes in READY.guilds (extracted by buildLightReadyPayload()).
    // Threads are merged via mergeThreadsIntoCache().
    if (eventName == "READY") {
      // Guild roles come from READY.guilds (GUILD_CREATE never fires on initial load);
      // without them canViewChannel() hides every channel.
      // READY guilds have no "members" field, so the user's own roles are fetched via
      // fetchSelfGuildMember() and kept current via GUILD_MEMBER_UPDATE.
      QVariantList readyGuilds = payload.value("guilds").toList();
      for (int i = 0; i < readyGuilds.size(); ++i) {
        QVariantMap guildRaw = readyGuilds.at(i).toMap();
        QString readyGuildId = guildRaw.value("id").toString().trimmed();
        mergeThreadsIntoCache(guildRaw.value("threads").toList(),
                              QVariantList());
        if (!readyGuildId.isEmpty()) {
          mergeGuildRolesIntoCache(readyGuildId, guildRaw);
        }
      }
    }
  }

  if (eventName == "GUILD_CREATE") {
    QString guildId = payload.value("id").toString().trimmed();
    if (!guildId.isEmpty()) {
      mergeGuildRolesIntoCache(guildId, payload);
    }

    // GUILD_CREATE is not sent on initial load; kept as a fallback for joining
    // a new guild while the app is running.
    mergeThreadsIntoCache(payload.value("threads").toList(), QVariantList());
  }


  if (eventName == "GUILD_MEMBER_LIST_UPDATE") {
    // Handles op "SYNC" only (full snapshot after guild_subscribe).
    // INSERT/UPDATE/DELETE live updates are not handled yet.
    QString guildId = payload.value("guild_id").toString().trimmed();
    // Needed for the self-role fallback in the member-parsing loop below.
    QString currentUserId = m_store ? m_store->currentUserId() : QString();
    // The payload has no root "channel_id"; for a single-channel subscribe the
    // channel id is in the root "id". Read "channel_id" first, fall back to "id".
    QString channelId = payload.value("channel_id").toString().trimmed();
    if (channelId.isEmpty()) {
      channelId = payload.value("id").toString().trimmed();
    }
    // "id" may be the list id ("everyone"), so fall back to the channel last
    // requested via requestMemberListSync() if both sources are empty or mismatched.
    if (channelId.isEmpty() || channelId == "everyone") {
      channelId = m_pendingMemberListChannelId;
    }
    // The first SYNC arrives when the channel is opened, before the Members sheet,
    // so m_pendingMemberListChannelId is empty and Discord will not resend it.
    // Fall back to the open channel (selectedChannelId) to avoid losing the data.
    if (channelId.isEmpty() && m_store) {
      channelId = m_store->selectedChannelId();
    }
    qDebug() << "[discord-gateway] GUILD_MEMBER_LIST_UPDATE received guild"
             << guildId << "channel" << channelId << "ops"
             << payload.value("ops").toList().size();

    if (!guildId.isEmpty() && m_store) {
      QVariantList roles = m_store->guildRolesForGuild(guildId);
      // roleId -> position for hoisted roles only (they form Members sheet headings);
      // the highest one decides a member's group/name color.
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
        QMap<QString, QStringList> syncedMemberRoles;
        QMap<QString, QString> syncedMemberNicks;
        for (int i = 0; i < items.size(); ++i) {
          QVariantMap item = items.at(i).toMap();
          if (!item.contains("member")) {
            // No "group" item stored; the Members sheet derives headings from primaryRoleId in QML.
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
            // Hardcoded cdn.discordapp.com (no RestClient here); the API URL in Settings
            // may point to a proxy. Same format as sendAvatarRequest().
            member.avatarUrl = QString("https://cdn.discordapp.com/avatars/"
                                       "%1/%2.png?size=128")
                                    .arg(userId)
                                    .arg(effectiveAvatarHash);
          }

          // "presence" is inside the "member" object, not a sibling of it; reading it
          // from the item always gave "offline".
          QVariantMap presence = memberRaw.value("presence").toMap();
          member.status = presence.value("status").toString();
          if (member.status.isEmpty()) {
            member.status = "offline";
          }

          QVariantList memberRoleIds = memberRaw.value("roles").toList();
          syncedMemberRoles.insert(userId, roleIdsFromVariantList(memberRoleIds));
          syncedMemberNicks.insert(userId, memberRaw.value("nick").toString().trimmed());
          // Pick up the current user's own roles from a member-list SYNC (fallback for
          // fetchSelfGuildMember()). Only updates when roles changed, to avoid redundant
          // recomputeAccessibleGuildChannels() calls.
          if (userId == currentUserId && !guildId.isEmpty() && m_store) {
            QStringList newRoleIds;
            for (int j = 0; j < memberRoleIds.size(); ++j) {
              QString roleId = memberRoleIds.at(j).toString().trimmed();
              if (!roleId.isEmpty()) {
                newRoleIds.append(roleId);
              }
            }
            if (m_store->currentUserRoleIdsForGuild(guildId) != newRoleIds) {
              m_store->setCurrentUserRoleIdsForGuild(guildId, newRoleIds);
              recomputeAccessibleGuildChannels(guildId);
            }
          }

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

        // Also feeds chat name colors: these members' roles are now known for free.
        m_store->setMemberNicks(guildId, syncedMemberNicks);
        m_store->setMemberRoles(guildId, syncedMemberRoles);

        if (!channelId.isEmpty()) {
          m_store->setMemberListForChannel(channelId, parsedMembers);
          qDebug() << "[discord-gateway] GUILD_MEMBER_LIST_UPDATE SYNC parsed"
                   << parsedMembers.size() << "members for channel"
                   << channelId;
        }
      }
    }
  }

  // The REST "active threads" endpoints are rejected for user tokens. Threads
  // arrive via THREAD_LIST_SYNC after op 14 with "threads: true"
  // (buildGuildSubscribePayload()).
  if (eventName == "THREAD_LIST_SYNC") {
    QVariantList rawThreads = payload.value("threads").toList();
    QVariantList syncedChannelIds = payload.value("channel_ids").toList();
    mergeThreadsIntoCache(rawThreads, syncedChannelIds);
    qDebug() << "[discord-gateway] THREAD_LIST_SYNC received guild"
             << payload.value("guild_id").toString() << "threads"
             << rawThreads.size();
  }

  // Member roles for chat name colors. Sources: op 8 answers, the "member" object on a
  // full MESSAGE_CREATE, and role changes of any member.
  if (eventName == "GUILD_MEMBERS_CHUNK" && m_store) {
    QString chunkGuildId = payload.value("guild_id").toString().trimmed();
    QVariantList chunkMembers = payload.value("members").toList();
    QMap<QString, QStringList> rolesByUserId;
    QMap<QString, QString> nicksByUserId;
    for (int i = 0; i < chunkMembers.size(); ++i) {
      QVariantMap chunkMember = chunkMembers.at(i).toMap();
      QString chunkUserId =
          chunkMember.value("user").toMap().value("id").toString().trimmed();
      if (!chunkUserId.isEmpty()) {
        rolesByUserId.insert(chunkUserId,
                             roleIdsFromVariantList(chunkMember.value("roles").toList()));
        nicksByUserId.insert(chunkUserId,
                             chunkMember.value("nick").toString().trimmed());
      }
    }
    // Nicknames first, so the chat refreshes names and colors in one pass.
    m_store->setMemberNicks(chunkGuildId, nicksByUserId);
    m_store->setMemberRoles(chunkGuildId, rolesByUserId);
  }

  if (eventName == "MESSAGE_CREATE" && m_store) {
    QVariantMap authorMember = payload.value("member").toMap();
    QString authorGuildId = payload.value("guild_id").toString().trimmed();
    QString authorUserId =
        payload.value("author").toMap().value("id").toString().trimmed();
    if (authorMember.contains("roles") && !authorGuildId.isEmpty() &&
        !authorUserId.isEmpty()) {
      QMap<QString, QStringList> rolesByUserId;
      rolesByUserId.insert(authorUserId, roleIdsFromVariantList(
                                             authorMember.value("roles").toList()));
      QMap<QString, QString> nicksByUserId;
      nicksByUserId.insert(authorUserId,
                           authorMember.value("nick").toString().trimmed());
      m_store->setMemberNicks(authorGuildId, nicksByUserId);
      m_store->setMemberRoles(authorGuildId, rolesByUserId);
    }
  }

  if (eventName == "GUILD_MEMBER_UPDATE" && m_store) {
    QString updatedGuildId = payload.value("guild_id").toString().trimmed();
    QString updatedUserId =
        payload.value("user").toMap().value("id").toString().trimmed();
    if (!updatedGuildId.isEmpty() && !updatedUserId.isEmpty() &&
        payload.contains("nick")) {
      // "nick": null (nickname removed) arrives as an empty string.
      QMap<QString, QString> updatedNicks;
      updatedNicks.insert(updatedUserId, payload.value("nick").toString().trimmed());
      m_store->setMemberNicks(updatedGuildId, updatedNicks);
    }
    if (!updatedGuildId.isEmpty() && !updatedUserId.isEmpty() &&
        payload.contains("roles")) {
      QMap<QString, QStringList> rolesByUserId;
      rolesByUserId.insert(updatedUserId, roleIdsFromVariantList(
                                              payload.value("roles").toList()));
      m_store->setMemberRoles(updatedGuildId, rolesByUserId);
    }
  }

  // THREAD_CREATE / THREAD_UPDATE carry a full thread channel object; THREAD_DELETE
  // carries only {id, guild_id, parent_id, type}.
  if (eventName == "THREAD_CREATE" || eventName == "THREAD_UPDATE") {
    QVariantList singleThread;
    singleThread.append(payload);
    mergeThreadsIntoCache(singleThread, QVariantList());
  }

  if (eventName == "THREAD_DELETE") {
    QString deletedId = payload.value("id").toString().trimmed();
    QString deletedParentId = payload.value("parent_id").toString().trimmed();
    if (!deletedId.isEmpty() && !deletedParentId.isEmpty()) {
      QVariantMap threadsByParentId = m_channelThreadsByParentId;
      QVariantList siblingThreads = threadsByParentId.value(deletedParentId).toList();
      bool removed = false;
      for (int j = siblingThreads.size() - 1; j >= 0; --j) {
        if (siblingThreads.at(j).toMap().value("id").toString() == deletedId) {
          siblingThreads.removeAt(j);
          removed = true;
        }
      }
      if (removed) {
        threadsByParentId.insert(deletedParentId, siblingThreads);
        m_channelThreadsByParentId = threadsByParentId;
        if (m_store) {
          m_store->setChannelThreadsByParentId(m_channelThreadsByParentId);
        }
      }
    }
  }

  if (eventName == "GUILD_MEMBER_UPDATE") {
    // GUILD_MEMBER_UPDATE: real-time counterpart of fetchSelfGuildMember(); picks up
    // role changes (payload carries the full new role-id array) without reselecting the guild.
    QString guildId = payload.value("guild_id").toString().trimmed();
    QString userId = payload.value("user").toMap().value("id").toString();
    QString currentUserId = m_store ? m_store->currentUserId() : QString();
    if (!guildId.isEmpty() && !userId.isEmpty() && userId == currentUserId) {
      QVariantList roleVariants = payload.value("roles").toList();
      QStringList roleIds;
      for (int i = 0; i < roleVariants.size(); ++i) {
        QString roleId = roleVariants.at(i).toString().trimmed();
        if (!roleId.isEmpty()) {
          roleIds.append(roleId);
        }
      }
      if (m_store) {
        m_store->setCurrentUserRoleIdsForGuild(guildId, roleIds);
      }
      recomputeAccessibleGuildChannels(guildId);
    }
  }

  if (eventName == "MESSAGE_CREATE" || eventName == "MESSAGE_UPDATE" ||
      eventName == "MESSAGE_DELETE") {
    QString channelId = payload.value("channel_id").toString().trimmed();
    bool isSelectedChannel = m_store != 0 && m_store->selectedChannelId() == channelId;
    bool selectedOrLoaded =
        isSelectedChannel ||
        (m_store != 0 && m_store->isChatInitialLoaded(channelId));
    if (selectedOrLoaded || payload.value("guild_id").toString().isEmpty()) {
      qDebug() << "[discord-client] gateway dispatch" << eventName << "guild"
               << payload.value("guild_id").toString() << "channel" << channelId
               << "message" << payload.value("id").toString();
    }

    // MESSAGE_CREATE only: a mention in the already-open channel must clear the
    // guild's unread/mention state (MESSAGE_DELETE/UPDATE create no new unread state).
    if (eventName == "MESSAGE_CREATE" && isSelectedChannel &&
        !channelId.isEmpty()) {
      clearChannelUnreadStateAndRecomputeGuildBadge(channelId);
    }
  }

  if (eventName == "MESSAGE_CREATE" && m_gatewayHandler != 0 &&
      m_hubIntegration != 0) {
    // Push to Hub regardless of the open channel or app state; not limited by selectedOrLoaded.
    MentionNotification notification =
        m_gatewayHandler->buildMentionNotification(payload);
    if (notification.shouldNotify) {
      m_hubIntegration->upsertThreadItem(
          notification.sourceId, notification.title, notification.preview,
          notification.timestampMs);
      // Ping sound under the same shouldNotify condition; independent of UDS/init().
      m_hubIntegration->playPingSound();
    }
  }

  m_gatewayHandler->applyGatewayOrderingEvent(
      eventName, payload, m_pendingUnreadGuildIds,
      m_pendingMentionCountsByGuildId, m_pendingMentionCountsByChannelId,
      m_pendingUnreadChannelIds, m_pendingDmUiUpdate, m_gatewayUiUpdateQueued);
}

void DiscordClient::requestMemberRoles(const QString &guildId,
                                       const QString &userId) {
  QString safeGuildId = guildId.trimmed();
  QString safeUserId = userId.trimmed();
  if (safeGuildId.isEmpty() || safeUserId.isEmpty() || !m_loggedIn ||
      m_gatewayWorker == 0) {
    return;
  }
  // Roles and nickname arrive together in the op 8 answer; ask until both are known.
  if (m_store && m_store->hasMemberRoles(safeGuildId, safeUserId) &&
      m_store->hasMemberNick(safeGuildId, safeUserId)) {
    return;
  }

  QString key = safeGuildId + ":" + safeUserId;
  if (m_requestedMemberRoleKeys.contains(key)) {
    return;
  }
  m_requestedMemberRoleKeys.insert(key);

  // The first id of a burst schedules the flush; the rest of the burst joins the batch.
  bool scheduleFlush = m_pendingMemberRoleUserIds.isEmpty();
  m_pendingMemberRoleUserIds[safeGuildId].append(safeUserId);
  if (scheduleFlush) {
    QTimer::singleShot(150, this, SLOT(flushMemberRoleRequests()));
  }
}

void DiscordClient::flushMemberRoleRequests() {
  QMap<QString, QStringList> pending = m_pendingMemberRoleUserIds;
  m_pendingMemberRoleUserIds.clear();
  if (m_gatewayWorker == 0) {
    return;
  }

  for (QMap<QString, QStringList>::const_iterator it = pending.constBegin();
       it != pending.constEnd(); ++it) {
    QStringList remaining = it.value();
    while (!remaining.isEmpty()) {
      // op 8 accepts at most 100 user_ids.
      QStringList batch = remaining.mid(0, 100);
      remaining = remaining.mid(100);
      QMetaObject::invokeMethod(
          m_gatewayWorker, "requestGuildMembers", Qt::QueuedConnection,
          Q_ARG(QString, it.key()), Q_ARG(QStringList, batch));
    }
  }
}

void DiscordClient::mergeGuildRolesIntoCache(const QString &guildId,
                                             const QVariantMap &guildRaw) {
  if (!m_store) {
    return;
  }

  // "roles": full role objects, same shape from READY guilds and GUILD_CREATE.
  // color == 0 means no custom color, so leave it empty rather than "#000000".
  QVariantList roleVariants = guildRaw.value("roles").toList();
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
    // "permissions" is a stringified int64; toLongLong() parses it directly.
    role.permissions = roleRaw.value("permissions").toLongLong();

    bool colorOk = false;
    qint64 colorValue = roleRaw.value("color").toLongLong(&colorOk);
    if (colorOk && colorValue > 0) {
      role.color = "#" +
                   QString("%1").arg(colorValue, 6, 16, QLatin1Char('0'))
                       .toUpper();
    }

    QVariantMap roleMap;
    roleMap["id"] = role.id;
    roleMap["guildId"] = role.guildId;
    roleMap["name"] = role.name;
    roleMap["color"] = role.color;
    roleMap["position"] = role.position;
    roleMap["hoisted"] = role.hoisted;
    roleMap["permissions"] = role.permissions;
    parsedRoles.append(roleMap);
  }
  m_store->setGuildRoles(guildId, parsedRoles);

  // "members": array of member objects (there is no single self "member" field on
  // the user gateway). Find our user.id to get our roles (Hub role pings,
  // canViewChannel()). Only present in real GUILD_CREATE payloads, so this loop
  // does nothing on the READY path; harmless there.
  QString currentUserId = m_store->currentUserId();
  if (currentUserId.isEmpty()) {
    return;
  }
  QVariantList members = guildRaw.value("members").toList();
  for (int i = 0; i < members.size(); ++i) {
    QVariantMap member = members.at(i).toMap();
    if (member.value("user").toMap().value("id").toString() !=
        currentUserId) {
      continue;
    }
    QVariantList roleVariantsForMember = member.value("roles").toList();
    QStringList roleIds;
    for (int j = 0; j < roleVariantsForMember.size(); ++j) {
      QString roleId = roleVariantsForMember.at(j).toString().trimmed();
      if (!roleId.isEmpty()) {
        roleIds.append(roleId);
      }
    }
    m_store->setCurrentUserRoleIdsForGuild(guildId, roleIds);
    break;
  }
}

void DiscordClient::mergeThreadsIntoCache(
    const QVariantList &rawThreads, const QVariantList &channelIdsToClear) {
  QVariantMap threadsByParentId = m_channelThreadsByParentId;

  // channelIdsToClear: channels with no active threads left (THREAD_LIST_SYNC only);
  // their old keys are removed before reloading.
  for (int i = 0; i < channelIdsToClear.size(); ++i) {
    threadsByParentId.remove(channelIdsToClear.at(i).toString());
  }

  for (int i = 0; i < rawThreads.size(); ++i) {
    QVariantMap item = m_itemMapper->guildChannelToItem(rawThreads.at(i).toMap());
    QString itemId = item.value("id").toString();
    QString parentId = item.value("parentId").toString();
    if (itemId.isEmpty() || parentId.isEmpty()) {
      continue;
    }

    QVariantList siblingThreads = threadsByParentId.value(parentId).toList();
    // THREAD_LIST_SYNC can fire several times per guild, so remove the existing item
    // with the same itemId before appending (newest wins, no duplicates).
    for (int j = siblingThreads.size() - 1; j >= 0; --j) {
      if (siblingThreads.at(j).toMap().value("id").toString() == itemId) {
        siblingThreads.removeAt(j);
      }
    }
    siblingThreads.append(item);
    threadsByParentId.insert(parentId, siblingThreads);
  }

  m_channelThreadsByParentId = threadsByParentId;
  if (m_store) {
    m_store->setChannelThreadsByParentId(m_channelThreadsByParentId);
  }
}

void DiscordClient::onGatewayReady(const QString &sessionId) {
  Q_UNUSED(sessionId);

  // READY or RESUMED. The backoff is only reset once this connection has stayed up for a
  // while (see onGatewayClosedWithCode()), so a server that accepts and immediately drops us
  // cannot turn into a 1-second reconnect loop.
  m_gatewayReadyAtMs = QDateTime::currentMSecsSinceEpoch();

  // A new gateway session forgot any op 8 that was dropped while disconnected.
  m_requestedMemberRoleKeys.clear();
  m_pendingMemberRoleUserIds.clear();

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

void DiscordClient::onGatewayClosedWithCode(int closeCode, bool fatal) {
  qDebug() << "[discord-client] gateway closed, code" << closeCode;
  if (m_busy && !m_loggedIn) {
    QString message = "Discord gateway connection closed";
    if (fatal) {
      message = QString("Discord rejected the connection (code %1)")
                    .arg(closeCode);
    }
    setBusy(false);
    setLoggedIn(false);
    setStatusText(message);
    emit loginFailed(message);
    return;
  }

  if (!m_loggedIn || m_token.trimmed().isEmpty() || m_gatewayWorker == 0) {
    return;
  }

  if (fatal) {
    // 4004 (bad token), 4010-4014 (shard/version/intents): reconnecting can never work,
    // and retrying only floods Discord with failing logins. Stop and tell the user.
    m_gatewayReconnectAttempts = 0;
    if (m_gatewayReconnectTimer != 0) {
      m_gatewayReconnectTimer->stop();
    }
    setStatusText(QString("Discord closed the connection (code %1). "
                          "Log in again.")
                      .arg(closeCode));
    return;
  }

  static const qint64 kHealthyMs = 60000;
  if (m_gatewayReadyAtMs > 0 &&
      QDateTime::currentMSecsSinceEpoch() - m_gatewayReadyAtMs >= kHealthyMs) {
    m_gatewayReconnectAttempts = 0;
  }
  m_gatewayReadyAtMs = 0;

  // Exponential backoff with jitter: 1, 2, 4 ... 256 s, capped at 300 s. It never gives up
  // (a phone can be offline for hours) but it can no longer spin: at the cap that is at most
  // 12 attempts per hour, and most reconnects are RESUMEs, not new sessions.
  static const int kBaseDelayMs = 1000;
  static const int kMaxDelayMs = 300000;
  int exponent = m_gatewayReconnectAttempts < 9 ? m_gatewayReconnectAttempts : 9;
  int delayMs = kBaseDelayMs << exponent;
  if (delayMs > kMaxDelayMs) {
    delayMs = kMaxDelayMs;
  }
  delayMs += qrand() % 1000;
  ++m_gatewayReconnectAttempts;

  if (m_gatewayReconnectTimer == 0) {
    m_gatewayReconnectTimer = new QTimer(this);
    m_gatewayReconnectTimer->setSingleShot(true);
    connect(m_gatewayReconnectTimer, SIGNAL(timeout()), this,
            SLOT(onGatewayReconnectTimer()));
  }

  qDebug() << "[discord-client] gateway reconnect attempt"
           << m_gatewayReconnectAttempts << "in" << delayMs << "ms";
  setStatusText("Reconnecting gateway...");
  m_gatewayReconnectTimer->start(delayMs);
}

void DiscordClient::onGatewayReconnectTimer() {
  if (!m_loggedIn || m_token.trimmed().isEmpty() || m_gatewayWorker == 0) {
    return;
  }

  syncGatewayOrderingStateToWorker();
  syncGatewayMessageFilterStateToWorker();
  // RESUME when the session is still valid, otherwise one fresh IDENTIFY.
  QMetaObject::invokeMethod(m_gatewayWorker, "reconnectGateway",
                            Qt::QueuedConnection, Q_ARG(QString, m_token));
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

  // Diagnostic logging for flushGatewayUiUpdates() (pending guild/channel ids).
  qDebug() << "[discord-chat] flushGatewayUiUpdates guildIds=" << guildIds
           << "channelIds=" << channelIds
           << "mentionCounts=" << mentionCounts;

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
    // updateGuildChannelUnread() only applies to the currently open guild, so
    // markChannelUnread() records it independently and it is applied when that
    // guild's channels load (see AppStore.hpp).
    if (m_store) {
      m_store->markChannelUnread(channelIds.at(i));
    }
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
