#ifndef SettingsController_HPP_
#define SettingsController_HPP_

#include <QObject>
#include <QString>

class SettingsController : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool sfxEnabled READ sfxEnabled WRITE setSfxEnabled NOTIFY
                 sfxEnabledChanged)
  Q_PROPERTY(bool compactMessageEnabled READ compactMessageEnabled WRITE
                 setCompactMessageEnabled NOTIFY compactMessageEnabledChanged)
  Q_PROPERTY(QString cachePath READ cachePath CONSTANT)
  Q_PROPERTY(QString cacheUsed READ cacheUsed NOTIFY cacheUsedChanged)
  Q_PROPERTY(QString apiUrl READ apiUrl NOTIFY apiUrlChanged)
  Q_PROPERTY(QString cdnUrl READ cdnUrl NOTIFY cdnUrlChanged)
  Q_PROPERTY(QString officialApiUrl READ officialApiUrl CONSTANT)
  Q_PROPERTY(QString officialCdnUrl READ officialCdnUrl CONSTANT)

public:
  explicit SettingsController(QObject *parent = 0);

  bool sfxEnabled() const;
  bool compactMessageEnabled() const;
  QString cachePath() const;
  QString cacheUsed() const;
  QString apiUrl() const;
  QString cdnUrl() const;
  QString officialApiUrl() const;
  QString officialCdnUrl() const;
  Q_INVOKABLE void setSfxEnabled(bool enabled);
  Q_INVOKABLE void setCompactMessageEnabled(bool enabled);
  Q_INVOKABLE void clearCache();
  Q_INVOKABLE void refreshCacheUsed();
  Q_INVOKABLE bool setApiUrl(const QString &url);
  Q_INVOKABLE bool setCdnUrl(const QString &url);
  Q_INVOKABLE bool resetDiscordBackend();
  Q_INVOKABLE bool guildFolderExpanded(const QString &folderId) const;
  Q_INVOKABLE void setGuildFolderExpanded(const QString &folderId,
                                          bool expanded);
  // Copy file log hiện tại vào thư mục Documents dùng chung
  // (/accounts/1000/shared/documents/BBcord) để người dùng có thể lấy
  // ra bằng file manager, cáp USB, v.v. Trả về đường dẫn file đã lưu,
  // hoặc chuỗi rỗng nếu thất bại (không có log, hoặc copy lỗi).
  Q_INVOKABLE QString exportLog();

Q_SIGNALS:
  void sfxEnabledChanged(bool enabled);
  void compactMessageEnabledChanged(bool enabled);
  void cacheUsedChanged(const QString &cacheUsed);
  void cacheCleared();
  void apiUrlChanged(const QString &apiUrl);
  void cdnUrlChanged(const QString &cdnUrl);

private:
  void ensureDatabase();
  void ensureCacheDirectory() const;
  void clearDirectory(const QString &path) const;
  qint64 directorySize(const QString &path) const;
  QString formatSize(qint64 bytes) const;
  bool readBool(const QString &key, bool defaultValue) const;
  void writeBool(const QString &key, bool value);
  QString normalizedUrl(const QString &url) const;

  QString m_connectionName;
  bool m_sfxEnabled;
  bool m_compactMessageEnabled;
  QString m_cacheUsed;
  QString m_apiUrl;
  QString m_cdnUrl;
};

#endif /* SettingsController_HPP_ */
