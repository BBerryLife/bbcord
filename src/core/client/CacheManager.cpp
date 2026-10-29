#include "CacheManager.hpp"
#include "../AppStore.hpp"
#include "../Client.hpp"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtConcurrentRun>
#include <bb/data/JsonDataAccess>

namespace {

const int kCacheSchemaVersion = 1;

static void writeCacheFileWorker(const QString path, const QVariantMap data) {
  QFileInfo fileInfo(path);
  QDir dir = fileInfo.dir();

  if (!dir.exists() && !dir.mkpath(".")) {
    qDebug() << "[discord-cache] could not create cache dir"
             << dir.absolutePath();
    return;
  }
  bb::data::JsonDataAccess json;
  QByteArray bytes;

  json.saveToBuffer(data, &bytes);

  if (json.hasError()) {
    qDebug() << "[discord-cache] could not serialize cache"
             << json.error().errorMessage();
    return;
  }
  QFile file(path);

  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    qDebug() << "[discord-cache] could not write cache" << path;
    return;
  }

  file.write(bytes);
  file.close();
}

QString presenceColor(const QString &status) {
  if (status == "online") {
    return "#23A55A";
  }

  if (status == "idle") {
    return "#F0B232";
  }

  if (status == "dnd" || status == "busy") {
    return "#F23F43";
  }

  return "#80848E";
}

} // namespace

CacheManager::CacheManager(DiscordClient *client, AppStore *store,
                           QObject *parent)
    : QObject(parent), m_readOnly(false), m_client(client), m_store(store) {}
CacheManager::~CacheManager() {}
void CacheManager::loadBootstrapCache(
    QVariantList &guilds, QVariantList &allDmChannels,
    QVariantMap &avatarSourcesByUserId, QString &selectedGuildId,
    bool &bootstrapCacheLoaded, bool &guildsHasMore, QString &lastGuildId,
    int &visibleDmChannelCount, QString &lastDmChannelId,
    bool &dmChannelsHasMore) {

  if (m_store == 0 || bootstrapCacheLoaded) {
    return;
  }

  QVariantMap guildRoot;

  if (loadCacheFile(guildsCachePath(), &guildRoot)) {
    QVariantList cachedGuilds = guildRoot.value("guilds").toList();
    if (!cachedGuilds.isEmpty()) {
      guilds = cachedGuilds;
      lastGuildId.clear();
      guildsHasMore = true;
      m_store->setGuilds(guilds);
      bootstrapCacheLoaded = true;
    }
  }
  QVariantMap dmRoot;

  if (loadCacheFile(dmChannelsCachePath(), &dmRoot)) {
    QVariantList cachedDms =
        dmChannelsFromCache(dmRoot.value("channels").toList());

    if (!cachedDms.isEmpty()) {
      allDmChannels = cachedDms;
      for (int i = 0; i < allDmChannels.size(); ++i) {
        QVariantMap channel = allDmChannels.at(i).toMap();
        QString userId = channel.value("avatarUserId").toString();
        QString avatar = channel.value("avatar").toString();
        if (!userId.isEmpty() && !avatar.isEmpty())
          avatarSourcesByUserId.insert(userId, avatar);

        QString userId2 = channel.value("avatarUserId2").toString();
        QString avatar2 = channel.value("avatar2").toString();

        if (!userId2.isEmpty() && !avatar2.isEmpty())
          avatarSourcesByUserId.insert(userId2, avatar2);
      }

      visibleDmChannelCount = 0;
      lastDmChannelId.clear();
      dmChannelsHasMore = true;
      bootstrapCacheLoaded = true;
    }
  }
  QString cachedSelectedGuildId = guildRoot.value("selectedGuildId").toString();
  QString selectedChannelId = guildRoot.value("selectedChannelId").toString();

  if (cachedSelectedGuildId.isEmpty())
    cachedSelectedGuildId = dmRoot.value("selectedGuildId").toString();

  if (selectedChannelId.isEmpty())
    selectedChannelId = dmRoot.value("selectedChannelId").toString();
  if (!cachedSelectedGuildId.isEmpty()) {
    m_store->selectGuild(cachedSelectedGuildId);
    selectedGuildId = cachedSelectedGuildId;
  }

  if (!selectedChannelId.isEmpty()) {
    m_store->selectChannel(selectedChannelId);
  }
}

void CacheManager::saveGuildsCache(const QVariantList &guilds) const {
  if (m_readOnly) {
    return;
  }
  QVariantMap root;
  root["version"] = kCacheSchemaVersion;
  root["guilds"] = guilds;
  root["selectedGuildId"] = m_store ? m_store->selectedGuildId() : QString();
  root["selectedChannelId"] =
      m_store ? m_store->selectedChannelId() : QString();
  saveCacheFileAsync(guildsCachePath(), root);
}

void CacheManager::saveDmChannelsCache(
    const QVariantList &allDmChannels) const {
  if (m_readOnly) {
    return;
  }
  QVariantMap root;
  root["version"] = kCacheSchemaVersion;
  root["channels"] = dmChannelsForCache(allDmChannels);
  root["selectedGuildId"] = m_store ? m_store->selectedGuildId() : QString();
  root["selectedChannelId"] =
      m_store ? m_store->selectedChannelId() : QString();
  saveCacheFileAsync(dmChannelsCachePath(), root);
}

void CacheManager::clearBootstrapCache() const {
  if (m_readOnly) {
    return;
  }
  QFile::remove(guildsCachePath());
  QFile::remove(dmChannelsCachePath());
}

QString CacheManager::guildsCachePath() const {
  QDir dir(QDir::homePath());
  dir.mkpath("data");
  return dir.absoluteFilePath("data/guilds_cache.json");
}

QString CacheManager::dmChannelsCachePath() const {
  QDir dir(QDir::homePath());
  dir.mkpath("data");
  return dir.absoluteFilePath("data/dms_cache.json");
}

bool CacheManager::loadCacheFile(const QString &path, QVariantMap *root) const {
  if (root == 0)
    return false;
  QFile file(path);

  if (!file.exists() || !file.open(QIODevice::ReadOnly))
    return false;

  QByteArray bytes = file.readAll();
  file.close();
  bb::data::JsonDataAccess json;
  QVariant parsed = json.loadFromBuffer(bytes);

  if (json.hasError()) {
    qDebug() << "[discord-cache] ignoring invalid cache" << path
             << json.error().errorMessage();
    return false;
  }
  QVariantMap parsedRoot = parsed.toMap();

  if (parsedRoot.value("version").toInt() != kCacheSchemaVersion) {
    qDebug() << "[discord-cache] ignoring unsupported cache version" << path;
    return false;
  }

  *root = parsedRoot;
  return true;
}

void CacheManager::saveCacheFileAsync(const QString &path,
                                      const QVariantMap &root) const {
  QString pathCopy = path;
  QVariantMap rootCopy = root;
  QtConcurrent::run(writeCacheFileWorker, pathCopy, rootCopy);
}

QVariantList
CacheManager::dmChannelsForCache(const QVariantList &allDmChannels) const {
  QVariantList cached;
  for (int i = 0; i < allDmChannels.size(); ++i) {
    QVariantMap item = allDmChannels.at(i).toMap();
    item["status"] = "offline";
    item["statusColor"] = presenceColor("offline");
    cached.append(item);
  }
  return cached;
}

QVariantList
CacheManager::dmChannelsFromCache(const QVariantList &channels) const {
  QVariantList cached;
  for (int i = 0; i < channels.size(); ++i) {
    QVariantMap item = channels.at(i).toMap();
    if (item.value("id").toString().isEmpty())
      continue;

    item["status"] = "offline";
    item["statusColor"] = presenceColor("offline");
    cached.append(item);
  }
  return cached;
}
