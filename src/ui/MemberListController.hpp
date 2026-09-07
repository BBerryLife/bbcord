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

// Controller behind ChannelMemberList.qml. Doesn't parse data itself —
// only reads results already parsed by DiscordClient::onGatewayDispatch()
// (Client.cpp) from GUILD_MEMBER_LIST_UPDATE and stored in AppStore,
// then builds a flat ArrayDataModel (role heading + member) for the
// ListView.
//
// Lazy-load: requestMemberList() MUST be called explicitly from QML
// when the Members sheet is opened (it doesn't auto-subscribe on
// channel open like messages do) — this is a deliberate bandwidth/CPU
// optimization, avoiding loading avatars/roles for every channel the
// user might never open the member list for.
//
// Avatar cache: shares the SAME AttachmentImageCacheWorker mechanism
// (generic url->file cache) that ChatController uses for attachments —
// NOT AvatarManager, since AvatarManager is tightly coupled to a
// 2-slot queue shared with DM/current-user avatars (see Client.hpp:
// m_loadingAvatarUserId, m_loadingAvatarUserId2, m_pendingAvatars). If
// the member sheet shared that queue, avatars for dozens of members in
// a large guild could stall/delay DM avatars needing to load at the
// same time. Uses its own "member-avatar-cache" folder to stay
// separate from ChatController's "chat-image-cache".
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

  // Called when the Members sheet is opened. Sends a guild-subscribe
  // request through Gateway (if not already sent for this channel) and
  // starts listening to AppStore::memberListChanged() to rebuild the
  // model once SYNC comes back.
  Q_INVOKABLE void requestMemberList(const QString &channelId,
                                     const QString &guildId);

  // Called when the Members sheet closes — stops listening to avoid
  // unnecessary model rebuilds for a channel no longer on screen, and
  // cancels any avatars still loading (no longer needed once the list
  // is closed).
  Q_INVOKABLE void releaseMemberList();

  // Called from QML (ListView.onCreationCompleted or similar) when a
  // member row is ACTUALLY shown on screen — this is the "only load
  // when the member tab is open" behavior applied at the PER-ROW level,
  // avoiding loading avatars for members outside the viewport (long
  // list, only loads as the user scrolls down to them). Returns the
  // already-cached image source synchronously if previously loaded, or
  // an empty string if it needs loading — in the empty case, the result
  // arrives later via the avatarCached() signal.
  Q_INVOKABLE QString cachedAvatarSource(const QString &avatarUrl);

private Q_SLOTS:
  void onMemberListChanged(const QString &channelId);
  void onGuildRolesChanged(const QString &guildId);
  void onAvatarImageCached(const QString &url, const QString &path);
  void onAvatarImageFailed(const QString &url);

Q_SIGNALS:
  void isLoadingChanged();
  // Fired when an avatar finishes loading — QML should listen to this
  // to refresh the matching row in the ListView (ArrayDataModel doesn't
  // auto re-render when mutating an already-appended QVariantMap,
  // needs a replace() instead).
  void avatarCached(const QString &avatarUrl, const QString &imageSource);

private:
  void rebuildMemberDataModel();
  QString avatarCachePath(const QString &avatarUrl) const;
  QString filePreviewSource(const QString &filePath) const;
  void ensureAvatarImageWorker();
  // Maps Discord's raw status string ("online"/"idle"/"dnd"/"" or
  // "offline") to a full, translated label, shown under a member's name
  // in the Members sheet (e.g. "dnd" -> "Do Not Disturb", matching how
  // the official Discord client displays it). A method instead of a
  // free function in an anonymous namespace since it needs tr() (only
  // available on a QObject-derived class).
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
