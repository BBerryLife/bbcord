#ifndef MemberListController_HPP_
#define MemberListController_HPP_

#include <QObject>
#include <QSet>
#include <QString>
#include <QVariantList>

#include <bb/cascades/ArrayDataModel>
#include <bb/cascades/DataModel>

class AppStore;
class AttachmentImageCacheWorker;
class DiscordClient;
class QThread;

// Controller behind ChannelMemberList.qml. Reads member data parsed by
// DiscordClient::onGatewayDispatch() into AppStore and builds a flat
// ArrayDataModel (role heading + member).
// requestMemberList() must be called from QML when the sheet opens (no
// auto-subscribe, to save bandwidth/CPU).
// Avatars use the generic AttachmentImageCacheWorker (not AvatarManager, whose
// 2-slot queue is shared with DM/current-user avatars and could stall) with its
// own "member-avatar-cache" folder.
class MemberListController : public QObject {
  Q_OBJECT
  Q_PROPERTY(bb::cascades::DataModel *memberDataModel READ memberDataModel
                 CONSTANT)
  Q_PROPERTY(bool isLoading READ isLoading NOTIFY isLoadingChanged)

public:
  explicit MemberListController(DiscordClient *client, AppStore *store,
                                QObject *parent = 0);
  ~MemberListController();

  bb::cascades::DataModel *memberDataModel() const;
  bool isLoading() const;

  // Called when the Members sheet opens. Sends a guild-subscribe (if not already sent
  // for this channel) and listens to AppStore::memberListChanged() to rebuild the model.
  Q_INVOKABLE void requestMemberList(const QString &channelId,
                                     const QString &guildId);

  // Called when the sheet closes: stops listening and cancels avatars still loading.
  Q_INVOKABLE void releaseMemberList();

  // Called from QML when a member row is actually shown (per-row lazy loading).
  // Returns the cached image source, or "" if it needs loading (result arrives via
  // avatarCached()).
  Q_INVOKABLE QString cachedAvatarSource(const QString &avatarUrl);

private Q_SLOTS:
  void onMemberListChanged(const QString &channelId);
  void onGuildRolesChanged(const QString &guildId);
  void onAvatarImageCached(const QString &url, const QString &path);
  void onAvatarImageFailed(const QString &url);

Q_SIGNALS:
  void isLoadingChanged();
  // Fired when an avatar finishes loading; QML should refresh the matching row
  // (ArrayDataModel needs replace() to re-render).
  void avatarCached(const QString &avatarUrl, const QString &imageSource);

private:
  void rebuildMemberDataModel();
  QString avatarCachePath(const QString &avatarUrl) const;
  QString filePreviewSource(const QString &filePath) const;
  void ensureAvatarImageWorker();
  // Maps a raw status ("online"/"idle"/"dnd"/""/"offline") to a translated label
  // (e.g. "dnd" -> "Do Not Disturb"). A method, not a free function, because it needs tr().
  QString displayLabelForStatus(const QString &status) const;

  DiscordClient *m_client;
  AppStore *m_store;
  bb::cascades::ArrayDataModel *m_memberDataModel;
  QString m_channelId;
  QString m_guildId;
  bool m_isLoading;
  QThread *m_avatarThread;
  AttachmentImageCacheWorker *m_avatarWorker;
  QSet<QString> m_loadingAvatarUrls;
};

#endif /* MemberListController_HPP_ */
