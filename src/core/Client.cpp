#include "Client.hpp"

#include "AppStore.hpp"
#include "HubIntegration.hpp"
#include "discord/DiscordUtils.hpp"
#include "discord/GatewayWorker.hpp"
#include "discord/NetworkWorker.hpp"

#include "client/AvatarManager.hpp"
#include "client/CacheManager.hpp"
#include "client/GatewayHandler.hpp"
// Fix: Client.hpp only forward-declares "class ItemMapper;" (enough
// for the m_itemMapper pointer member, but NOT enough to call methods
// on it - incomplete type). Previously only GuildChannels.cpp (a
// separate module file) included this header in full. Client.cpp now
// also calls m_itemMapper->guildChannelToItem() directly when handling
// THREAD_LIST_SYNC (onGatewayDispatch()), so it needs the include here
// - confirmed by an actual build error: "invalid use of incomplete
// type 'struct ItemMapper'" at the guildChannelToItem() call site in
// Client.cpp.
#include "client/ItemMapper.hpp"

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

    // Fix: confirmed via real debug logs that GUILD_CREATE is NEVER
    // sent by this user-token gateway (0/140 events in a full session,
    // including opening many guilds/channels/forums) - unlike the
    // traditional bot protocol. All full guild data (channels, threads,
    // roles, members...) is bundled by Discord directly into the
    // "guilds" field of the READY payload - this is also why the READY
    // payload is ~5MB even with only 49 guilds. buildLightReadyPayload()
    // (GatewayEvents.cpp) was updated to extract this raw "guilds"
    // array (without parsing the full JSON payload, keeping the
    // fast-path optimization) - loop through it here to get each
    // guild's threads, sharing mergeThreadsIntoCache() with
    // THREAD_LIST_SYNC.
    if (eventName == "READY") {
      // Fix: the user-token gateway does NOT send individual
      // GUILD_CREATE events per guild on initial load (confirmed via
      // real debug logs: 0/140 events in a full session) - unlike the
      // traditional bot protocol. Most full guild data (channels,
      // threads, roles...) is bundled by Discord straight into the
      // "guilds" field of the READY payload itself - this is also why
      // the READY payload is ~5MB even with only 49 guilds.
      // buildLightReadyPayload() (GatewayEvents.cpp) was updated to
      // extract this raw "guilds" array (without parsing the full JSON
      // payload, keeping the fast-path optimization) - loop through it
      // here to get each guild's threads AND roles.
      // Fix: role parsing used to live ONLY in the GUILD_CREATE branch
      // below, which (per the comment above) never actually fires on
      // initial load - so m_store's role data for every guild stayed
      // empty for the whole session, which made
      // PermissionUtils::canViewChannel() treat every channel as
      // inaccessible (base permissions computed from zero roles = 0)
      // and the channel list came back empty for every guild. Confirmed
      // via a real BBCord log: "guild channels" REST calls returned 200
      // with data, but the channel list rendered empty - the filtering
      // pass throwing everything away, not the fetch. Parsing roles
      // here, from the data READY actually carries, is the fix.
      //
      // Fix: unlike "roles"/"threads", each guild object in
      // READY.guilds[] does NOT include a "members" field on this
      // gateway - directly confirmed via a debug qDebug() dump of a
      // real guild object's keys (temporarily added, then removed once
      // this was settled). So the current user's OWN roles in a guild
      // can't be derived from READY at all - see
      // fetchSelfGuildMember()/onSelfGuildMemberLoaded()
      // (GuildChannels.cpp) for where that's fetched instead, and the
      // GUILD_MEMBER_UPDATE handling further below in this function for
      // how a role change is picked up in real time afterwards.
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

    // GUILD_CREATE doesn't fire on the user-token gateway during
    // initial load (confirmed: 0/140 events in a full session - see
    // comment in the "READY" block above, that's the real threads
    // source for the normal case). Keeping the threads-reading logic
    // here in case Discord still sends GUILD_CREATE when the user JOINS
    // a new guild while the app is running (the official docs list
    // this as one of three reasons GUILD_CREATE is sent).
    mergeThreadsIntoCache(payload.value("threads").toList(), QVariantList());
  }


  if (eventName == "GUILD_MEMBER_LIST_UPDATE") {
    // Standard Discord payload (not restricted-intent bot-gateway):
    //   guild_id: string
    //   id: string (list id, usually "everyone" for the default list)
    //   ops: [ { op: "SYNC"|"INSERT"|"UPDATE"|"DELETE"|"INVALIDATE",
    //            range: [start,end] (SYNC only),
    //            items: [ {group:{id,count}} | {member:{...}} ] } ]
    // Only handles op "SYNC" (a full snapshot, always the first
    // response after sending guild_subscribe with a channel range —
    // see sendLazyRequest()/buildGuildSubscribePayload() in
    // Gateway.cpp). INSERT/UPDATE/DELETE (live updates while the
    // Members sheet is open) are NOT handled yet — see comment at
    // AppStore::setMemberListForChannel().
    QString guildId = payload.value("guild_id").toString().trimmed();
    // Fix: needed for the self-role fallback further below (see the
    // "opportunistically pick up the CURRENT USER's own role list"
    // comment inside the member-parsing loop).
    QString currentUserId = m_store ? m_store->currentUserId() : QString();
    // The standard op:14/GUILD_MEMBER_LIST_UPDATE payload (an
    // unofficial protocol, not in Discord's official bot docs) has no
    // root-level "channel_id" field - that field only exists for
    // regular channel events (MESSAGE_CREATE, etc). For a
    // single-channel subscribe (channels: {"<id>": [[0,99]]} - see
    // buildMemberListSyncPayload() in JsonParser.cpp), Discord returns
    // that channel id in the root-level "id" field instead. Read
    // "channel_id" first (in case Discord changes format), fall back to
    // "id" if empty - the code used to only read "channel_id" so it
    // always got an empty string and threw away the whole parsed member
    // list.
    QString channelId = payload.value("channel_id").toString().trimmed();
    if (channelId.isEmpty()) {
      channelId = payload.value("id").toString().trimmed();
    }
    // "id" is usually the logical list id ("everyone") rather than a
    // real channel id, so it's not 100% reliable. Since the current
    // flow only tracks one channel's member list at a time, fall back
    // to the channel most recently requested via
    // requestMemberListSync() if both sources above are empty or don't
    // match the channel waiting for data.
    if (channelId.isEmpty() || channelId == "everyone") {
      channelId = m_pendingMemberListChannelId;
    }
    // The FIRST SYNC for a channel always arrives right after that
    // channel gets subscribed (sendLazyRequest() in Gateway.cpp -
    // called when the user OPENS the channel, not when the Members
    // sheet opens). At that point, m_pendingMemberListChannelId is
    // still empty (Members sheet never opened yet), so the fallback
    // above isn't enough - the ACTUAL member list data is present in
    // this SYNC but gets thrown away if the channel can't be
    // identified. Discord won't send another SYNC once the Members
    // sheet later opens and resends the same request (known gateway
    // behavior: a subscribe request identical to an existing
    // subscription gets no response) - so this is the ONLY CHANCE to
    // get the data. Falls back to the channel currently open in the
    // chat view (selectedChannelId) when no other channel can be
    // determined.
    if (channelId.isEmpty() && m_store) {
      channelId = m_store->selectedChannelId();
    }
    qDebug() << "[discord-gateway] GUILD_MEMBER_LIST_UPDATE received guild"
             << guildId << "channel" << channelId << "ops"
             << payload.value("ops").toList().size();

    if (!guildId.isEmpty() && m_store) {
      QVariantList roles = m_store->guildRolesForGuild(guildId);
      // Map roleId -> position, keeping only hoisted roles (only
      // hoisted roles create a group/heading in the Members sheet). The
      // highest-position role a member has determines their display
      // group/name color, matching real Discord client behavior.
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
            // A "group" item (heading role) — the Members sheet
            // computes its own heading from each member's
            // primaryRoleId at the QML level, no need to store a
            // separate "group" item here to avoid duplicating logic
            // between C++ and QML.
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
            // Hardcoded cdn.discordapp.com instead of using
            // DiscordRestClient::cdnBaseUrl() — no RestClient instance
            // conveniently available in this scope, and
            // cdn.discordapp.com is Discord's stable CDN hostname
            // (different from the custom API URL in Settings, which
            // targets an API proxy, not the CDN). Same
            // "%1/%2.png?size=128" format as
            // RestClientRequests.cpp::sendAvatarRequest() for
            // consistency.
            member.avatarUrl = QString("https://cdn.discordapp.com/avatars/"
                                       "%1/%2.png?size=128")
                                    .arg(userId)
                                    .arg(effectiveAvatarHash);
          }

          // "presence" lives INSIDE the "member" object (memberRaw), not
          // a sibling of "member" at the "item" level — reading
          // item.value("presence") by mistake always returns an empty
          // map, causing status to always fall back to "offline" for
          // EVERY member regardless of their real state (confirmed bug:
          // Members sheet showed all 97 members as "Offline" and lost
          // role grouping entirely, since everyone got dumped into
          // offlineGroup in
          // MemberListController::rebuildMemberDataModel()).
          QVariantMap presence = memberRaw.value("presence").toMap();
          member.status = presence.value("status").toString();
          if (member.status.isEmpty()) {
            member.status = "offline";
          }

          QVariantList memberRoleIds = memberRaw.value("roles").toList();
          // Fix: opportunistically pick up the CURRENT USER's own role
          // list whenever they happen to appear in a member-list SYNC
          // (which does include a full "roles" array per member,
          // confirmed working in real logs) - this is a fallback path,
          // separate from fetchSelfGuildMember()/GUILD_MEMBER_UPDATE
          // (see GuildChannels.cpp), for whenever that REST endpoint
          // isn't usable for user tokens. Only updates if this SYNC's
          // roles differ from what's cached, to avoid redundant
          // recomputeAccessibleGuildChannels() calls on every channel
          // open (a SYNC fires each time a channel is opened, not just
          // when roles actually change).
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

        if (!channelId.isEmpty()) {
          m_store->setMemberListForChannel(channelId, parsedMembers);
          qDebug() << "[discord-gateway] GUILD_MEMBER_LIST_UPDATE SYNC parsed"
                   << parsedMembers.size() << "members for channel"
                   << channelId;
        }
      }
    }
  }

  // Fix: GET /guilds/{id}/threads/active AND GET /channels/{id}/threads/
  // active (2 REST endpoints tried previously) are both flatly rejected
  // by Discord for user tokens - actual response confirmed:
  // {"message": "Only bots can use this endpoint.", "code": 20002}.
  // This is a hard limit from Discord, not fixable by changing the
  // request. What the real Discord client actually uses (confirmed via
  // community network traces): gateway op:14 (Lazy Guild Subscribe)
  // with "threads: true" - already enabled in
  // DiscordJsonParser::buildGuildSubscribePayload() (see
  // JsonParser.cpp) - causing Discord to push threads itself via the
  // THREAD_LIST_SYNC dispatch event on every guild subscribe, no
  // manual request needed.
  if (eventName == "THREAD_LIST_SYNC") {
    QVariantList rawThreads = payload.value("threads").toList();
    QVariantList syncedChannelIds = payload.value("channel_ids").toList();
    mergeThreadsIntoCache(rawThreads, syncedChannelIds);
    qDebug() << "[discord-gateway] THREAD_LIST_SYNC received guild"
             << payload.value("guild_id").toString() << "threads"
             << rawThreads.size();
  }

  if (eventName == "GUILD_MEMBER_UPDATE") {
    // Fix: real-time counterpart to fetchSelfGuildMember() (see
    // GuildChannels.cpp) - Discord sends this to every gateway
    // connection a member (or an admin acting on them) has open
    // whenever that member's roles change, INCLUDING when it's the
    // current user's own roles. Without handling it, a role granted
    // while BBCord is sitting on the affected guild wouldn't reveal
    // the newly-unlocked channel until the guild was closed and
    // reselected (which is what triggers the REST fetch). Standard
    // Discord payload: guild_id, user.id, roles (the member's full new
    // role-id array, not a delta).
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
    // Push to BlackBerry Hub regardless of whether that channel is
    // currently open — the user could be viewing a different channel,
    // or the app could be in the background/screen off; the Hub is a
    // notification channel independent of the in-app UI. Not limited
    // by selectedOrLoaded like the log block above.
    MentionNotification notification =
        m_gatewayHandler->buildMentionNotification(payload);
    if (notification.shouldNotify) {
      m_hubIntegration->upsertThreadItem(
          notification.sourceId, notification.title, notification.preview,
          notification.timestampMs);
      // ping.m4a sound for every notify-worthy message (like Zalo) —
      // same shouldNotify condition as the Hub push above, but called
      // independently since playPingSound() doesn't depend on
      // UDS/init() (see HubIntegration.cpp).
      m_hubIntegration->playPingSound();
    }
  }

  m_gatewayHandler->applyGatewayOrderingEvent(
      eventName, payload, m_pendingUnreadGuildIds,
      m_pendingMentionCountsByGuildId, m_pendingMentionCountsByChannelId,
      m_pendingUnreadChannelIds, m_pendingDmUiUpdate, m_gatewayUiUpdateQueued);
}

void DiscordClient::mergeGuildRolesIntoCache(const QString &guildId,
                                             const QVariantMap &guildRaw) {
  if (!m_store) {
    return;
  }

  // "roles": the guild's full array of role objects (id/name/color/
  // position/hoist/permissions/...). Present the same way whether
  // guildRaw came from READY's "guilds" array or a genuine
  // GUILD_CREATE payload - both are "a guild object" per Discord's
  // docs. color == 0 means the role has no custom color — the real
  // Discord client doesn't show black for this case, it uses the
  // default text color, so we leave color empty instead of "#000000".
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
    // "permissions" arrives as a stringified int64 in the Discord
    // payload (values exceed int32 range) - toLongLong() parses the
    // numeric string directly, no manual conversion needed.
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

  // "members": the full array of guild member objects (not a single
  // "member" field for self — that's a restricted-intent bot gateway
  // behavior, NOT applicable to the user-account gateway BBCord uses).
  // Find the entry whose user.id matches ourselves to get our roles -
  // used for the Hub notification feature when pinged via a role, and
  // for PermissionUtils::canViewChannel() (GuildChannels.cpp).
  // Fix: confirmed this "members" field is NEVER present when this
  // function is called from the READY branch above - only real
  // GUILD_CREATE payloads (the join-a-new-guild-while-running case)
  // actually carry it, so this loop is effectively dead code on the
  // READY path and only fires there in practice. The real source of
  // the current user's own roles on initial load is
  // fetchSelfGuildMember() (GuildChannels.cpp) - kept here anyway since
  // it's harmless (empty "members" -> loop just does nothing) and
  // covers the genuine GUILD_CREATE case for free.
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

  // channelIdsToClear lists channels that no longer have ANY active
  // threads (only present for THREAD_LIST_SYNC, empty when called from
  // GUILD_CREATE) - their old keys must be removed before reloading, or
  // the list would keep threads that were already closed/archived.
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
    // Fix: THREAD_LIST_SYNC can fire multiple times for the same guild
    // in one session (once per new channel subscribed in that guild) -
    // append() used to just add without checking for duplicates, so
    // the same thread (same itemId) got added repeatedly on every
    // merge, causing observable duplicates in the UI (confirmed bug via
    // real testing). Remove the old item with the same itemId before
    // appending again - ensures the newest version always wins (thread
    // data can change between syncs, e.g. message count) without
    // duplicating the list.
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
