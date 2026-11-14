#ifndef ChatController_HPP_
#define ChatController_HPP_

#include <QHash>
#include <QObject>
#include <QSet>
#include <QMap>
#include <QTimer>
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
  // "Alice is typing..." text for the open chat; empty when nobody is typing.
  Q_PROPERTY(QString typingText READ typingText NOTIFY typingTextChanged)
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
  QString typingText() const { return m_typingText; }
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
  // True when the open channel is a DM / group DM (no guild) whose history has loaded and is
  // empty, i.e. the next message would be the first one in that conversation. Discord's spam
  // filter is strictest on exactly that, so the UI warns before sending it.
  Q_INVOKABLE bool needsEmptyDmWarning() const;
  Q_INVOKABLE bool isLoadingInitial() const;
  Q_INVOKABLE bool isLoadingBefore() const;
  Q_INVOKABLE bool hasMoreBefore() const;
  Q_INVOKABLE QString oldestMessageId() const;
  Q_INVOKABLE QString newestMessageId() const;
  // Public wrapper around chatDataModelIndexForMessage() for the jump to a replied-to
  // message (see ChatCard.qml's scrollToMessage()). Returns -1 if the message is not
  // loaded; treat that as "cannot jump yet".
  Q_INVOKABLE int indexForMessage(const QString &messageId) const;

  Q_INVOKABLE void requestInitialMessages();
  Q_INVOKABLE void requestOlderMessages();
  Q_INVOKABLE bool setPendingAttachment(const QString &filePath);
  Q_INVOKABLE int addPendingAttachments(const QVariantList &filePaths);
  Q_INVOKABLE void removePendingAttachment(int index);
  Q_INVOKABLE void clearPendingAttachment();
  // Call while the user is composing; sends a throttled "typing" ping like the
  // official client does (roughly every 8 s while text is being entered).
  Q_INVOKABLE void notifyTyping();
  // Debounced read-state ack of the newest message in the open channel. Does nothing while
  // the app is not on screen. Called on new messages and when the app returns to the front.
  Q_INVOKABLE void scheduleAck();
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
  void typingTextChanged();
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
  void typingRequested(const QString &channelId);
  void ackRequested(const QString &channelId, const QString &messageId);
  void editMessageRequested(const QString &channelId, const QString &messageId,
                            const QString &content);
  void deleteMessageRequested(const QString &channelId,
                              const QString &messageId);

private Q_SLOTS:
  void clearMediaCacheState();
  void ackCurrentChannel();
  void onTypingStarted(const QString &channelId, const QString &userId,
                       const QString &displayName);
  void pruneTypers();
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
  // Member roles or guild role colors changed: recolor author names already in the model.
  void onMemberRolesChanged(const QString &guildId);
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
  // Author name color: the author's highest colored role, else the default text color.
  // Asks the client to fetch the member's roles when they are not cached yet.
  QString authorColorFor(const QString &guildId, const QString &authorId);
  // Server nickname if the author has one in this guild, else `baseName`. Asks the gateway
  // for the member when the nickname is not known yet (answer comes via memberNicksChanged()).
  QString authorNameFor(const QString &guildId, const QString &authorId,
                        const QString &baseName);
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
  qint64 m_lastTypingSentMs;
  QString m_lastTypingChannelId;
  QTimer *m_ackTimer;
  // Typing indicator: user id -> (name, expiry). A TYPING_START lasts ~10 s; it also ends
  // when that user's message arrives.
  struct Typer {
    QString name;
    qint64 untilMs;
  };
  void clearTypers();
  void removeTyper(const QString &userId);
  void rebuildTypingText();
  QString resolveTyperName(const QString &userId, const QString &name) const;
  QMap<QString, Typer> m_typers;
  QStringList m_typerOrder; // oldest first, so the text names people in arrival order
  QTimer *m_typingTimer;
  QString m_typingText;
  QHash<QString, QString> m_lastAckedByChannel;
};

#endif /* ChatController_HPP_ */
