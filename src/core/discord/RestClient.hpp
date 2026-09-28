#ifndef RestClient_HPP_
#define RestClient_HPP_

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

struct mg_mgr;
struct mg_connection;

enum RequestType {
  NoRequest,
  LoginRequest,
  PasswordLoginRequest,
  MfaTotpRequest,
  GuildsRequest,
  DmChannelsRequest,
  GuildChannelsRequest,
  ActiveThreadsRequest,
  ArchivedThreadsRequest,
  ChannelMessagesRequest,
  ChannelInfoRequest,
  SendMessageRequest,
  UploadMessageRequest,
  EditMessageRequest,
  DeleteMessageRequest,
  AvatarRequest,
  GuildIconRequest,
  SelfGuildMemberRequest
};

struct RestRequest {
  RestRequest() : type(NoRequest) {}

  RequestType type;
  QString requestPath;
  QString requestMethod;
  QByteArray requestBody;
  QString contentType;
  QString token;
  QString guildId;
  QString channelId;
  QString messageId;
  QString beforeMessageId;
  QString nonce;
  QString avatarUserId;
  QString avatarHash;
  QString iconGuildId;
  QString iconHash;
  QString outputPath;
  QString loginEmail;
  QString loginPassword;
  QString mfaTicket;
  QString mfaLoginInstanceId;
  QString mfaCode;
  QString captchaKey;
};

class DiscordRestClient : public QObject {
  Q_OBJECT

public:
  explicit DiscordRestClient(QObject *parent = 0);
  virtual ~DiscordRestClient();

  void loginWithToken(const QString &token);
  void loginWithPassword(const QString &email, const QString &password);
  void submitMfaCode(const QString &ticket, const QString &loginInstanceId,
                     const QString &code);
  // Retries the login step that failed with a CAPTCHA (password or MFA) using the
  // hCaptcha token from the WebView. captchaRequired() reports the step via requestKind.
  void submitCaptchaKey(const QString &captchaKey);
  void fetchGuilds(const QString &token, int limit, const QString &afterId);
  void fetchDmChannels(const QString &token, int limit, const QString &afterId);
  void fetchGuildChannels(const QString &token, const QString &guildId,
                          int limit, const QString &afterId);
  // READY.guilds has no "members" field on user-token gateways, so the user's own
  // roles (needed by PermissionUtils) are fetched via REST. Called after
  // loadGuildChannels() and on GUILD_MEMBER_UPDATE for the current user.
  // userId must be the numeric snowflake (AppStore::currentUserId()), not "@me":
  // Discord rejects "@me" on this route with a 400.
  void fetchSelfGuildMember(const QString &token, const QString &guildId,
                            const QString &userId);
  // Threads are not in /guilds/{id}/channels. Uses the channel-level endpoint
  // (/channels/{id}/threads/active); the guild-level one is bot-only
  // (see Channel.cpp::fetchActiveThreads()).
  void fetchActiveThreads(const QString &token, const QString &channelId);
  // Auto-archived threads are not in active threads. beforeCursor (empty = first
  // page) is the id of the oldest loaded thread, for pagination.
  void fetchArchivedThreads(const QString &token, const QString &channelId,
                            const QString &beforeCursor);
  void fetchChannelMessages(const QString &token, const QString &channelId,
                            int limit, const QString &beforeMessageId);
  // GET /channels/{id}: a single channel with "guild_id" and "name". Only a fallback
  // for Hub-invoked opens when nothing is cached (cold start); DMs have no "guild_id".
  void fetchChannelInfo(const QString &token, const QString &channelId);
  void sendChannelMessage(const QString &token, const QString &channelId,
                          const QString &content, const QString &nonce,
                          const QString &replyMessageId,
                          const QStringList &attachmentPaths);
  void editChannelMessage(const QString &token, const QString &channelId,
                          const QString &messageId, const QString &content);
  void deleteChannelMessage(const QString &token, const QString &channelId,
                            const QString &messageId);
  void downloadAvatar(const QString &userId, const QString &avatarHash,
                      const QString &outputPath);
  void downloadGuildIcon(const QString &guildId, const QString &iconHash,
                         const QString &outputPath);
  void removeQueuedChannelMessageRequestsExcept(const QString &channelId);
  void cancel();

Q_SIGNALS:
  void loginSucceeded(const QVariantMap &user, const QString &token);
  void loginFailed(const QString &message);
  void mfaRequired(const QString &ticket, const QString &loginInstanceId);
  // Emitted when Discord answers a password/MFA request with a CAPTCHA challenge.
  // sitekey/rqdata/rqtoken feed the hCaptcha widget; requestKind is "password" or "mfa".
  void captchaRequired(const QString &requestKind, const QString &sitekey,
                       const QString &rqdata, const QString &rqtoken);
  void guildsLoaded(const QVariantList &guilds);
  void dmChannelsLoaded(const QVariantList &channels);
  void guildChannelsLoaded(const QString &guildId,
                           const QVariantList &channels);
  // "roles" is the member's current role-id array in this guild (raw
  // Discord strings) - see fetchSelfGuildMember()/PermissionUtils.
  void selfGuildMemberLoaded(const QString &guildId,
                             const QStringList &roleIds);
  // The "threads" field of the threads/active response, each still a raw channel
  // object (type 10/11/12) to be mapped by ItemMapper::guildChannelToItem().
  // Keyed by the parent channelId.
  void activeThreadsLoaded(const QString &channelId,
                           const QVariantList &threads);
  // Extra "hasMore" says whether an older page exists. Not merged into
  // m_channelThreadsByParentId; archived threads stay separate.
  void archivedThreadsLoaded(const QString &channelId,
                             const QVariantList &threads, bool hasMore);
  void channelMessagesLoaded(const QString &channelId,
                             const QString &beforeMessageId,
                             const QVariantList &messages);
  // channelName can be empty (DMs have no "name"); callers should fall back
  // rather than treat it as an error.
  void channelInfoLoaded(const QString &channelId, const QString &guildId,
                         const QString &channelName);
  void channelInfoLoadFailed(const QString &channelId, const QString &message);
  void channelMessageSent(const QString &channelId, const QString &nonce,
                          const QVariantMap &message);
  void channelMessageEdited(const QString &channelId,
                            const QVariantMap &message);
  void channelMessageDeleted(const QString &channelId,
                             const QString &messageId);
  void chatRequestFailed(const QString &operation, const QString &channelId,
                         const QString &nonce, const QString &message);
  void requestFailed(const QString &message);
  void avatarDownloaded(const QString &userId, const QString &localPath);
  void avatarDownloadFailed(const QString &userId, const QString &message);
  void guildIconDownloaded(const QString &guildId, const QString &localPath);
  void guildIconDownloadFailed(const QString &guildId, const QString &message);

protected:
  virtual void timerEvent(QTimerEvent *event);

private:
  static void eventHandler(struct mg_connection *connection, int event,
                           void *eventData);
  void handleEvent(struct mg_connection *connection, int event,
                   void *eventData);
  void enqueueRequest(const RestRequest &request);
  void processNextRequest();
  void sendCurrentRequest(struct mg_connection *connection);
  void startTimerIfNeeded();
  void stopTimerIfIdle();
  void finishRequest(bool keepConnectionAlive = false);
  void requeueCurrentRequestForRetry();
  void failWithMessage(const QString &message);
  void failDataRequest(const QString &message);
  void failChatRequest(const QString &message);
  // If parsedBody is a CAPTCHA response: saves a replayable copy of the in-flight
  // request, emits captchaRequired(), finishes the request as a non-error and returns
  // true (caller must not fail it). Returns false for other responses.
  bool tryHandleCaptcha(bool keepConnectionAlive,
                        const QVariantMap &parsedBody);
  void succeedWithUser(const QVariantMap &user);
  void sendGetMeRequest(struct mg_connection *connection);
  void sendFingerprintRequest(struct mg_connection *connection);
  void sendPasswordLoginRequest(struct mg_connection *connection);
  void sendMfaTotpRequest(struct mg_connection *connection);
  void sendApiRequest(struct mg_connection *connection);
  void sendAvatarRequest(struct mg_connection *connection);
  void sendGuildIconRequest(struct mg_connection *connection);
  QString apiBaseUrl() const;
  QString cdnBaseUrl() const;
  QString connectionUrl(const QString &url) const;
  QString hostHeader(const QString &url) const;
  QString apiRequestPath(const QString &requestPath) const;
  QString cdnRequestPath(const QString &requestPath) const;
  QByteArray buildMultipartMessageBody(
      const QString &content, const QString &nonce, const QString &channelId,
      const QString &replyMessageId, const QStringList &attachmentPaths,
      QString *contentType, QString *errorMessage) const;

  mg_mgr *m_mgr;
  mg_connection *m_connection;
  int m_timerId;
  int m_pollTicks;
  int m_idleTicks;
  // Remaining retries before "Discord REST timeout" reaches the UI. Reset only in
  // enqueueRequest(), since processNextRequest()/finishRequest() also run on the retry path.
  int m_timeoutRetriesLeft;
  RequestType m_requestType;
  QString m_token;
  QString m_requestPath;
  QString m_requestMethod;
  QByteArray m_requestBody;
  QString m_guildId;
  QString m_channelId;
  QString m_messageId;
  QString m_beforeMessageId;
  QString m_nonce;
  QString m_avatarUserId;
  QString m_avatarHash;
  QString m_iconGuildId;
  QString m_iconHash;
  QString m_outputPath;
  QString m_contentType;
  QString m_connectionUrl;
  QString m_loginEmail;
  QString m_loginPassword;
  QString m_mfaTicket;
  QString m_mfaLoginInstanceId;
  QString m_mfaCode;
  QString m_fingerprint;
  bool m_awaitingFingerprint;
  // hCaptcha token from the WebView, sent as X-Captcha-Key; cleared after each attempt (single-use).
  QString m_captchaKey;
  // The request in flight when the CAPTCHA was demanded, replayed by submitCaptchaKey().
  RestRequest m_pendingCaptchaRequest;
  bool m_hasPendingCaptchaRequest;
  QList<RestRequest> m_requestQueue;
  bool m_isProcessing;
  bool m_requestSent;
  bool m_finished;
};

#endif /* RestClient_HPP_ */
