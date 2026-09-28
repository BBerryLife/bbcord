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

// "online" shows a green icon; "idle"/"dnd" also count as active. Only "offline"/empty go to "Offline".
bool isOnlineStatus(const QString &status) {
  return status == "online" || status == "idle" || status == "dnd";
}

// Fallback colors for members without a loaded avatar (Discord's default-avatar family).
const char *kFallbackAvatarColors[] = {
    "#5865F2", "#EB459E", "#57F287", "#FEE75C",
    "#ED4245", "#3BA55D", "#9B84EE", "#00A8FC",
};
const int kFallbackAvatarColorCount = 8;

// Stable fallback color from userId, like Discord's default avatar colors.
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
    // Sheet reopened for an already loaded channel: show the old data at once, since
    // Discord may not resend SYNC if nothing changed.
    rebuildMemberDataModel();
  } else {
    m_memberDataModel->clear();
  }

  if (!m_isLoading) {
    m_isLoading = true;
    emit isLoadingChanged();
  }

  // DMs have no guild_id and the Members sheet does not apply to them; this guard
  // avoids an empty guild-subscribe. Uses requestMemberListSync(), not
  // subscribeToGuildChannel(), whose request was already consumed on channel open and
  // would be dropped by the sendLazyRequest() dedup cache (sendMemberListSync()
  // bypasses it).
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

  // Tell the gateway the sheet closed so it stops re-sending SYNC after a reconnect (m_activeMemberListGuildId).
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
  // A role was updated (color/name): rebuild with the member list already in hand, without waiting for a SYNC.
  rebuildMemberDataModel();
}

void MemberListController::onAvatarImageCached(const QString &url,
                                               const QString &path) {
  m_loadingAvatarUrls.remove(url);
  emit avatarCached(url, filePreviewSource(path));
}

void MemberListController::onAvatarImageFailed(const QString &url) {
  m_loadingAvatarUrls.remove(url);
  // No avatarCached() on failure; QML keeps the fallback avatarColor already on the row.
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

QString MemberListController::displayLabelForStatus(
    const QString &status) const {
  if (status == "online") {
    return tr("Online");
  }
  if (status == "idle") {
    return tr("Idle");
  }
  if (status == "dnd") {
    return tr("Do Not Disturb");
  }
  // "offline" or empty (presence field missing/unparseable) both land
  // here - Discord doesn't visually distinguish the two cases.
  return tr("Offline");
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
  onlineGroup.position = -1; // always sorted after any hoisted role with position >= 0
  RoleGroup offlineGroup;
  offlineGroup.id = "";
  offlineGroup.name = tr("Offline");
  offlineGroup.position = -2; // always sorted after the Online group

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

      // avatar holds only the raw CDN URL: QML calls cachedAvatarSource() when the row is
      // actually shown, so avatars of rows never scrolled to are not loaded.
      QVariantMap memberRow;
      memberRow["type"] = "member";
      memberRow["userId"] = userId;
      memberRow["name"] = displayName;
      memberRow["initials"] =
          displayName.isEmpty() ? QString("?")
                                : displayName.left(1).toUpper();
      memberRow["avatarUrl"] = remoteAvatarUrl;
      memberRow["avatar"] = QString(); // QML loads it via cachedAvatarSource()
      memberRow["avatarColor"] = fallbackAvatarColorForUserId(userId);
      memberRow["nameColor"] = group.color.isEmpty() ? "#F2F3F5" : group.color;
      memberRow["status"] = displayLabelForStatus(status);
      m_memberDataModel->append(memberRow);
    }
  }
}
