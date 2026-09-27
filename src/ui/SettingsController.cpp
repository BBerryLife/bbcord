#include "SettingsController.hpp"

#include "../utils/Logger.hpp"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QUrl>
#include <QVariant>

namespace {
const char *kSfxEnabledKey = "sfx_enabled";
const char *kCompactMessageEnabledKey = "compact_message_enabled";
const char *kCacheDirName = "cache";
const char *kApiUrlKey = "discord/apiUrl";
const char *kCdnUrlKey = "discord/cdnUrl";
const char *kOfficialApiUrl = "https://discord.com/api/v9/";
const char *kOfficialCdnUrl = "https://cdn.discordapp.com/";
} // namespace

SettingsController::SettingsController(QObject *parent)
    : QObject(parent), m_connectionName("BBCordSettings"), m_sfxEnabled(false),
      m_compactMessageEnabled(false), m_cacheUsed("0 B") {
  ensureDatabase();
  m_sfxEnabled = readBool(kSfxEnabledKey, false);
  m_compactMessageEnabled = readBool(kCompactMessageEnabledKey, false);
  QSettings settings;
  m_apiUrl =
      normalizedUrl(settings.value(kApiUrlKey, kOfficialApiUrl).toString());
  m_cdnUrl =
      normalizedUrl(settings.value(kCdnUrlKey, kOfficialCdnUrl).toString());
  refreshCacheUsed();
}

bool SettingsController::sfxEnabled() const { return m_sfxEnabled; }

bool SettingsController::compactMessageEnabled() const {
  return m_compactMessageEnabled;
}

QString SettingsController::cachePath() const {
  QDir dir(QDir::homePath());
  return dir.absoluteFilePath(kCacheDirName);
}

QString SettingsController::cacheUsed() const { return m_cacheUsed; }

QString SettingsController::apiUrl() const { return m_apiUrl; }

QString SettingsController::cdnUrl() const { return m_cdnUrl; }

QString SettingsController::officialApiUrl() const { return kOfficialApiUrl; }

QString SettingsController::officialCdnUrl() const { return kOfficialCdnUrl; }

void SettingsController::setSfxEnabled(bool enabled) {
  if (m_sfxEnabled == enabled) {
    return;
  }

  m_sfxEnabled = enabled;
  writeBool(kSfxEnabledKey, enabled);
  emit sfxEnabledChanged(m_sfxEnabled);
}

void SettingsController::setCompactMessageEnabled(bool enabled) {
  if (m_compactMessageEnabled == enabled) {
    return;
  }

  m_compactMessageEnabled = enabled;
  writeBool(kCompactMessageEnabledKey, enabled);
  emit compactMessageEnabledChanged(m_compactMessageEnabled);
}

void SettingsController::clearCache() {
  clearDirectory(cachePath());
  refreshCacheUsed();
  emit cacheCleared();
}

void SettingsController::refreshCacheUsed() {
  ensureCacheDirectory();
  QString used = formatSize(directorySize(cachePath()));
  if (m_cacheUsed == used) {
    return;
  }

  m_cacheUsed = used;
  emit cacheUsedChanged(m_cacheUsed);
}

bool SettingsController::setApiUrl(const QString &url) {
  QString normalized = normalizedUrl(url);
  if (normalized.isEmpty()) {
    return false;
  }

  if (m_apiUrl == normalized) {
    return true;
  }

  m_apiUrl = normalized;
  QSettings settings;
  settings.setValue(kApiUrlKey, m_apiUrl);
  settings.sync();
  emit apiUrlChanged(m_apiUrl);
  return true;
}

bool SettingsController::setCdnUrl(const QString &url) {
  QString normalized = normalizedUrl(url);
  if (normalized.isEmpty()) {
    return false;
  }

  if (m_cdnUrl == normalized) {
    return true;
  }

  m_cdnUrl = normalized;
  QSettings settings;
  settings.setValue(kCdnUrlKey, m_cdnUrl);
  settings.sync();
  emit cdnUrlChanged(m_cdnUrl);
  return true;
}

bool SettingsController::resetDiscordBackend() {
  bool apiChanged = setApiUrl(kOfficialApiUrl);
  bool cdnChanged = setCdnUrl(kOfficialCdnUrl);
  return apiChanged && cdnChanged;
}

QString SettingsController::exportLog() {
  QString logPath = Logger::logFilePath();
  QFileInfo logInfo(logPath);
  if (!logInfo.exists() || logInfo.size() == 0) {
    qWarning() << "[settings] no log file to export at" << logPath;
    return QString();
  }

  QString destFileName =
      QString("bbcord-log-%1.log")
          .arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));

  // Fix: /accounts/1000/shared/documents is only reachable once the
  // user has actually granted the "Shared Files" permission via
  // Settings > App Permissions on-device - the <permission
  // access_shared> entry in bar-descriptor.xml only REQUESTS that
  // permission at install time, it doesn't grant it, and a sideloaded
  // dev-mode install commonly needs it granted (or re-granted)
  // manually. Deleting and letting the app recreate destDir doesn't
  // help when the failure is a permission the app was never given
  // rather than a stale/broken directory - mkpath() fails identically
  // either way. Falling back to the app's own sandbox (QDir::homePath,
  // already proven writable elsewhere - see
  // ChatController::attachmentImageCachePath()'s use of it for the
  // image cache) means exportLog() still produces a usable file
  // instead of silently returning empty, and the returned path itself
  // tells the caller which case happened.
  QDir sharedDir("/accounts/1000/shared/documents/bbcord");
  if (sharedDir.exists() || sharedDir.mkpath(".")) {
    QString sharedPath = sharedDir.absoluteFilePath(destFileName);
    if (QFile::exists(sharedPath)) {
      QFile::remove(sharedPath);
    }
    if (QFile::copy(logPath, sharedPath)) {
      return sharedPath;
    }
    qWarning() << "[settings] failed to copy log to" << sharedPath
               << "- falling back to app sandbox";
  } else {
    qWarning() << "[settings] failed to create export directory"
               << sharedDir.absolutePath()
               << "(likely missing Shared Files permission - check"
               << "Settings > App Permissions on-device) - falling back"
               << "to app sandbox";
  }

  QDir sandboxDir(QDir::homePath() + "/data/exported-logs");
  if (!sandboxDir.exists() && !sandboxDir.mkpath(".")) {
    qWarning() << "[settings] failed to create fallback export directory"
               << sandboxDir.absolutePath();
    return QString();
  }

  QString sandboxPath = sandboxDir.absoluteFilePath(destFileName);
  if (QFile::exists(sandboxPath)) {
    QFile::remove(sandboxPath);
  }
  if (!QFile::copy(logPath, sandboxPath)) {
    qWarning() << "[settings] failed to copy log to" << sandboxPath;
    return QString();
  }
  return sandboxPath;
}

bool SettingsController::guildFolderExpanded(const QString &folderId) const {
  QString safeFolderId = folderId.trimmed();
  if (safeFolderId.isEmpty()) {
    return false;
  }

  return readBool(QString("guild_folder/%1/expanded").arg(safeFolderId), false);
}

void SettingsController::setGuildFolderExpanded(const QString &folderId,
                                                bool expanded) {
  QString safeFolderId = folderId.trimmed();
  if (safeFolderId.isEmpty()) {
    return;
  }

  writeBool(QString("guild_folder/%1/expanded").arg(safeFolderId), expanded);
}

void SettingsController::ensureDatabase() {
  QSqlDatabase db;
  if (QSqlDatabase::contains(m_connectionName)) {
    db = QSqlDatabase::database(m_connectionName);
  } else {
    db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
    QDir dataDir(QDir::homePath());
    if (!dataDir.exists("data")) {
      dataDir.mkpath("data");
    }
    db.setDatabaseName(dataDir.absoluteFilePath("data/settings.db"));
  }

  if (!db.isOpen() && !db.open()) {
    qWarning() << "[settings] failed to open database" << db.lastError().text();
    return;
  }

  QSqlQuery query(db);
  if (!query.exec("CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, "
                  "value INTEGER NOT NULL)")) {
    qWarning() << "[settings] failed to create table"
               << query.lastError().text();
  }
}

void SettingsController::ensureCacheDirectory() const {
  QDir dir(QDir::homePath());
  if (!dir.exists(kCacheDirName)) {
    dir.mkpath(kCacheDirName);
  }
}

void SettingsController::clearDirectory(const QString &path) const {
  QDir dir(path);
  if (!dir.exists()) {
    return;
  }

  QFileInfoList entries = dir.entryInfoList(
      QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System);
  foreach (const QFileInfo &entry, entries) {
    if (entry.isDir()) {
      clearDirectory(entry.absoluteFilePath());
      QDir().rmdir(entry.absoluteFilePath());
    } else {
      QFile::remove(entry.absoluteFilePath());
    }
  }
}

qint64 SettingsController::directorySize(const QString &path) const {
  qint64 total = 0;
  QDir dir(path);
  QFileInfoList entries = dir.entryInfoList(
      QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System);
  foreach (const QFileInfo &entry, entries) {
    if (entry.isDir()) {
      total += directorySize(entry.absoluteFilePath());
    } else {
      total += entry.size();
    }
  }
  return total;
}

QString SettingsController::formatSize(qint64 bytes) const {
  if (bytes < 1024) {
    return tr("%1 B").arg(bytes);
  }

  double value = bytes;
  QStringList units;
  units << "B" << "KB" << "MB" << "GB";
  int unitIndex = 0;
  while (value >= 1024.0 && unitIndex < units.size() - 1) {
    value /= 1024.0;
    ++unitIndex;
  }

  return QString("%1 %2").arg(value, 0, 'f', 1).arg(units.at(unitIndex));
}

bool SettingsController::readBool(const QString &key, bool defaultValue) const {
  QSqlDatabase db = QSqlDatabase::database(m_connectionName);
  if (!db.isOpen()) {
    return defaultValue;
  }

  QSqlQuery query(db);
  query.prepare("SELECT value FROM settings WHERE key = ?");
  query.addBindValue(key);
  if (!query.exec()) {
    qWarning() << "[settings] failed to read" << key
               << query.lastError().text();
    return defaultValue;
  }

  if (!query.next()) {
    return defaultValue;
  }

  return query.value(0).toInt() != 0;
}

void SettingsController::writeBool(const QString &key, bool value) {
  QSqlDatabase db = QSqlDatabase::database(m_connectionName);
  if (!db.isOpen()) {
    return;
  }

  QSqlQuery query(db);
  query.prepare("INSERT OR REPLACE INTO settings (key, value) VALUES (?, ?)");
  query.addBindValue(key);
  query.addBindValue(value ? 1 : 0);
  if (!query.exec()) {
    qWarning() << "[settings] failed to write" << key
               << query.lastError().text();
  }
}

QString SettingsController::normalizedUrl(const QString &url) const {
  QString value = url.trimmed();
  if (value.isEmpty()) {
    return QString();
  }

  QUrl parsed(value);
  if (!parsed.isValid() || parsed.scheme().isEmpty() ||
      parsed.host().isEmpty()) {
    return QString();
  }

  QString path = parsed.path();
  if (!path.endsWith('/')) {
    path += '/';
  }
  parsed.setPath(path);
  parsed.setEncodedQuery(QByteArray());
  parsed.setFragment(QString());
  return parsed.toString();
}
