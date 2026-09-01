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
  ChannelMessagesRequest,
  SendMessageRequest,
  UploadMessageRequest,
  EditMessageRequest,
  DeleteMessageRequest,
  AvatarRequest,
  GuildIconRequest
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
  // Retries the login step that most recently failed with a CAPTCHA
  // challenge (password login or MFA), attaching the token obtained from
  // solving the hCaptcha widget shown in a WebView. captchaRequired()
  // reports which step needs it via requestKind ("password" or "mfa").
  void submitCaptchaKey(const QString &captchaKey);
  void fetchGuilds(const QString &token, int limit, const QString &afterId);
  void fetchDmChannels(const QString &token, int limit, const QString &afterId);
  void fetchGuildChannels(const QString &token, const QString &guildId,
                          int limit, const QString &afterId);
  void fetchChannelMessages(const QString &token, const QString &channelId,
                            int limit, const QString &beforeMessageId);
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
  void loginSucceeded(const QVariantMap &user);
  void loginFailed(const QString &message);
  void mfaRequired(const QString &ticket, const QString &loginInstanceId);
  // Emitted when Discord rejects a password-login or MFA request with a
  // CAPTCHA challenge. sitekey/rqdata/rqtoken come straight from Discord's
  // response and are exactly what the hCaptcha JS widget needs; requestKind
  // is "password" or "mfa" so the UI (and submitCaptchaKey()) know which
  // request to retry once the challenge is solved.
  void captchaRequired(const QString &requestKind, const QString &sitekey,
                       const QString &rqdata, const QString &rqtoken);
  void guildsLoaded(const QVariantList &guilds);
  void dmChannelsLoaded(const QVariantList &channels);
  void guildChannelsLoaded(const QString &guildId,
                           const QVariantList &channels);
  void channelMessagesLoaded(const QString &channelId,
                             const QString &beforeMessageId,
                             const QVariantList &messages);
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
  void failWithMessage(const QString &message);
  void failDataRequest(const QString &message);
  void failChatRequest(const QString &message);
  // If parsedBody is a CAPTCHA-required response, saves a replayable copy
  // of the currently in-flight password-login/MFA request, emits
  // captchaRequired() with the sitekey/rqdata/rqtoken the WebView needs,
  // finishes the current request as a non-error, and returns true (the
  // caller must not also call failWithMessage()/etc.). Returns false for
  // any other response so the caller proceeds with its normal handling.
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
  // The most recent hCaptcha token obtained from the WebView challenge
  // (see captchaRequired()/submitCaptchaKey()). Sent as X-Captcha-Key on
  // the password-login and MFA requests once set; cleared after each
  // attempt since a token is single-use.
  QString m_captchaKey;
  // The exact request that was in flight when Discord demanded a CAPTCHA,
  // so submitCaptchaKey() can replay it unchanged (same ticket/email/etc.)
  // with the solved token attached, instead of the UI having to re-collect
  // the password or TOTP code from the user.
  RestRequest m_pendingCaptchaRequest;
  bool m_hasPendingCaptchaRequest;
  QList<RestRequest> m_requestQueue;
  bool m_isProcessing;
  bool m_requestSent;
  bool m_finished;
};

#endif /* RestClient_HPP_ */
