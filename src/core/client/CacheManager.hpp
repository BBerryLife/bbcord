#ifndef CACHEMANAGER_HPP_
#define CACHEMANAGER_HPP_

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class AppStore;
class DiscordClient;
class CacheManager : public QObject {
  Q_OBJECT
public:
  explicit CacheManager(DiscordClient *client, AppStore *store,
                        QObject *parent = 0);

  virtual ~CacheManager();
  // The headless service must not write the bootstrap caches the UI reads at startup.
  void setReadOnly(bool readOnly) { m_readOnly = readOnly; }
  void loadBootstrapCache(QVariantList &guilds, QVariantList &allDmChannels,
                          QVariantMap &avatarSourcesByUserId,
                          QString &selectedGuildId, bool &bootstrapCacheLoaded,
                          bool &guildsHasMore, QString &lastGuildId,
                          int &visibleDmChannelCount, QString &lastDmChannelId,
                          bool &dmChannelsHasMore);
  void saveGuildsCache(const QVariantList &guilds) const;
  void saveDmChannelsCache(const QVariantList &allDmChannels) const;
  void clearBootstrapCache() const;

private:
  QString guildsCachePath() const;
  QString dmChannelsCachePath() const;
  bool loadCacheFile(const QString &path, QVariantMap *root) const;
  void saveCacheFileAsync(const QString &path, const QVariantMap &root) const;
  QVariantList dmChannelsForCache(const QVariantList &allDmChannels) const;
  QVariantList dmChannelsFromCache(const QVariantList &channels) const;
  bool m_readOnly;
  DiscordClient *m_client;
  AppStore *m_store;
};

#endif /* CACHEMANAGER_HPP_ */
