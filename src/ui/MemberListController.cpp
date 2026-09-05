#include "MemberListController.hpp"

#include "../core/AppStore.hpp"
#include "../core/AttachmentImageCacheWorker.hpp"
#include "../core/Client.hpp"

#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QMetaObject>
#include <QPair>
#include <QThread>
#include <QUrl>
#include <QVector>
#include <algorithm>

namespace {

struct RoleGroup {
  QString id;
  QString name;
  QString color;
  int position;
  QVariantList members;

  RoleGroup() : position(0) {}
};

bool roleGroupPositionGreater(const RoleGroup &a, const RoleGroup &b) {
  return a.position > b.position;
}

bool memberNameLess(const QVariant &a, const QVariant &b) {
  QString nameA = a.toMap().value("displayName").toString();
  QString nameB = b.toMap().value("displayName").toString();
  return nameA.compare(nameB, Qt::CaseInsensitive) < 0;
}

// "online" hiển thị icon 1 màu xanh trong sheet Members của Discord thật,
// "idle"/"dnd" cũng được coi là đang hoạt động cho mục đích phân nhóm
// "Online" (chỉ tách riêng "offline"/rỗng vào nhóm "Offline").
bool isOnlineStatus(const QString &status) {
  return status == "online" || status == "idle" || status == "dnd";
}

// Bảng màu fallback khi member chưa có avatar cache (đang tải hoặc tải
// lỗi) — cùng họ màu "blurple" mà Discord dùng cho avatar mặc định, để
// nhất quán thẩm mỹ với phần còn lại của app thay vì 1 màu tím cứng cho
// mọi người như bản mock cũ.
const char *kFallbackAvatarColors[] = {
    "#5865F2", "#EB459E", "#57F287", "#FEE75C",
    "#ED4245", "#3BA55D", "#9B84EE", "#00A8FC",
};
const int kFallbackAvatarColorCount = 8;

// Chọn màu fallback ổn định theo userId (cùng 1 user luôn ra cùng 1 màu
// giữa các lần mở sheet, giống Discord thật gán màu avatar mặc định theo
// hash discriminator/id chứ không random mỗi lần render).
QString fallbackAvatarColorForUserId(const QString &userId) {
  if (userId.isEmpty()) {
    return QString::fromLatin1(kFallbackAvatarColors[0]);
  }
  QByteArray hash =
      QCryptographicHash::hash(userId.toUtf8(), QCryptographicHash::Md5);
  quint8 firstByte = static_cast<quint8>(hash.at(0));
  int index = firstByte % kFallbackAvatarColorCount;
  return QString::fromLatin1(kFallbackAvatarColors[index]);
}

} // namespace

MemberListController::MemberListController(DiscordClient *client,
                                            AppStore *store, QObject *parent)
    : QObject(parent), m_client(client), m_store(store),
      m_memberDataModel(new bb::cascades::ArrayDataModel(this)),
      m_isLoading(false), m_avatarThread(0), m_avatarWorker(0) {
  if (m_store) {
    connect(m_store, SIGNAL(memberListChanged(QString)), this,
            SLOT(onMemberListChanged(QString)));
    connect(m_store, SIGNAL(guildRolesChanged(QString)), this,
            SLOT(onGuildRolesChanged(QString)));
  }
}

MemberListController::~MemberListController() {
  if (m_avatarWorker != 0) {
    QMetaObject::invokeMethod(m_avatarWorker, "cancelAll",
                              Qt::QueuedConnection);
  }
  if (m_avatarThread != 0) {
    m_avatarThread->quit();
    m_avatarThread->wait(2000);
  }
}

bb::cascades::DataModel *MemberListController::memberDataModel() const {
  return m_memberDataModel;
}

bool MemberListController::isLoading() const { return m_isLoading; }

void MemberListController::requestMemberList(const QString &channelId,
                                             const QString &guildId) {
  QString safeChannelId = channelId.trimmed();
  QString safeGuildId = guildId.trimmed();
  if (safeChannelId.isEmpty()) {
    return;
  }

  m_channelId = safeChannelId;
  m_guildId = safeGuildId;

  bool hadCachedList =
      m_store && !m_store->memberListForChannel(safeChannelId).isEmpty();
  if (hadCachedList) {
    // Sheet mở lại cho channel đã từng load — hiện luôn dữ liệu cũ ngay
    // lập tức thay vì màn hình trống trong lúc chờ SYNC mới (nếu guild
    // subscribe request đã gửi trước đó, Discord có thể không gửi lại
    // SYNC nếu state phía server không đổi).
    rebuildMemberDataModel();
  } else {
    m_memberDataModel->clear();
  }

  if (!m_isLoading) {
    m_isLoading = true;
    emit isLoadingChanged();
  }

  // DM channel không có guild_id — sheet Members không áp dụng cho DM
  // (ChatCard.qml hiện chỉ cho mở sheet Members từ context guild), nhưng
  // guard ở đây để không gửi guild-subscribe rỗng nếu lỡ gọi từ DM.
  // Dùng requestMemberListSync() (KHÔNG dùng subscribeToGuildChannel())
  // vì channel gần như luôn đã được subscribeToGuildChannel() "tiêu" mất
  // request lúc user mở channel để lazy-load tin nhắn — gọi lại
  // subscribeToGuildChannel() ở đây sẽ bị dedup cache trong
  // DiscordGateway::sendLazyRequest() chặn im lặng, không có SYNC mới
  // nào trả về, khiến sheet Members hiện trống dù channel đã mở trước
  // đó (bug đã xác nhận qua log thực tế). requestMemberListSync() gọi
  // DiscordGateway::sendMemberListSync() — cùng payload op:14 nhưng
  // KHÔNG qua dedup, luôn gửi request mới.
  qDebug() << "[member-list] requestMemberList channel" << safeChannelId
           << "guild" << safeGuildId << "hadCachedList" << hadCachedList;
  if (!safeGuildId.isEmpty() && m_client) {
    m_client->requestMemberListSync(safeChannelId, safeGuildId);
  }
}

void MemberListController::releaseMemberList() {
  m_channelId.clear();
  m_guildId.clear();
  if (m_isLoading) {
    m_isLoading = false;
    emit isLoadingChanged();
  }
  if (m_avatarWorker != 0) {
    QMetaObject::invokeMethod(m_avatarWorker, "cancelAll",
                              Qt::QueuedConnection);
  }
  m_loadingAvatarUrls.clear();

  // Báo cho DiscordGateway biết sheet Members đã đóng, để nó dừng tự
  // động gửi lại SYNC cho channel này nếu gateway reconnect sau khi user
  // đã rời trang (xem Gateway.hpp: m_activeMemberListGuildId).
  if (m_client) {
    m_client->clearMemberListSync();
  }
}

QString MemberListController::cachedAvatarSource(const QString &avatarUrl) {
  QString safeUrl = avatarUrl.trimmed();
  if (safeUrl.isEmpty()) {
    return QString();
  }

  QString path = avatarCachePath(safeUrl);
  QFileInfo cachedFile(path);
  if (cachedFile.exists() && cachedFile.size() > 0) {
    return filePreviewSource(path);
  }

  if (m_loadingAvatarUrls.contains(safeUrl)) {
    return QString();
  }

  ensureAvatarImageWorker();
  if (m_avatarWorker == 0) {
    return QString();
  }

  m_loadingAvatarUrls.insert(safeUrl);
  QMetaObject::invokeMethod(m_avatarWorker, "requestImage",
                            Qt::QueuedConnection, Q_ARG(QString, safeUrl),
                            Q_ARG(QString, path), Q_ARG(qint64, 0));
  return QString();
}

void MemberListController::onMemberListChanged(const QString &channelId) {
  if (channelId != m_channelId) {
    return;
  }
  if (m_isLoading) {
    m_isLoading = false;
    emit isLoadingChanged();
  }
  rebuildMemberDataModel();
}

void MemberListController::onGuildRolesChanged(const QString &guildId) {
  if (guildId != m_guildId) {
    return;
  }
  // Role vừa cập nhật (vd: đổi màu role, đổi tên role) — build lại để
  // heading/màu tên member phản ánh đúng, dùng lại member list đã có
  // sẵn (không cần chờ SYNC mới).
  rebuildMemberDataModel();
}

void MemberListController::onAvatarImageCached(const QString &url,
                                               const QString &path) {
  m_loadingAvatarUrls.remove(url);
  emit avatarCached(url, filePreviewSource(path));
}

void MemberListController::onAvatarImageFailed(const QString &url) {
  m_loadingAvatarUrls.remove(url);
  // Không emit avatarCached() khi lỗi — QML giữ nguyên avatarColor
  // fallback đã có sẵn trong row, không cần signal riêng cho failure vì
  // UI không có gì phải đổi (đã hiển thị fallback từ đầu).
}

QString
MemberListController::avatarCachePath(const QString &avatarUrl) const {
  QByteArray hash = QCryptographicHash::hash(avatarUrl.toUtf8(),
                                             QCryptographicHash::Sha1)
                        .toHex();
  QString suffix = QFileInfo(QUrl(avatarUrl).path()).suffix().toLower();
  if (suffix.isEmpty()) {
    suffix = "png";
  }

  QDir dir(QDir::homePath());
  return dir.absoluteFilePath(QString("cache/member-avatar-cache/%1.%2")
                                  .arg(QString::fromLatin1(hash))
                                  .arg(suffix));
}

QString
MemberListController::filePreviewSource(const QString &filePath) const {
  return QUrl::fromLocalFile(filePath).toString();
}

void MemberListController::ensureAvatarImageWorker() {
  if (m_avatarWorker != 0) {
    return;
  }

  m_avatarThread = new QThread(this);
  m_avatarWorker = new AttachmentImageCacheWorker();
  m_avatarWorker->moveToThread(m_avatarThread);

  connect(m_avatarThread, SIGNAL(finished()), m_avatarWorker,
          SLOT(deleteLater()));
  connect(m_avatarWorker, SIGNAL(imageCached(QString, QString)), this,
          SLOT(onAvatarImageCached(QString, QString)));
  connect(m_avatarWorker, SIGNAL(imageFailed(QString)), this,
          SLOT(onAvatarImageFailed(QString)));

  m_avatarThread->start();
}

void MemberListController::rebuildMemberDataModel() {
  m_memberDataModel->clear();
  if (!m_store || m_channelId.isEmpty()) {
    return;
  }

  QVariantList members = m_store->memberListForChannel(m_channelId);
  if (members.isEmpty()) {
    return;
  }

  QVariantList roles = m_store->guildRolesForGuild(m_guildId);
  QMap<QString, RoleGroup> roleGroupById;
  for (int i = 0; i < roles.size(); ++i) {
    QVariantMap role = roles.at(i).toMap();
    if (!role.value("hoisted").toBool()) {
      continue;
    }
    RoleGroup group;
    group.id = role.value("id").toString();
    group.name = role.value("name").toString();
    group.color = role.value("color").toString();
    group.position = role.value("position").toInt();
    roleGroupById.insert(group.id, group);
  }

  RoleGroup onlineGroup;
  onlineGroup.id = "";
  onlineGroup.name = tr("Online");
  onlineGroup.position = -1; // luôn xếp sau mọi role hoisted có position >= 0
  RoleGroup offlineGroup;
  offlineGroup.id = "";
  offlineGroup.name = tr("Offline");
  offlineGroup.position = -2; // luôn xếp sau nhóm Online

  for (int i = 0; i < members.size(); ++i) {
    QVariantMap member = members.at(i).toMap();
    QString primaryRoleId = member.value("primaryRoleId").toString();
    QString status = member.value("status").toString();

    if (!primaryRoleId.isEmpty() && roleGroupById.contains(primaryRoleId)) {
      roleGroupById[primaryRoleId].members.append(member);
    } else if (isOnlineStatus(status)) {
      onlineGroup.members.append(member);
    } else {
      offlineGroup.members.append(member);
    }
  }

  QVector<RoleGroup> orderedGroups;
  for (QMap<QString, RoleGroup>::const_iterator it = roleGroupById.constBegin();
       it != roleGroupById.constEnd(); ++it) {
    if (!it.value().members.isEmpty()) {
      orderedGroups.append(it.value());
    }
  }
  std::sort(orderedGroups.begin(), orderedGroups.end(),
            roleGroupPositionGreater);
  if (!onlineGroup.members.isEmpty()) {
    orderedGroups.append(onlineGroup);
  }
  if (!offlineGroup.members.isEmpty()) {
    orderedGroups.append(offlineGroup);
  }

  for (int g = 0; g < orderedGroups.size(); ++g) {
    RoleGroup group = orderedGroups.at(g);
    std::sort(group.members.begin(), group.members.end(), memberNameLess);

    QVariantMap roleRow;
    roleRow["type"] = "role";
    roleRow["name"] = group.name;
    roleRow["count"] = group.members.size();
    m_memberDataModel->append(roleRow);

    for (int i = 0; i < group.members.size(); ++i) {
      QVariantMap member = group.members.at(i).toMap();
      QString displayName = member.value("displayName").toString();
      QString status = member.value("status").toString();
      QString userId = member.value("userId").toString();
      QString remoteAvatarUrl = member.value("avatarUrl").toString();

      // "avatar" ở đây CHỈ chứa URL gốc trên CDN (chưa cache) — QML phải
      // tự gọi memberListController.cachedAvatarSource(avatar) khi hàng
      // này thực sự render trên màn hình để lấy source file cục bộ (hoặc
      // trigger tải nếu chưa có), đúng thiết kế lazy-per-row-visibility.
      // Không gọi cachedAvatarSource() ở đây (trong C++) vì điều đó sẽ
      // tải avatar cho MỌI member ngay khi model build xong, kể cả những
      // hàng chưa cuộn tới — đi ngược lại yêu cầu tối ưu ban đầu.
      QVariantMap memberRow;
      memberRow["type"] = "member";
      memberRow["userId"] = userId;
      memberRow["name"] = displayName;
      memberRow["initials"] =
          displayName.isEmpty() ? QString("?")
                                : displayName.left(1).toUpper();
      memberRow["avatarUrl"] = remoteAvatarUrl;
      memberRow["avatar"] = QString(); // QML tự nạp qua cachedAvatarSource()
      memberRow["avatarColor"] = fallbackAvatarColorForUserId(userId);
      memberRow["nameColor"] = group.color.isEmpty() ? "#F2F3F5" : group.color;
      memberRow["status"] = status.isEmpty() ? tr("Offline") : status;
      m_memberDataModel->append(memberRow);
    }
  }
}
