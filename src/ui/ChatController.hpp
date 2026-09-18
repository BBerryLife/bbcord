#ifndef ChatController_HPP_
#define ChatController_HPP_

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <bb/cascades/ArrayDataModel>
#include <bb/cascades/DataModel>

class AppStore;
class AttachmentImageCacheWorker;
class DiscordClient;
class QThread;

class ChatController : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString currentChannelId READ currentChannelId NOTIFY
                 currentChannelChanged)
  Q_PROPERTY(
      QString currentGuildId READ currentGuildId NOTIFY currentChannelChanged)
  Q_PROPERTY(QString currentChannelName READ currentChannelName NOTIFY
                 currentChannelChanged)
  Q_PROPERTY(bool hasPendingAttachment READ hasPendingAttachment NOTIFY
                 pendingAttachmentChanged)
  Q_PROPERTY(QString pendingAttachmentName READ pendingAttachmentName NOTIFY
                 pendingAttachmentChanged)
  Q_PROPERTY(QString pendingAttachmentPreview READ pendingAttachmentPreview
                 NOTIFY pendingAttachmentChanged)
  Q_PROPERTY(bool pendingAttachmentIsImage READ pendingAttachmentIsImage NOTIFY
                 pendingAttachmentChanged)
  Q_PROPERTY(QString pendingAttachmentError READ pendingAttachmentError NOTIFY
                 pendingAttachmentChanged)
  Q_PROPERTY(bb::cascades::DataModel *pendingAttachmentsModel READ
                 pendingAttachmentsModel NOTIFY pendingAttachmentChanged)
  Q_PROPERTY(bb::cascades::DataModel *chatDataModel READ chatDataModel NOTIFY
                 chatDataModelChanged)

public:
  explicit ChatController(DiscordClient *client, AppStore *store,
                          QObject *parent = 0);
  ~ChatController();

  QString currentChannelId() const;
  QString currentGuildId() const;
  QString currentChannelName() const;
  bool hasPendingAttachment() const;
  QString pendingAttachmentName() const;
  QString pendingAttachmentPreview() const;
  bool pendingAttachmentIsImage() const;
  QString pendingAttachmentError() const;
  bb::cascades::DataModel *pendingAttachmentsModel() const;
  bb::cascades::DataModel *chatDataModel() const;

  Q_INVOKABLE void openChannel(const QString &channelId, const QString &guildId,
                               const QString &channelName);
  Q_INVOKABLE void closeChannel();
  Q_INVOKABLE QVariantList currentMessages() const;
  Q_INVOKABLE QVariantList messagesForChannel(const QString &channelId) const;
  Q_INVOKABLE bool isInitialLoaded() const;
  Q_INVOKABLE bool isLoadingInitial() const;
  Q_INVOKABLE bool isLoadingBefore() const;
  Q_INVOKABLE bool hasMoreBefore() const;
  Q_INVOKABLE QString oldestMessageId() const;
  Q_INVOKABLE QString newestMessageId() const;
  // Fix: for "jump to the original message" when tapping a reply-quote
  // box (see MessageBubble.qml's replyBox tap handling / ChatCard.qml's
  // scrollToMessage()) - thin public wrapper around the existing
  // private chatDataModelIndexForMessage(), which was already used
  // internally for message updates/edits but had no QML-callable
  // entry point. Returns -1 if the message isn't currently loaded in
  // chatDataModel (e.g. it's further back than what's been fetched) -
  // callers should treat that as "can't jump there yet" rather than an
  // error.
  Q_INVOKABLE int indexForMessage(const QString &messageId) const;

  Q_INVOKABLE void requestInitialMessages();
  Q_INVOKABLE void requestOlderMessages();
  Q_INVOKABLE bool setPendingAttachment(const QString &filePath);
  Q_INVOKABLE int addPendingAttachments(const QVariantList &filePaths);
  Q_INVOKABLE void removePendingAttachment(int index);
  Q_INVOKABLE void clearPendingAttachment();
  Q_INVOKABLE QString sendMessage(const QString &content,
                                  const QString &replyMessageId,
                                  const QString &replyAuthor,
                                  const QString &replyMessage);
  Q_INVOKABLE void editMessage(const QString &messageId,
                               const QString &content);
  Q_INVOKABLE void deleteMessage(const QString &messageId);
  Q_INVOKABLE void markPendingFailed(const QString &messageId);
  Q_INVOKABLE void copyText(const QString &text);
  Q_INVOKABLE void openAttachment(const QString &url);
  Q_INVOKABLE QString avatarSource(const QString &userId,
                                   const QString &avatarHash);
  Q_INVOKABLE QString cachedImageSource(const QString &url);
  Q_INVOKABLE void requestCachedImage(const QString &url);
  Q_INVOKABLE void cancelCachedImage(const QString &url);

Q_SIGNALS:
  void currentChannelChanged();
  void pendingAttachmentChanged();
  void chatDataModelChanged();
  void attachmentImageCached(const QString &url, const QString &imageSource);
  void attachmentImageFailed(const QString &url);
  void initialMessagesRequested(const QString &channelId,
                                const QString &guildId);
  void olderMessagesRequested(const QString &channelId,
                              const QString &beforeMessageId);
  void sendMessageRequested(const QString &channelId, const QString &content,
                            const QString &nonce, const QString &replyMessageId,
                            const QStringList &attachmentPaths);
  void editMessageRequested(const QString &channelId, const QString &messageId,
                            const QString &content);
  void deleteMessageRequested(const QString &channelId,
                              const QString &messageId);

private Q_SLOTS:
  void clearMediaCacheState();
  void onCurrentChannelMessagesChanged();
  void onChatMessagesReset(const QString &channelId,
                           const QVariantList &messages);
  void onChatMessagesPrepended(const QString &channelId,
                               const QVariantList &messages);
  void onChatMessagesBatched(const QString &channelId,
                             const QVariantList &messages);
  void onChatMessageAdded(const QString &channelId, const QVariantMap &message);
  void onChatMessageUpdated(const QString &channelId,
                            const QVariantMap &message);
  void onChatMessageDeleted(const QString &channelId, const QString &messageId);
  void onChatAvatarChanged(const QString &userId, const QString &avatarSource);
  void onAttachmentImageCached(const QString &url, const QString &path);
  void onAttachmentImageFailed(const QString &url);

private:
  QString safeCurrentChannelId() const;
  QString buildPendingMessageId() const;
  QVariantMap findCachedMessage(const QString &messageId) const;
  QString filePreviewSource(const QString &filePath) const;
  QString fileContentType(const QString &filePath) const;
  bool isImageFile(const QString &filePath) const;
  QVariantMap pendingAttachmentMap(const QString &filePath) const;
  bool appendPendingAttachment(const QString &filePath);
  void rebuildPendingAttachmentsModel();
  void syncPrimaryPendingAttachment();
  bool isRemoteImageUrl(const QString &url) const;
  QString attachmentImageCachePath(const QString &url) const;
  void ensureAttachmentImageWorker();
  void rebuildChatDataModel();
  void replaceChatDataModel(const QVariantList &messages);
  void syncChatDataModel(const QVariantList &messages);
  int chatDataModelIndexForNonce(const QString &nonce) const;
  void replaceGroupedMessage(const QVariantMap &message);
  QVariantMap prepareMessageForModel(const QVariantMap &message);
  int chatDataModelIndexForMessage(const QString &messageId) const;
  bool sameMessageIdentity(const QVariantMap &left,
                           const QVariantMap &right) const;
  void refreshModelGroupingAround(int index);
  void updateAttachmentImageInModel(const QString &url, const QString &image,
                                    bool loading, bool failed);

  DiscordClient *m_client;
  AppStore *m_store;
  bb::cascades::ArrayDataModel *m_chatDataModel;
  bb::cascades::ArrayDataModel *m_pendingAttachmentsModel;
  QThread *m_imageThread;
  AttachmentImageCacheWorker *m_imageWorker;
  QHash<QString, QString> m_cachedAttachmentImages;
  QSet<QString> m_loadingAttachmentImages;
  QString m_currentChannelId;
  QString m_currentGuildId;
  QString m_currentChannelName;
  QStringList m_pendingAttachmentPaths;
  QString m_pendingAttachmentPath;
  QString m_pendingAttachmentName;
  QString m_pendingAttachmentPreview;
  QString m_pendingAttachmentError;
  bool m_pendingAttachmentIsImage;
};

#endif /* ChatController_HPP_ */
