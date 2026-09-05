#include "ChatController.hpp"

#include "../core/AppStore.hpp"
#include "../core/AttachmentImageCacheWorker.hpp"
#include "../core/Client.hpp"
#include "../core/models/Models.hpp"

#include <bb/system/Clipboard>
#include <bb/system/InvokeManager>
#include <bb/system/InvokeRequest>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QThread>
#include <QUrl>

namespace {
const qint64 kMaxPendingAttachmentBytes = 8 * 1024 * 1024;
const int kMaxPendingAttachments = 10;
const int kAttachmentPreviewWidthPx = 512;
const int kAttachmentPreviewHeightPx = 512;

bool shouldUseDiscordPreview(int originalWidth, int originalHeight) {
  return originalWidth > kAttachmentPreviewWidthPx ||
         originalHeight > kAttachmentPreviewHeightPx;
}

void discordPreviewSize(int originalWidth, int originalHeight,
                        int &previewWidth, int &previewHeight) {
  if (originalWidth <= 0 || originalHeight <= 0) {
    previewWidth = kAttachmentPreviewWidthPx;
    previewHeight = kAttachmentPreviewHeightPx;
    return;
  }

  if (originalWidth <= kAttachmentPreviewWidthPx &&
      originalHeight <= kAttachmentPreviewHeightPx) {
    previewWidth = originalWidth;
    previewHeight = originalHeight;
    return;
  }

  double widthScale = static_cast<double>(kAttachmentPreviewWidthPx) /
                      static_cast<double>(originalWidth);
  double heightScale = static_cast<double>(kAttachmentPreviewHeightPx) /
                       static_cast<double>(originalHeight);
  double scale = widthScale < heightScale ? widthScale : heightScale;
  previewWidth =
      static_cast<int>(static_cast<double>(originalWidth) * scale + 0.5);
  previewHeight =
      static_cast<int>(static_cast<double>(originalHeight) * scale + 0.5);
}

QString discordPreviewUrl(const QString &url, int originalWidth,
                          int originalHeight) {
  QUrl parsed(url.trimmed());
  if (!parsed.isValid()) {
    return url.trimmed();
  }

  QString scheme = parsed.scheme().toLower();
  QString host = parsed.host().toLower();
  if ((scheme != "http" && scheme != "https") ||
      (!host.endsWith("discordapp.com") && !host.endsWith("discordapp.net"))) {
    return url.trimmed();
  }

  parsed.setScheme("https");
  parsed.setHost("media.discordapp.net");

  int previewWidth = kAttachmentPreviewWidthPx;
  int previewHeight = kAttachmentPreviewHeightPx;
  discordPreviewSize(originalWidth, originalHeight, previewWidth,
                     previewHeight);
  parsed.removeAllQueryItems("width");
  parsed.removeAllQueryItems("height");
  parsed.addQueryItem("width", QString::number(previewWidth));
  parsed.addQueryItem("height", QString::number(previewHeight));
  return parsed.toString();
}
} // namespace

ChatController::ChatController(DiscordClient *client, AppStore *store,
                               QObject *parent)
    : QObject(parent), m_client(client), m_store(store),
      m_chatDataModel(new bb::cascades::ArrayDataModel(this)),
      m_pendingAttachmentsModel(new bb::cascades::ArrayDataModel(this)),
      m_imageThread(0), m_imageWorker(0), m_pendingAttachmentIsImage(false) {
  if (m_client) {
    connect(this, SIGNAL(initialMessagesRequested(QString, QString)), m_client,
            SLOT(loadInitialChatMessages(QString, QString)));
    connect(this, SIGNAL(olderMessagesRequested(QString, QString)), m_client,
            SLOT(loadOlderChatMessages(QString, QString)));
    connect(
        this,
        SIGNAL(sendMessageRequested(QString, QString, QString, QString,
                                    QStringList)),
        m_client,
        SLOT(sendChatMessage(QString, QString, QString, QString, QStringList)));
    connect(this, SIGNAL(editMessageRequested(QString, QString, QString)),
            m_client, SLOT(editChatMessage(QString, QString, QString)));
    connect(this, SIGNAL(deleteMessageRequested(QString, QString)), m_client,
            SLOT(deleteChatMessage(QString, QString)));
  }

  if (m_store) {
    connect(m_store, SIGNAL(currentChannelMessagesChanged()), this,
            SLOT(onCurrentChannelMessagesChanged()));
    connect(m_store, SIGNAL(chatMessagesReset(QString, QVariantList)), this,
            SLOT(onChatMessagesReset(QString, QVariantList)));
    connect(m_store, SIGNAL(chatMessagesPrepended(QString, QVariantList)), this,
            SLOT(onChatMessagesPrepended(QString, QVariantList)));
    connect(m_store, SIGNAL(chatMessagesBatched(QString, QVariantList)), this,
            SLOT(onChatMessagesBatched(QString, QVariantList)));
    connect(m_store, SIGNAL(chatMessageAdded(QString, QVariantMap)), this,
            SLOT(onChatMessageAdded(QString, QVariantMap)));
    connect(m_store, SIGNAL(chatMessageUpdated(QString, QVariantMap)), this,
            SLOT(onChatMessageUpdated(QString, QVariantMap)));
    connect(m_store, SIGNAL(chatMessageDeleted(QString, QString)), this,
            SLOT(onChatMessageDeleted(QString, QString)));
    connect(m_store, SIGNAL(chatAvatarChanged(QString, QString)), this,
            SLOT(onChatAvatarChanged(QString, QString)));
  }
}

ChatController::~ChatController() {
  if (m_imageWorker != 0) {
    QMetaObject::invokeMethod(m_imageWorker, "cancelAll",
                              Qt::BlockingQueuedConnection);
  }

  if (m_imageThread != 0) {
    m_imageThread->quit();
    m_imageThread->wait(2000);
  }
}

QString ChatController::currentChannelId() const { return m_currentChannelId; }

QString ChatController::currentGuildId() const { return m_currentGuildId; }

QString ChatController::currentChannelName() const {
  return m_currentChannelName;
}

bool ChatController::hasPendingAttachment() const {
  return !m_pendingAttachmentPaths.isEmpty();
}

QString ChatController::pendingAttachmentName() const {
  return m_pendingAttachmentName;
}

QString ChatController::pendingAttachmentPreview() const {
  return m_pendingAttachmentPreview;
}

bool ChatController::pendingAttachmentIsImage() const {
  return m_pendingAttachmentIsImage;
}

QString ChatController::pendingAttachmentError() const {
  return m_pendingAttachmentError;
}

bb::cascades::DataModel *ChatController::pendingAttachmentsModel() const {
  return m_pendingAttachmentsModel;
}

bb::cascades::DataModel *ChatController::chatDataModel() const {
  return m_chatDataModel;
}

void ChatController::openChannel(const QString &channelId,
                                 const QString &guildId,
                                 const QString &channelName) {
  QString safeChannelId = channelId.trimmed();
  if (safeChannelId.isEmpty()) {
    return;
  }

  bool changed = m_currentChannelId != safeChannelId ||
                 m_currentGuildId != guildId.trimmed() ||
                 m_currentChannelName != channelName;

  m_currentChannelId = safeChannelId;
  m_currentGuildId = guildId.trimmed();
  m_currentChannelName = channelName;

  if (m_client) {
    m_client->subscribeToGuildChannel(safeChannelId, m_currentGuildId);
    m_client->selectChannel(safeChannelId);
  } else if (m_store) {
    m_store->selectChannel(safeChannelId);
  }

  if (changed) {
    rebuildChatDataModel();
    emit currentChannelChanged();
  }

  requestInitialMessages();
}

void ChatController::closeChannel() {
  if (m_currentChannelId.isEmpty() && m_currentGuildId.isEmpty() &&
      m_currentChannelName.isEmpty()) {
    return;
  }

  m_currentChannelId.clear();
  m_currentGuildId.clear();
  m_currentChannelName.clear();
  m_chatDataModel->clear();
  emit chatDataModelChanged();
  emit currentChannelChanged();
}

QVariantList ChatController::currentMessages() const {
  return m_store ? m_store->messagesForChannel(safeCurrentChannelId())
                 : QVariantList();
}

QVariantList
ChatController::messagesForChannel(const QString &channelId) const {
  return m_store ? m_store->messagesForChannel(channelId) : QVariantList();
}

bool ChatController::isInitialLoaded() const {
  return m_store && m_store->isChatInitialLoaded(safeCurrentChannelId());
}

bool ChatController::isLoadingInitial() const {
  return m_store && m_store->isChatLoadingInitial(safeCurrentChannelId());
}

bool ChatController::isLoadingBefore() const {
  return m_store && m_store->isChatLoadingBefore(safeCurrentChannelId());
}

bool ChatController::hasMoreBefore() const {
  return m_store && m_store->hasMoreChatBefore(safeCurrentChannelId());
}

QString ChatController::oldestMessageId() const {
  return m_store ? m_store->oldestChatMessageId(safeCurrentChannelId())
                 : QString();
}

QString ChatController::newestMessageId() const {
  return m_store ? m_store->newestChatMessageId(safeCurrentChannelId())
                 : QString();
}

void ChatController::requestInitialMessages() {
  QString channelId = safeCurrentChannelId();
  if (channelId.isEmpty() || !m_store) {
    return;
  }

  if (m_store->isChatInitialLoaded(channelId) ||
      m_store->isChatLoadingInitial(channelId)) {
    return;
  }

  m_store->setChatLoadingInitial(channelId, true);
  emit initialMessagesRequested(channelId, m_currentGuildId);
}

void ChatController::requestOlderMessages() {
  QString channelId = safeCurrentChannelId();
  if (channelId.isEmpty() || !m_store) {
    return;
  }

  if (!m_store->isChatInitialLoaded(channelId) ||
      m_store->isChatLoadingBefore(channelId) ||
      !m_store->hasMoreChatBefore(channelId)) {
    return;
  }

  QString beforeMessageId = m_store->oldestChatMessageId(channelId);
  if (beforeMessageId.isEmpty()) {
    return;
  }

  m_store->setChatLoadingBefore(channelId, true);
  emit olderMessagesRequested(channelId, beforeMessageId);
}

bool ChatController::setPendingAttachment(const QString &filePath) {
  m_pendingAttachmentPaths.clear();
  bool added = appendPendingAttachment(filePath);
  syncPrimaryPendingAttachment();
  rebuildPendingAttachmentsModel();
  emit pendingAttachmentChanged();
  return added;
}

int ChatController::addPendingAttachments(const QVariantList &filePaths) {
  int added = 0;
  for (int i = 0; i < filePaths.size(); ++i) {
    if (m_pendingAttachmentPaths.size() >= kMaxPendingAttachments) {
      m_pendingAttachmentError = tr("Maximum 10 attachments");
      break;
    }
    if (appendPendingAttachment(filePaths.at(i).toString())) {
      ++added;
    }
  }

  syncPrimaryPendingAttachment();
  rebuildPendingAttachmentsModel();
  emit pendingAttachmentChanged();
  return added;
}

void ChatController::removePendingAttachment(int index) {
  if (index < 0 || index >= m_pendingAttachmentPaths.size()) {
    return;
  }

  m_pendingAttachmentPaths.removeAt(index);
  m_pendingAttachmentError.clear();
  syncPrimaryPendingAttachment();
  rebuildPendingAttachmentsModel();
  emit pendingAttachmentChanged();
}

void ChatController::clearPendingAttachment() {
  if (m_pendingAttachmentPaths.isEmpty() && m_pendingAttachmentPath.isEmpty() &&
      m_pendingAttachmentName.isEmpty() &&
      m_pendingAttachmentPreview.isEmpty() &&
      m_pendingAttachmentError.isEmpty() && !m_pendingAttachmentIsImage) {
    return;
  }

  m_pendingAttachmentPaths.clear();
  m_pendingAttachmentPath.clear();
  m_pendingAttachmentName.clear();
  m_pendingAttachmentPreview.clear();
  m_pendingAttachmentError.clear();
  m_pendingAttachmentIsImage = false;
  rebuildPendingAttachmentsModel();
  emit pendingAttachmentChanged();
}

QString ChatController::sendMessage(const QString &content,
                                    const QString &replyMessageId,
                                    const QString &replyAuthor,
                                    const QString &replyMessage) {
  QString channelId = safeCurrentChannelId();
  QString trimmedContent = content.trimmed();
  QStringList attachmentPaths = m_pendingAttachmentPaths;
  bool hasAttachment = !attachmentPaths.isEmpty();
  if (channelId.isEmpty() || (!hasAttachment && trimmedContent.isEmpty()) ||
      !m_store) {
    return QString();
  }

  QString pendingId = buildPendingMessageId();
  DiscordMessage message;
  message.id = pendingId;
  message.channelId = channelId;
  message.guildId = m_currentGuildId;
  message.nonce = pendingId;
  message.content = trimmedContent;
  message.timestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
  message.replyMessageId = replyMessageId.trimmed();
  message.replyAuthor = replyAuthor;
  message.replyContent = replyMessage;
  message.author.id = m_store->currentUserId();
  message.author.username = m_store->currentUserName();
  message.author.globalName = m_store->currentUserName();

  for (int i = 0; i < attachmentPaths.size(); ++i) {
    DiscordAttachment attachment;
    QFileInfo fileInfo(attachmentPaths.at(i));
    attachment.id = QString::number(i);
    attachment.filename = fileInfo.fileName();
    attachment.url = filePreviewSource(attachmentPaths.at(i));
    attachment.proxyUrl = attachment.url;
    attachment.contentType = fileContentType(attachmentPaths.at(i));
    attachment.size = static_cast<int>(fileInfo.size());
    if (attachment.isImage()) {
      attachment.width = 24;
      attachment.height = 18;
    }
    message.attachments.append(attachment);
  }

  m_store->addPendingChatMessage(message);
  emit sendMessageRequested(channelId, trimmedContent, pendingId,
                            message.replyMessageId, attachmentPaths);
  clearPendingAttachment();
  return pendingId;
}

void ChatController::editMessage(const QString &messageId,
                                 const QString &content) {
  QString channelId = safeCurrentChannelId();
  QString safeMessageId = messageId.trimmed();
  QString trimmedContent = content.trimmed();
  if (channelId.isEmpty() || safeMessageId.isEmpty() ||
      trimmedContent.isEmpty() || !m_store) {
    return;
  }

  DiscordMessage message;
  message.id = safeMessageId;
  message.channelId = channelId;
  message.guildId = m_currentGuildId;
  message.content = trimmedContent;
  message.editedTimestamp =
      QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
  m_store->updateChatMessage(message);
  emit editMessageRequested(channelId, safeMessageId, trimmedContent);
}

void ChatController::deleteMessage(const QString &messageId) {
  QString channelId = safeCurrentChannelId();
  QString safeMessageId = messageId.trimmed();
  if (channelId.isEmpty() || safeMessageId.isEmpty() || !m_store) {
    return;
  }

  m_store->deleteChatMessage(channelId, safeMessageId);
  emit deleteMessageRequested(channelId, safeMessageId);
}

void ChatController::markPendingFailed(const QString &messageId) {
  QString channelId = safeCurrentChannelId();
  QString safeMessageId = messageId.trimmed();
  if (channelId.isEmpty() || safeMessageId.isEmpty() || !m_store) {
    return;
  }

  m_store->markPendingChatMessageFailed(channelId, safeMessageId);
}

void ChatController::copyText(const QString &text) {
  QString safeText = text;
  if (safeText.isEmpty()) {
    return;
  }

  bb::system::Clipboard clipboard;
  clipboard.remove("text/plain");
  clipboard.insert("text/plain", safeText.toUtf8());
}

void ChatController::openAttachment(const QString &url) {
  QString safeUrl = url.trimmed();
  if (safeUrl.isEmpty()) {
    return;
  }

  bb::system::InvokeManager manager;
  bb::system::InvokeRequest request;
  request.setTarget("sys.browser");
  request.setAction("bb.action.OPEN");
  request.setMimeType("text/html");
  request.setUri(safeUrl);
  manager.invoke(request);
}

QString ChatController::avatarSource(const QString &userId,
                                     const QString &avatarHash) {
  QString safeUserId = userId.trimmed();
  QString safeAvatarHash = avatarHash.trimmed();
  if (safeUserId.isEmpty() || safeAvatarHash.isEmpty() || m_client == 0) {
    return QString();
  }

  QString source = m_client->avatarSourceForUser(safeUserId);
  if (source.isEmpty()) {
    m_client->loadUserAvatar(safeUserId, safeAvatarHash);
  }
  return source;
}

QString ChatController::cachedImageSource(const QString &url) {
  QString safeUrl = url.trimmed();
  if (safeUrl.isEmpty()) {
    return QString();
  }

  if (!isRemoteImageUrl(safeUrl)) {
    return safeUrl;
  }

  if (m_cachedAttachmentImages.contains(safeUrl)) {
    return m_cachedAttachmentImages.value(safeUrl);
  }

  QString path = attachmentImageCachePath(safeUrl);
  QFileInfo cachedFile(path);
  if (cachedFile.exists() && cachedFile.size() > 0) {
    QString source = filePreviewSource(path);
    m_cachedAttachmentImages.insert(safeUrl, source);
    return source;
  }

  return QString();
}

void ChatController::requestCachedImage(const QString &url) {
  QString safeUrl = url.trimmed();
  if (safeUrl.isEmpty() || !isRemoteImageUrl(safeUrl) ||
      m_cachedAttachmentImages.contains(safeUrl) ||
      m_loadingAttachmentImages.contains(safeUrl)) {
    return;
  }

  QString path = attachmentImageCachePath(safeUrl);
  QFileInfo cachedFile(path);
  if (cachedFile.exists() && cachedFile.size() > 0) {
    QString source = filePreviewSource(path);
    m_cachedAttachmentImages.insert(safeUrl, source);
    emit attachmentImageCached(safeUrl, source);
    return;
  }

  ensureAttachmentImageWorker();
  if (m_imageWorker == 0) {
    return;
  }

  m_loadingAttachmentImages.insert(safeUrl);
  QMetaObject::invokeMethod(m_imageWorker, "requestImage", Qt::QueuedConnection,
                            Q_ARG(QString, safeUrl), Q_ARG(QString, path),
                            Q_ARG(qint64, 0));
}

void ChatController::cancelCachedImage(const QString &url) {
  QString safeUrl = url.trimmed();
  if (safeUrl.isEmpty() || !m_loadingAttachmentImages.contains(safeUrl) ||
      m_imageWorker == 0) {
    return;
  }

  m_loadingAttachmentImages.remove(safeUrl);
  QMetaObject::invokeMethod(m_imageWorker, "cancelImage", Qt::QueuedConnection,
                            Q_ARG(QString, safeUrl));
}

void ChatController::clearMediaCacheState() {
  m_cachedAttachmentImages.clear();
  m_loadingAttachmentImages.clear();
  rebuildChatDataModel();
}

void ChatController::onCurrentChannelMessagesChanged() {
  if (m_chatDataModel->size() == 0) {
    rebuildChatDataModel();
  }
}

void ChatController::onChatMessagesReset(const QString &channelId,
                                         const QVariantList &messages) {
  if (channelId != safeCurrentChannelId()) {
    return;
  }

  syncChatDataModel(messages);
}

void ChatController::onChatMessagesPrepended(const QString &channelId,
                                             const QVariantList &messages) {
  if (channelId != safeCurrentChannelId() || messages.isEmpty()) {
    return;
  }

  QVariantList current = currentMessages();
  if (current.size() < messages.size()) {
    syncChatDataModel(current);
    return;
  }

  for (int i = 0; i < messages.size(); ++i) {
    m_chatDataModel->insert(i, prepareMessageForModel(messages.at(i).toMap()));
  }

  int refreshEnd = messages.size();
  if (refreshEnd >= current.size()) {
    refreshEnd = current.size() - 1;
  }
  for (int i = 0; i <= refreshEnd; ++i) {
    if (i >= 0 && i < m_chatDataModel->size() && i < current.size()) {
      QVariantMap rawMessage = current.at(i).toMap();
      // Item này có thể đã tồn tại từ trước (không phải 1 trong các
      // message vừa prepend ở trên) và đã có avatar tải xong - giữ lại
      // trước khi build lại từ dữ liệu thô. Xem giải thích đầy đủ ở
      // refreshModelGroupingAround()/onChatMessagesBatched().
      QString existingAvatarSource =
          m_chatDataModel->value(i).toMap().value("avatarSource").toString();
      if (!existingAvatarSource.isEmpty()) {
        rawMessage["avatarSource"] = existingAvatarSource;
      }
      m_chatDataModel->replace(i, prepareMessageForModel(rawMessage));
    }
  }
}

void ChatController::onChatMessagesBatched(const QString &channelId,
                                           const QVariantList &messages) {
  if (channelId != safeCurrentChannelId()) {
    return;
  }

  QVariantList current = currentMessages();
  int minTouchedIndex = m_chatDataModel->size();
  int maxTouchedIndex = -1;

  for (int i = 0; i < messages.size(); ++i) {
    QVariantMap rawMessage = messages.at(i).toMap();
    int index = chatDataModelIndexForMessage(rawMessage.value("id").toString());
    if (index < 0) {
      index = chatDataModelIndexForNonce(rawMessage.value("nonce").toString());
    }
    // Nếu message này đã có trong model (đang update, không phải mới),
    // copy avatarSource hiện có sang trước khi build lại - "rawMessage"
    // đến từ AppStore, không mang theo avatarSource (chỉ tồn tại trong
    // m_chatDataModel, set bởi onChatAvatarChanged() khi avatar tải
    // xong). Không làm vậy có thể xoá mất avatar đã tải đúng của item
    // này nếu avatar chưa kịp có trong cache tại đúng lúc này (đang tải
    // dở avatar của MỘT tin nhắn khác, cùng tác giả) - xem
    // prepareMessageForModel().
    if (index >= 0) {
      QString existingAvatarSource =
          m_chatDataModel->value(index).toMap().value("avatarSource").toString();
      if (!existingAvatarSource.isEmpty()) {
        rawMessage["avatarSource"] = existingAvatarSource;
      }
    }

    QVariantMap item = prepareMessageForModel(rawMessage);
    if (index >= 0) {
      m_chatDataModel->replace(index, item);
    } else {
      if (!current.isEmpty() &&
          !sameMessageIdentity(rawMessage, current.last().toMap())) {
        syncChatDataModel(current);
        return;
      }
      m_chatDataModel->append(item);
      index = m_chatDataModel->size() - 1;
    }

    if (index < minTouchedIndex) {
      minTouchedIndex = index;
    }
    if (index > maxTouchedIndex) {
      maxTouchedIndex = index;
    }
  }

  if (m_chatDataModel->size() != current.size()) {
    syncChatDataModel(current);
    return;
  }

  for (int i = minTouchedIndex - 1; i <= maxTouchedIndex + 1; ++i) {
    if (i < 0 || i >= m_chatDataModel->size() || i >= current.size()) {
      continue;
    }

    QVariantMap rawMessage = current.at(i).toMap();
    QVariantMap existing = m_chatDataModel->value(i).toMap();
    QString existingAvatarSource = existing.value("avatarSource").toString();
    if (!existingAvatarSource.isEmpty()) {
      rawMessage["avatarSource"] = existingAvatarSource;
    }

    QVariantMap grouped = prepareMessageForModel(rawMessage);
    if (!sameMessageIdentity(grouped, existing)) {
      syncChatDataModel(current);
      return;
    }
    m_chatDataModel->replace(i, grouped);
  }
}

void ChatController::onChatMessageAdded(const QString &channelId,
                                        const QVariantMap &message) {
  if (channelId != safeCurrentChannelId()) {
    return;
  }

  QVariantMap item = prepareMessageForModel(message);
  int index = chatDataModelIndexForMessage(item.value("id").toString());
  if (index >= 0) {
    m_chatDataModel->replace(index, item);
    refreshModelGroupingAround(index);
    return;
  }

  m_chatDataModel->append(item);
  refreshModelGroupingAround(m_chatDataModel->size() - 1);
}

void ChatController::onChatMessageUpdated(const QString &channelId,
                                          const QVariantMap &message) {
  if (channelId != safeCurrentChannelId()) {
    return;
  }

  replaceGroupedMessage(message);
}

void ChatController::onChatMessageDeleted(const QString &channelId,
                                          const QString &messageId) {
  if (channelId != safeCurrentChannelId()) {
    return;
  }

  int index = chatDataModelIndexForMessage(messageId);
  if (index < 0) {
    return;
  }

  syncChatDataModel(currentMessages());
}

void ChatController::onChatAvatarChanged(const QString &userId,
                                         const QString &avatarSource) {
  if (userId.isEmpty() || avatarSource.isEmpty()) {
    return;
  }

  for (int i = 0; i < m_chatDataModel->size(); ++i) {
    QVariantMap message = m_chatDataModel->value(i).toMap();
    if (message.value("authorId").toString() == userId) {
      message["avatarSource"] = avatarSource;
      m_chatDataModel->replace(i, message);
    }
  }
}

void ChatController::onAttachmentImageCached(const QString &url,
                                             const QString &path) {
  m_loadingAttachmentImages.remove(url);
  QString source = filePreviewSource(path);
  m_cachedAttachmentImages.insert(url, source);
  updateAttachmentImageInModel(url, source, false, false);
  emit attachmentImageCached(url, source);
}

void ChatController::onAttachmentImageFailed(const QString &url) {
  m_loadingAttachmentImages.remove(url);
  updateAttachmentImageInModel(url, QString(), false, true);
  emit attachmentImageFailed(url);
}

QString ChatController::safeCurrentChannelId() const {
  if (!m_currentChannelId.isEmpty()) {
    return m_currentChannelId;
  }

  return m_store ? m_store->selectedChannelId() : QString();
}

QString ChatController::buildPendingMessageId() const {
  return QString("pending-%1").arg(QDateTime::currentMSecsSinceEpoch());
}

QVariantMap ChatController::findCachedMessage(const QString &messageId) const {
  QVariantList messages = currentMessages();
  for (int i = 0; i < messages.size(); ++i) {
    QVariantMap message = messages.at(i).toMap();
    if (message.value("id").toString() == messageId) {
      return message;
    }
  }
  return QVariantMap();
}

void ChatController::rebuildChatDataModel() {
  replaceChatDataModel(currentMessages());
}

void ChatController::replaceChatDataModel(const QVariantList &messages) {
  // clear() xoá sạch mọi state hiện có trong model, bao gồm avatarSource
  // đã tải xong của từng message (field đó chỉ tồn tại trong
  // m_chatDataModel, không có trong dữ liệu thô của AppStore - xem
  // refreshModelGroupingAround()). Gom lại theo authorId trước khi clear
  // để không phải tải lại avatar cho những tác giả đã có avatar đúng.
  QHash<QString, QString> avatarSourceByAuthorId;
  for (int i = 0; i < m_chatDataModel->size(); ++i) {
    QVariantMap existing = m_chatDataModel->value(i).toMap();
    QString existingAuthorId = existing.value("authorId").toString();
    QString existingAvatarSource = existing.value("avatarSource").toString();
    if (!existingAuthorId.isEmpty() && !existingAvatarSource.isEmpty()) {
      avatarSourceByAuthorId.insert(existingAuthorId, existingAvatarSource);
    }
  }

  m_chatDataModel->clear();
  for (int i = 0; i < messages.size(); ++i) {
    QVariantMap rawMessage = messages.at(i).toMap();
    QString authorId = rawMessage.value("authorId").toString();
    if (!authorId.isEmpty() && avatarSourceByAuthorId.contains(authorId)) {
      rawMessage["avatarSource"] = avatarSourceByAuthorId.value(authorId);
    }
    m_chatDataModel->append(prepareMessageForModel(rawMessage));
  }
  emit chatDataModelChanged();
}

void ChatController::syncChatDataModel(const QVariantList &messages) {
  int modelSize = m_chatDataModel->size();
  if (messages.size() >= modelSize && modelSize > 0) {
    int appendedCount = messages.size() - modelSize;
    bool prefixMatches = true;
    for (int i = 0; i < modelSize; ++i) {
      if (!sameMessageIdentity(messages.at(i).toMap(),
                               m_chatDataModel->value(i).toMap())) {
        prefixMatches = false;
        break;
      }
    }

    if (prefixMatches) {
      int firstAppendIndex = modelSize;
      for (int i = modelSize; i < messages.size(); ++i) {
        m_chatDataModel->append(prepareMessageForModel(messages.at(i).toMap()));
      }
      int refreshStart = firstAppendIndex - 1;
      if (refreshStart < 0) {
        refreshStart = 0;
      }
      for (int i = refreshStart; i < messages.size(); ++i) {
        QVariantMap rawMessage = messages.at(i).toMap();
        // i có thể trỏ tới item ĐÃ tồn tại từ trước (refreshStart lùi 1
        // để refresh phần grouping giáp ranh) - giữ lại avatar đã tải
        // của item đó trước khi build lại từ dữ liệu thô. Xem giải thích
        // đầy đủ ở refreshModelGroupingAround().
        if (i < m_chatDataModel->size()) {
          QString existingAvatarSource = m_chatDataModel->value(i)
                                             .toMap()
                                             .value("avatarSource")
                                             .toString();
          if (!existingAvatarSource.isEmpty()) {
            rawMessage["avatarSource"] = existingAvatarSource;
          }
        }
        m_chatDataModel->replace(i, prepareMessageForModel(rawMessage));
      }
      if (appendedCount > 0) {
        return;
      }
    }
  }

  int i = 0;
  while (i < messages.size() && i < m_chatDataModel->size()) {
    QVariantMap existing = m_chatDataModel->value(i).toMap();
    QVariantMap rawMessage = messages.at(i).toMap();
    QString existingAvatarSource = existing.value("avatarSource").toString();
    if (!existingAvatarSource.isEmpty()) {
      rawMessage["avatarSource"] = existingAvatarSource;
    }
    QVariantMap item = prepareMessageForModel(rawMessage);
    QString itemId = item.value("id").toString();
    QString existingId = existing.value("id").toString();
    QString itemNonce = item.value("nonce").toString();
    QString existingNonce = existing.value("nonce").toString();

    if (itemId == existingId ||
        (!itemNonce.isEmpty() && itemNonce == existingId) ||
        (!existingNonce.isEmpty() && existingNonce == itemId) ||
        (!itemNonce.isEmpty() && itemNonce == existingNonce)) {
      m_chatDataModel->replace(i, item);
      ++i;
      continue;
    }

    replaceChatDataModel(messages);
    return;
  }

  while (i < messages.size()) {
    m_chatDataModel->append(prepareMessageForModel(messages.at(i).toMap()));
    ++i;
  }

  if (m_chatDataModel->size() != messages.size()) {
    replaceChatDataModel(messages);
  }
}

QVariantMap ChatController::prepareMessageForModel(const QVariantMap &message) {
  QVariantMap item = message;
  QString authorId = item.value("authorId").toString();
  QString avatarHash = item.value("avatarHash").toString();
  QString attachmentUrl = item.value("attachmentUrl").toString();
  bool attachmentIsImage = item.value("attachmentIsImage").toBool();
  QVariantList attachments = item.value("attachments").toList();
  QString imageSource = item.value("image").toString();
  bool imageFailed = attachmentIsImage && !attachmentUrl.isEmpty() &&
                     item.value("imageLoadFailed").toBool();
  bool imageLoading = attachmentIsImage && !attachmentUrl.isEmpty() &&
                      item.value("imageLoading").toBool();

  if (attachments.isEmpty() && !attachmentUrl.isEmpty()) {
    QVariantMap attachment;
    attachment["url"] = attachmentUrl;
    attachment["filename"] = item.value("attachmentName").toString();
    attachment["isImage"] = attachmentIsImage;
    attachment["width"] = item.value("imageWidth", 0).toInt();
    attachment["height"] = item.value("imageHeight", 0).toInt();
    attachments.append(attachment);
  }

  QVariantList displayAttachments;
  for (int i = 0; i < attachments.size(); ++i) {
    QVariantMap attachment = attachments.at(i).toMap();
    QString url = attachment.value("url").toString();
    bool isImage = attachment.value("isImage").toBool();
    int width = attachment.value("width", 0).toInt();
    int height = attachment.value("height", 0).toInt();
    QString previewUrl = isImage && shouldUseDiscordPreview(width, height)
                             ? discordPreviewUrl(url, width, height)
                             : url;
    QString image = attachment.value("image").toString();
    bool failed = isImage && !url.isEmpty() &&
                  attachment.value("imageLoadFailed").toBool();
    bool loading =
        isImage && !url.isEmpty() && attachment.value("imageLoading").toBool();

    if (isImage && !url.isEmpty()) {
      if (!isRemoteImageUrl(previewUrl)) {
        image = previewUrl;
        loading = false;
        failed = false;
      } else {
        QString cached = cachedImageSource(previewUrl);
        if (!cached.isEmpty()) {
          image = cached;
          loading = false;
          failed = false;
        } else if (!failed) {
          image.clear();
          loading = true;
          requestCachedImage(previewUrl);
        }
      }
    }

    attachment["url"] = url;
    attachment["previewUrl"] = previewUrl;
    attachment["name"] = attachment.value("filename").toString();
    attachment["image"] = image;
    attachment["imageLoading"] = loading;
    attachment["imageLoadFailed"] = failed;
    attachment["isImage"] = isImage;
    attachment["width"] = width;
    attachment["height"] = height;
    double ratio = 16.0 / 9.0;
    if (width > 0 && height > 0) {
      ratio = static_cast<double>(width) / static_cast<double>(height);
    }
    double displayWidth = static_cast<double>(width) / 10.0;
    if (displayWidth <= 0.0) {
      displayWidth = 50.4;
    }
    double displayHeight = displayWidth / ratio;
    if (displayHeight > 43.2) {
      displayHeight = 43.2;
      displayWidth = displayHeight * ratio;
    }
    if (displayWidth < 16.8) {
      displayWidth = 16.8;
      displayHeight = displayWidth / ratio;
      if (displayHeight > 43.2) {
        displayHeight = 43.2;
      }
    }
    attachment["displayWidthDu"] = displayWidth;
    attachment["displayHeightDu"] = displayHeight;
    displayAttachments.append(attachment);

    if (i == 0) {
      attachmentUrl = url;
      attachmentIsImage = isImage;
      imageSource = image;
      imageLoading = loading;
      imageFailed = failed;
      item["attachmentName"] = attachment.value("name").toString();
      item["imageWidth"] = attachment.value("width", 0).toInt();
      item["imageHeight"] = attachment.value("height", 0).toInt();
    }
  }

  if (!authorId.isEmpty()) {
    QString avatarSource =
        m_cachedAttachmentImages.value(QString("avatar:%1").arg(authorId));
    if (avatarSource.isEmpty() && m_client != 0) {
      avatarSource = m_client->avatarSourceForUser(authorId);
      if (avatarSource.isEmpty() && !avatarHash.isEmpty()) {
        m_client->loadUserAvatar(authorId, avatarHash);
      }
    }
    if (avatarSource.isEmpty()) {
      // Chưa tính ra avatar mới (avatar đang tải hoặc chưa bắt đầu tải).
      // Giữ nguyên avatarSource đã có sẵn trong item đầu vào thay vì để
      // trống - "message" truyền vào hàm này có thể là dữ liệu THÔ đọc
      // lại từ store (xem currentMessages() ở refreshModelGroupingAround()
      // / onChatMessagesBatched()), vốn không mang theo avatarSource đã
      // được set trước đó bởi onChatAvatarChanged() khi avatar tải xong
      // lần đầu. Không giữ lại giá trị cũ ở đây khiến avatar của tin
      // nhắn ĐÃ CÓ AVATAR ĐÚNG bị xoá về rỗng mỗi khi item được re-render
      // qua đường này trong lúc avatar của MỘT tin nhắn khác (cùng tác
      // giả) vẫn đang trong hàng đợi tải - đúng hiện tượng "2 tin nhắn
      // cùng tác giả, chỉ 1 cái có avatar" quan sát được trên thiết bị.
      avatarSource = item.value("avatarSource").toString();
      item["avatarSource"] = avatarSource;
    } else {
      item["avatarSource"] = avatarSource;
      m_cachedAttachmentImages.insert(QString("avatar:%1").arg(authorId),
                                      avatarSource);
    }
  }

  if (displayAttachments.isEmpty() && attachmentIsImage &&
      !attachmentUrl.isEmpty()) {
    if (!isRemoteImageUrl(attachmentUrl)) {
      imageSource = attachmentUrl;
      imageLoading = false;
      imageFailed = false;
    } else {
      QString cached = cachedImageSource(attachmentUrl);
      if (!cached.isEmpty()) {
        imageSource = cached;
        imageLoading = false;
        imageFailed = false;
      } else if (!imageFailed) {
        imageSource.clear();
        imageLoading = true;
        requestCachedImage(attachmentUrl);
      }
    }
  }

  item["id"] = item.value("id").toString();
  item["author"] = item.value("author").toString();
  item["authorId"] = authorId;
  item["ownMessage"] =
      !authorId.isEmpty() && m_store && authorId == m_store->currentUserId();
  item["initials"] = item.value("initials").toString();
  item["avatarHash"] = avatarHash;
  // KHÔNG ghi đè bằng item.value("avatarSource") ở đây nữa - khối phía
  // trên đã set item["avatarSource"] đúng cách (giá trị mới hoặc giá trị
  // cũ được giữ lại), ghi đè lại bằng chính field đó là dư thừa và, tệ
  // hơn, từng khiến giá trị cũ bị đọc nhầm nếu thứ tự các trường trong
  // "item" không như mong đợi.
  item["avatarColor"] = item.value("avatarColor", "#5865F2").toString();
  if (item.value("avatarColor").toString().isEmpty()) {
    item["avatarColor"] = "#5865F2";
  }
  item["time"] = item.value("time", "Now").toString();
  item["timestampMs"] = item.value("timestampMs");
  item["message"] = item.value("message").toString();
  item["messageHtml"] = item.value("messageHtml").toString();
  item["replyAuthor"] = item.value("replyAuthor").toString();
  item["replyMessage"] = item.value("replyMessage").toString();
  item["replyMessageHtml"] = item.value("replyMessageHtml").toString();
  item["image"] = imageSource;
  item["imageLoading"] = imageLoading;
  item["imageLoadFailed"] = imageFailed;
  item["imageWidth"] = item.value("imageWidth", 0).toInt();
  item["imageHeight"] = item.value("imageHeight", 0).toInt();
  item["attachmentUrl"] = attachmentUrl;
  item["attachmentName"] = item.value("attachmentName").toString();
  item["attachmentIsImage"] = attachmentIsImage;
  item["attachments"] = displayAttachments;
  item["pending"] = item.value("pending").toBool();
  item["failed"] = item.value("failed").toBool();
  item["edited"] = item.value("edited").toBool();
  item["isGroupStart"] = item.value("isGroupStart", true).toBool();
  item["isGroupEnd"] = item.value("isGroupEnd", true).toBool();
  item["showAvatar"] = item.value("showAvatar", true).toBool();
  item["showUsername"] = item.value("showUsername", true).toBool();
  item["showTimestamp"] = item.value("showTimestamp", true).toBool();
  return item;
}

int ChatController::chatDataModelIndexForMessage(
    const QString &messageId) const {
  if (messageId.isEmpty()) {
    return -1;
  }

  for (int i = 0; i < m_chatDataModel->size(); ++i) {
    QVariantMap message = m_chatDataModel->value(i).toMap();
    if (message.value("id").toString() == messageId) {
      return i;
    }
  }
  return -1;
}

int ChatController::chatDataModelIndexForNonce(const QString &nonce) const {
  if (nonce.isEmpty()) {
    return -1;
  }

  for (int i = 0; i < m_chatDataModel->size(); ++i) {
    QVariantMap message = m_chatDataModel->value(i).toMap();
    if (message.value("nonce").toString() == nonce) {
      return i;
    }
  }
  return -1;
}

void ChatController::replaceGroupedMessage(const QVariantMap &message) {
  QVariantMap item = prepareMessageForModel(message);
  QString messageId = item.value("id").toString();
  int index = chatDataModelIndexForMessage(messageId);
  if (index < 0) {
    index = chatDataModelIndexForNonce(item.value("nonce").toString());
  }
  if (index < 0) {
    m_chatDataModel->append(item);
    return;
  }

  m_chatDataModel->replace(index, item);
  refreshModelGroupingAround(index);
}

bool ChatController::sameMessageIdentity(const QVariantMap &left,
                                         const QVariantMap &right) const {
  QString leftId = left.value("id").toString();
  QString rightId = right.value("id").toString();
  QString leftNonce = left.value("nonce").toString();
  QString rightNonce = right.value("nonce").toString();

  return leftId == rightId || (!leftNonce.isEmpty() && leftNonce == rightId) ||
         (!rightNonce.isEmpty() && rightNonce == leftId) ||
         (!leftNonce.isEmpty() && leftNonce == rightNonce);
}

void ChatController::refreshModelGroupingAround(int index) {
  QVariantList messages = currentMessages();
  for (int i = index - 1; i <= index + 1; ++i) {
    if (i < 0 || i >= m_chatDataModel->size() || i >= messages.size()) {
      continue;
    }

    QVariantMap rawMessage = messages.at(i).toMap();
    // messages.at(i) đến từ AppStore::messagesForChannel() - dữ liệu THÔ,
    // không mang theo "avatarSource" (field đó chỉ tồn tại trong
    // m_chatDataModel, được set bởi onChatAvatarChanged() khi avatar tải
    // xong). Nếu không copy nó qua trước khi gọi prepareMessageForModel(),
    // và avatar chưa kịp có trong cache tại đúng thời điểm này (đang tải
    // dở cho MỘT tin nhắn khác của cùng tác giả), avatar ĐÃ TẢI ĐÚNG của
    // dòng này bị xoá về rỗng khi replace() bên dưới chạy.
    QVariantMap existing = m_chatDataModel->value(i).toMap();
    QString existingAvatarSource = existing.value("avatarSource").toString();
    if (!existingAvatarSource.isEmpty()) {
      rawMessage["avatarSource"] = existingAvatarSource;
    }

    QVariantMap grouped = prepareMessageForModel(rawMessage);
    if (sameMessageIdentity(grouped, existing)) {
      m_chatDataModel->replace(i, grouped);
    }
  }
}

void ChatController::updateAttachmentImageInModel(const QString &url,
                                                  const QString &image,
                                                  bool loading, bool failed) {
  if (url.isEmpty()) {
    return;
  }

  for (int i = 0; i < m_chatDataModel->size(); ++i) {
    QVariantMap message = m_chatDataModel->value(i).toMap();
    bool topLevelChanged = message.value("attachmentUrl").toString() == url;
    if (topLevelChanged) {
      message["image"] = image;
      message["imageLoading"] = loading;
      message["imageLoadFailed"] = failed;
    }

    QVariantList attachments = message.value("attachments").toList();
    bool changed = false;
    for (int j = 0; j < attachments.size(); ++j) {
      QVariantMap attachment = attachments.at(j).toMap();
      bool matches = attachment.value("url").toString() == url ||
                     attachment.value("previewUrl").toString() == url;
      if (matches) {
        attachment["image"] = image;
        attachment["imageLoading"] = loading;
        attachment["imageLoadFailed"] = failed;
        attachments[j] = attachment;
        changed = true;
        if (j == 0 && message.value("attachmentUrl").toString() ==
                          attachment.value("url").toString()) {
          message["image"] = image;
          message["imageLoading"] = loading;
          message["imageLoadFailed"] = failed;
          topLevelChanged = true;
        }
      }
    }
    if (changed || topLevelChanged) {
      message["attachments"] = attachments;
      m_chatDataModel->replace(i, message);
    }
  }
}

QString ChatController::filePreviewSource(const QString &filePath) const {
  return QUrl::fromLocalFile(filePath).toString();
}

QString ChatController::fileContentType(const QString &filePath) const {
  QString suffix = QFileInfo(filePath).suffix().toLower();
  if (suffix == "png") {
    return "image/png";
  }
  if (suffix == "jpg" || suffix == "jpeg") {
    return "image/jpeg";
  }
  if (suffix == "gif") {
    return "image/gif";
  }
  if (suffix == "webp") {
    return "image/webp";
  }
  if (suffix == "bmp") {
    return "image/bmp";
  }
  return "application/octet-stream";
}

bool ChatController::isImageFile(const QString &filePath) const {
  return fileContentType(filePath).startsWith("image/");
}

QVariantMap
ChatController::pendingAttachmentMap(const QString &filePath) const {
  QFileInfo fileInfo(filePath);
  bool image = isImageFile(filePath);
  QVariantMap attachment;
  attachment["path"] = filePath;
  attachment["name"] = fileInfo.fileName();
  attachment["preview"] = image ? filePreviewSource(filePath) : QString();
  attachment["isImage"] = image;
  attachment["size"] = static_cast<int>(fileInfo.size());
  return attachment;
}

bool ChatController::appendPendingAttachment(const QString &filePath) {
  QString safePath = filePath.trimmed();
  if (safePath.startsWith("file://")) {
    safePath = QUrl(safePath).toLocalFile();
  }

  QFileInfo fileInfo(safePath);
  if (safePath.isEmpty() || !fileInfo.exists() || !fileInfo.isFile()) {
    m_pendingAttachmentError = tr("Attachment file not found");
    return false;
  }

  QString absolutePath = fileInfo.absoluteFilePath();
  if (m_pendingAttachmentPaths.contains(absolutePath)) {
    return false;
  }

  if (fileInfo.size() > kMaxPendingAttachmentBytes) {
    m_pendingAttachmentError = tr("Attachment is too large");
    return false;
  }

  if (m_pendingAttachmentPaths.size() >= kMaxPendingAttachments) {
    m_pendingAttachmentError = tr("Maximum 10 attachments");
    return false;
  }

  m_pendingAttachmentPaths.append(absolutePath);
  m_pendingAttachmentError.clear();
  return true;
}

void ChatController::rebuildPendingAttachmentsModel() {
  m_pendingAttachmentsModel->clear();
  for (int i = 0; i < m_pendingAttachmentPaths.size(); ++i) {
    QVariantMap attachment =
        pendingAttachmentMap(m_pendingAttachmentPaths.at(i));
    attachment["index"] = i;
    m_pendingAttachmentsModel->append(attachment);
  }
}

void ChatController::syncPrimaryPendingAttachment() {
  if (m_pendingAttachmentPaths.isEmpty()) {
    m_pendingAttachmentPath.clear();
    m_pendingAttachmentName.clear();
    m_pendingAttachmentPreview.clear();
    m_pendingAttachmentIsImage = false;
    return;
  }

  m_pendingAttachmentPath = m_pendingAttachmentPaths.first();
  QFileInfo fileInfo(m_pendingAttachmentPath);
  if (m_pendingAttachmentPaths.size() == 1) {
    m_pendingAttachmentName = fileInfo.fileName();
  } else {
    m_pendingAttachmentName =
        tr("%1 files").arg(m_pendingAttachmentPaths.size());
  }
  m_pendingAttachmentIsImage = isImageFile(m_pendingAttachmentPath);
  m_pendingAttachmentPreview = m_pendingAttachmentIsImage
                                   ? filePreviewSource(m_pendingAttachmentPath)
                                   : QString();
}

bool ChatController::isRemoteImageUrl(const QString &url) const {
  QString safeUrl = url.trimmed().toLower();
  return safeUrl.startsWith("https://") || safeUrl.startsWith("http://");
}

QString ChatController::attachmentImageCachePath(const QString &url) const {
  QByteArray hash =
      QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1).toHex();
  QString suffix = QFileInfo(QUrl(url).path()).suffix().toLower();
  if (suffix.isEmpty()) {
    suffix = "img";
  }

  QDir dir(QDir::homePath());
  return dir.absoluteFilePath(QString("cache/chat-image-cache/%1.%2")
                                  .arg(QString::fromLatin1(hash))
                                  .arg(suffix));
}

void ChatController::ensureAttachmentImageWorker() {
  if (m_imageWorker != 0) {
    return;
  }

  m_imageThread = new QThread(this);
  m_imageWorker = new AttachmentImageCacheWorker();
  m_imageWorker->moveToThread(m_imageThread);

  connect(m_imageThread, SIGNAL(finished()), m_imageWorker,
          SLOT(deleteLater()));
  connect(m_imageWorker, SIGNAL(imageCached(QString, QString)), this,
          SLOT(onAttachmentImageCached(QString, QString)));
  connect(m_imageWorker, SIGNAL(imageFailed(QString)), this,
          SLOT(onAttachmentImageFailed(QString)));

  m_imageThread->start();
}
