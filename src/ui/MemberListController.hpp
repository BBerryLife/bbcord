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

// Controller đứng sau ChannelMemberList.qml. Không tự parse dữ liệu gì —
// chỉ đọc kết quả đã được DiscordClient::onGatewayDispatch() (Client.cpp)
// parse sẵn từ GUILD_MEMBER_LIST_UPDATE và lưu trong AppStore, rồi build
// thành 1 ArrayDataModel phẳng (role heading + member) cho ListView.
//
// Lazy-load: requestMemberList() PHẢI được gọi tường minh từ QML khi
// sheet Members được mở (không tự động subscribe khi mở channel như
// tin nhắn) — đây là tối ưu băng thông/CPU đã được yêu cầu, tránh tải
// avatar/role cho mọi channel mà người dùng có thể không bao giờ xem
// danh sách member.
//
// Avatar cache: dùng CHUNG cơ chế AttachmentImageCacheWorker (generic
// url->file cache) mà ChatController dùng cho ảnh đính kèm — KHÔNG dùng
// AvatarManager, vì AvatarManager gắn chặt với queue 2-slot dùng chung
// cho DM/current-user avatar (xem Client.hpp: m_loadingAvatarUserId,
// m_loadingAvatarUserId2, m_pendingAvatars). Nếu member sheet dùng chung
// queue đó, avatar của hàng chục member trong 1 guild lớn có thể làm
// nghẽn/trễ avatar DM đang cần load song song. Cache riêng thư mục
// "member-avatar-cache" để không đụng cache "chat-image-cache" của
// ChatController.
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

  // Gọi khi sheet Members được mở. Gửi guild-subscribe request qua
  // Gateway (nếu chưa gửi cho channel này) và bắt đầu lắng nghe
  // AppStore::memberListChanged() để build lại model khi SYNC về.
  Q_INVOKABLE void requestMemberList(const QString &channelId,
                                     const QString &guildId);

  // Gọi khi sheet Members đóng — dừng lắng nghe để tránh build lại model
  // không cần thiết cho 1 channel không còn hiển thị trên màn hình, và
  // hủy các avatar còn đang tải dở (danh sách đã đóng thì không cần nữa).
  Q_INVOKABLE void releaseMemberList();

  // Gọi từ QML (ListView.onCreationCompleted hoặc tương tự) khi 1 hàng
  // member THỰC SỰ hiển thị trên màn hình — đây là điểm "chỉ tải khi mở
  // tab member" mà yêu cầu ban đầu nhắc tới, áp dụng ở cấp độ TỪNG DÒNG
  // để tránh tải avatar của member nằm ngoài viewport (danh sách dài,
  // cuộn xuống mới cần). Trả về source ảnh cache sẵn có ngay (đồng bộ)
  // nếu đã tải trước đó, hoặc chuỗi rỗng nếu cần tải — trong trường hợp
  // rỗng, kết quả sẽ về sau qua signal avatarCached().
  Q_INVOKABLE QString cachedAvatarSource(const QString &avatarUrl);

private Q_SLOTS:
  void onMemberListChanged(const QString &channelId);
  void onGuildRolesChanged(const QString &guildId);
  void onAvatarImageCached(const QString &url, const QString &path);
  void onAvatarImageFailed(const QString &url);

Q_SIGNALS:
  void isLoadingChanged();
  // Bắn khi 1 avatar tải xong — QML nên lắng nghe signal này để cập nhật
  // lại hàng tương ứng trong ListView (ArrayDataModel không tự re-render
  // khi mutate 1 QVariantMap đã append, phải replace() lại phần tử).
  void avatarCached(const QString &avatarUrl, const QString &imageSource);

private:
  void rebuildMemberDataModel();
  QString avatarCachePath(const QString &avatarUrl) const;
  QString filePreviewSource(const QString &filePath) const;
  void ensureAvatarImageWorker();

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
