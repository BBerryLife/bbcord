#include "RestClient.hpp"

#include "JsonParser.hpp"

#include <QByteArray>
#include <QDebug>
#include <QSettings>
#include <QUrl>

extern "C" {
#include "mongoose.h"
}

#include <string.h>

namespace {
const char *kDiscordApiHost = "discord.com";
const char *kDiscordApiUrl = "https://discord.com";
const char *kDiscordApiSettingsKey = "discord/apiUrl";
const char *kDiscordCdnSettingsKey = "discord/cdnUrl";
const char *kOfficialApiBaseUrl = "https://discord.com/api/v9/";
const char *kOfficialCdnBaseUrl = "https://cdn.discordapp.com/";
const int kPollIntervalMs = 10;
const int kRequestTimeoutTicks = 600;
const int kKeepAliveIdleTimeoutTicks = 300;

QByteArray httpBodyToBytes(const struct mg_http_message *message) {
  return QByteArray(message->body.buf, static_cast<int>(message->body.len));
}

bool responseAllowsKeepAlive(struct mg_http_message *message) {
  struct mg_str closeValue = mg_str("close");
  struct mg_str *connection = mg_http_get_header(message, "Connection");
  return connection == NULL || mg_strcasecmp(*connection, closeValue) != 0;
}

QString restErrorMessage(int status) {
  switch (status) {
  case 401:
  case 403:
    return "Invalid Discord token";
  case 429:
    return "Discord REST rate limited";
  case 0:
    return "Discord REST response is invalid";
  default:
    return QString("Discord REST error %1").arg(status);
  }
}

QString avatarErrorMessage(int status) {
  if (status == 0) {
    return "Discord avatar response is invalid";
  }

  return QString("Discord avatar error %1").arg(status);
}

QString dataErrorMessage(const QString &name, int status) {
  if (status == 401 || status == 403) {
    return "Invalid Discord token";
  }

  if (status == 429) {
    return "Discord REST rate limited";
  }

  return QString("Discord %1 error %2").arg(name).arg(status);
}

QString authErrorMessage(int status, const QVariantMap &parsedBody,
                         const QString &fallback) {
  QString message = parsedBody.value("message").toString().trimmed();
  if (!message.isEmpty()) {
    return message;
  }

  if (status == 429) {
    return "Discord REST rate limited";
  }

  if (!fallback.isEmpty()) {
    return fallback;
  }

  return restErrorMessage(status);
}

} // namespace

DiscordRestClient::DiscordRestClient(QObject *parent)
    : QObject(parent), m_connection(NULL), m_timerId(0), m_pollTicks(0),
      m_idleTicks(0), m_requestType(NoRequest), m_awaitingFingerprint(false),
      m_isProcessing(false), m_requestSent(false), m_finished(true) {
  m_mgr = new mg_mgr;
  mg_mgr_init(m_mgr);
  mg_log_set(MG_LL_NONE);
}

DiscordRestClient::~DiscordRestClient() {
  cancel();
  mg_mgr_free(m_mgr);
  delete m_mgr;
}

void DiscordRestClient::cancel() {
  m_requestQueue.clear();
  if (m_connection != NULL) {
    m_connection->fn_data = NULL;
    if (!m_connection->is_closing) {
      m_connection->is_closing = 1;
    }
  }
  m_connection = NULL;
  m_requestType = NoRequest;
  m_requestSent = false;
  m_finished = true;
  m_pollTicks = 0;
  m_idleTicks = 0;
  m_requestPath.clear();
  m_requestMethod.clear();
  m_requestBody.clear();
  m_guildId.clear();
  m_channelId.clear();
  m_messageId.clear();
  m_beforeMessageId.clear();
  m_nonce.clear();
  m_avatarUserId.clear();
  m_avatarHash.clear();
  m_iconGuildId.clear();
  m_iconHash.clear();
  m_outputPath.clear();
  m_loginEmail.clear();
  m_loginPassword.clear();
  m_mfaTicket.clear();
  m_mfaLoginInstanceId.clear();
  m_mfaCode.clear();
  m_fingerprint.clear();
  m_awaitingFingerprint = false;
  m_isProcessing = false;
  m_connectionUrl.clear();
  stopTimerIfIdle();
}

void DiscordRestClient::timerEvent(QTimerEvent *event) {
  Q_UNUSED(event);

  if (m_connection == NULL) {
    stopTimerIfIdle();
    return;
  }

  mg_mgr_poll(m_mgr, 0);

  if (m_connection != NULL && m_finished) {
    ++m_idleTicks;
    if (m_idleTicks > kKeepAliveIdleTimeoutTicks) {
      m_connection->fn_data = NULL;
      if (!m_connection->is_closing) {
        m_connection->is_closing = 1;
      }
      m_connection = NULL;
      stopTimerIfIdle();
    }
  } else if (m_connection != NULL && !m_finished) {
    ++m_pollTicks;
    if (m_pollTicks > kRequestTimeoutTicks) {
      failWithMessage("Discord REST timeout");
    }
  } else {
    stopTimerIfIdle();
  }
}

void DiscordRestClient::eventHandler(struct mg_connection *connection,
                                     int event, void *eventData) {
  DiscordRestClient *client =
      static_cast<DiscordRestClient *>(connection->fn_data);
  if (client != NULL) {
    client->handleEvent(connection, event, eventData);
  }
}

void DiscordRestClient::enqueueRequest(const RestRequest &request) {
  m_requestQueue.append(request);
  processNextRequest();
}

QString DiscordRestClient::apiBaseUrl() const {
  QSettings settings;
  QString url = settings.value(kDiscordApiSettingsKey, kOfficialApiBaseUrl)
                    .toString()
                    .trimmed();
  return url.isEmpty() ? QString::fromLatin1(kOfficialApiBaseUrl) : url;
}

QString DiscordRestClient::cdnBaseUrl() const {
  QSettings settings;
  QString url = settings.value(kDiscordCdnSettingsKey, kOfficialCdnBaseUrl)
                    .toString()
                    .trimmed();
  return url.isEmpty() ? QString::fromLatin1(kOfficialCdnBaseUrl) : url;
}

QString DiscordRestClient::connectionUrl(const QString &url) const {
  QUrl parsed(url);
  if (!parsed.isValid() || parsed.scheme().isEmpty() ||
      parsed.host().isEmpty()) {
    return QString::fromLatin1(kDiscordApiUrl);
  }

  QString result = parsed.scheme() + "://" + parsed.host();
  if (parsed.port() > 0) {
    result += QString(":%1").arg(parsed.port());
  }
  return result;
}

QString DiscordRestClient::hostHeader(const QString &url) const {
  QUrl parsed(url);
  if (!parsed.isValid() || parsed.host().isEmpty()) {
    return QString::fromLatin1(kDiscordApiHost);
  }

  QString host = parsed.host();
  if (parsed.port() > 0) {
    host += QString(":%1").arg(parsed.port());
  }
  return host;
}

QString DiscordRestClient::apiRequestPath(const QString &requestPath) const {
  QUrl parsed(apiBaseUrl());
  QString basePath = parsed.path();
  if (basePath.endsWith('/')) {
    basePath.chop(1);
  }
  if (basePath.isEmpty()) {
    basePath = "/api/v9";
  }

  QString path = requestPath;
  if (path.startsWith("/api/v9")) {
    path = path.mid(QString("/api/v9").length());
  }
  if (!path.startsWith('/')) {
    path.prepend('/');
  }
  return basePath + path;
}

QString DiscordRestClient::cdnRequestPath(const QString &requestPath) const {
  QUrl parsed(cdnBaseUrl());
  QString basePath = parsed.path();
  if (basePath.endsWith('/')) {
    basePath.chop(1);
  }

  QString path = requestPath;
  if (!path.startsWith('/')) {
    path.prepend('/');
  }
  return basePath + path;
}

void DiscordRestClient::processNextRequest() {
  if (m_isProcessing || m_requestQueue.isEmpty()) {
    return;
  }

  RestRequest restRequest = m_requestQueue.takeFirst();
  QString url = connectionUrl((restRequest.type == AvatarRequest ||
                               restRequest.type == GuildIconRequest)
                                  ? cdnBaseUrl()
                                  : apiBaseUrl());
  bool reuseConnection = m_connection != NULL && !m_connection->is_closing &&
                         m_connectionUrl == url;
  if (m_connection != NULL && !reuseConnection) {
    m_connection->fn_data = NULL;
    if (!m_connection->is_closing) {
      m_connection->is_closing = 1;
    }
    m_connection = NULL;
    m_connectionUrl.clear();
  }
  m_isProcessing = true;
  m_requestType = restRequest.type;
  m_token = restRequest.token;
  m_requestPath = restRequest.requestPath;
  m_requestMethod = restRequest.requestMethod;
  m_requestBody = restRequest.requestBody;
  m_contentType = restRequest.contentType;
  m_guildId = restRequest.guildId;
  m_channelId = restRequest.channelId;
  m_messageId = restRequest.messageId;
  m_beforeMessageId = restRequest.beforeMessageId;
  m_nonce = restRequest.nonce;
  m_avatarUserId = restRequest.avatarUserId;
  m_avatarHash = restRequest.avatarHash;
  m_iconGuildId = restRequest.iconGuildId;
  m_iconHash = restRequest.iconHash;
  m_outputPath = restRequest.outputPath;
  m_loginEmail = restRequest.loginEmail;
  m_loginPassword = restRequest.loginPassword;
  m_mfaTicket = restRequest.mfaTicket;
  m_mfaLoginInstanceId = restRequest.mfaLoginInstanceId;
  m_mfaCode = restRequest.mfaCode;
  m_requestSent = false;
  m_finished = false;
  m_pollTicks = 0;
  m_idleTicks = 0;

  if (reuseConnection) {
    sendCurrentRequest(m_connection);
    startTimerIfNeeded();
    return;
  }

  QByteArray urlBytes = url.toUtf8();
  m_connection = mg_http_connect(m_mgr, urlBytes.constData(),
                                 DiscordRestClient::eventHandler, this);
  if (m_connection != NULL) {
    m_connectionUrl = url;
  }

  if (m_connection == NULL) {
    QString message;
    if (m_requestType == LoginRequest) {
      message = "Could not create Discord REST connection";
    } else if (m_requestType == PasswordLoginRequest ||
               m_requestType == MfaTotpRequest) {
      message = "Could not create Discord login connection";
    } else if (m_requestType == GuildsRequest) {
      message = "Could not create Discord guilds connection";
    } else if (m_requestType == DmChannelsRequest) {
      message = "Could not create Discord DM connection";
    } else if (m_requestType == GuildChannelsRequest) {
      message = "Could not create Discord channel connection";
    } else if (m_requestType == ChannelMessagesRequest) {
      message = "Could not create Discord messages connection";
    } else if (m_requestType == SendMessageRequest ||
               m_requestType == UploadMessageRequest) {
      message = "Could not create Discord send connection";
    } else if (m_requestType == EditMessageRequest) {
      message = "Could not create Discord edit connection";
    } else if (m_requestType == DeleteMessageRequest) {
      message = "Could not create Discord delete connection";
    } else if (m_requestType == AvatarRequest) {
      message = "Could not create Discord avatar connection";
    } else if (m_requestType == GuildIconRequest) {
      message = "Could not create Discord icon connection";
    } else {
      message = "Could not create Discord REST connection";
    }

    if (m_requestType == LoginRequest || m_requestType == PasswordLoginRequest ||
        m_requestType == MfaTotpRequest) {
      failWithMessage(message);
    } else if (m_requestType == ChannelMessagesRequest ||
               m_requestType == SendMessageRequest ||
               m_requestType == UploadMessageRequest ||
               m_requestType == EditMessageRequest ||
               m_requestType == DeleteMessageRequest) {
      failChatRequest(message);
    } else if (m_requestType == AvatarRequest) {
      QString avatarUserId = m_avatarUserId;
      finishRequest();
      emit avatarDownloadFailed(avatarUserId, message);
      processNextRequest();
    } else if (m_requestType == GuildIconRequest) {
      QString guildId = m_iconGuildId;
      finishRequest();
      emit guildIconDownloadFailed(guildId, message);
      processNextRequest();
    } else {
      failDataRequest(message);
    }
    return;
  }

  startTimerIfNeeded();
}

void DiscordRestClient::sendCurrentRequest(struct mg_connection *connection) {
  if (m_requestType == AvatarRequest) {
    sendAvatarRequest(connection);
  } else if (m_requestType == GuildIconRequest) {
    sendGuildIconRequest(connection);
  } else if (m_requestType == LoginRequest) {
    sendGetMeRequest(connection);
  } else if (m_requestType == PasswordLoginRequest) {
    if (m_fingerprint.isEmpty() && !m_awaitingFingerprint) {
      sendFingerprintRequest(connection);
    } else {
      sendPasswordLoginRequest(connection);
    }
  } else if (m_requestType == MfaTotpRequest) {
    sendMfaTotpRequest(connection);
  } else {
    sendApiRequest(connection);
  }
}

void DiscordRestClient::handleEvent(struct mg_connection *connection, int event,
                                    void *eventData) {
  switch (event) {
  case MG_EV_CONNECT:
    if (connection->is_tls) {
      struct mg_tls_opts opts;
      memset(&opts, 0, sizeof(opts));
      QByteArray tlsHost = hostHeader((m_requestType == AvatarRequest ||
                                       m_requestType == GuildIconRequest)
                                          ? cdnBaseUrl()
                                          : apiBaseUrl())
                               .toUtf8();
      opts.name = mg_str(tlsHost.constData());
      opts.skip_verification = true;
      mg_tls_init(connection, &opts);
    }
    break;

  case MG_EV_HTTP_HDRS:
    break;

  case MG_EV_TLS_HS:
    sendCurrentRequest(connection);
    break;

  case MG_EV_HTTP_MSG: {
    struct mg_http_message *message =
        static_cast<struct mg_http_message *>(eventData);
    int status = mg_http_status(message);
    QByteArray body = httpBodyToBytes(message);
    bool keepConnectionAlive = responseAllowsKeepAlive(message);

    if (m_requestType == AvatarRequest || m_requestType == GuildIconRequest) {
      qDebug() << "[discord-rest] avatar status" << status;
      if (status == 200) {
        QByteArray outputPathBytes = m_outputPath.toUtf8();
        if (!mg_file_write(&mg_fs_posix, outputPathBytes.constData(),
                           body.constData(),
                           static_cast<size_t>(body.size()))) {
          QString outputPath = m_outputPath;
          QString avatarUserId = m_avatarUserId;
          QString guildId = m_iconGuildId;
          bool isGuildIcon = m_requestType == GuildIconRequest;
          finishRequest(keepConnectionAlive);
          if (isGuildIcon) {
            emit guildIconDownloadFailed(
                guildId, QString("Could not save icon: %1").arg(outputPath));
          } else {
            emit avatarDownloadFailed(
                avatarUserId,
                QString("Could not save avatar: %1").arg(outputPath));
          }
          processNextRequest();
          break;
        }

        QString outputPath = m_outputPath;
        QString avatarUserId = m_avatarUserId;
        QString guildId = m_iconGuildId;
        bool isGuildIcon = m_requestType == GuildIconRequest;
        finishRequest(keepConnectionAlive);
        if (isGuildIcon) {
          emit guildIconDownloaded(guildId, outputPath);
        } else {
          emit avatarDownloaded(avatarUserId, outputPath);
        }
        processNextRequest();
        break;
      }

      QString errorMessage = avatarErrorMessage(status);
      QString avatarUserId = m_avatarUserId;
      QString guildId = m_iconGuildId;
      bool isGuildIcon = m_requestType == GuildIconRequest;
      finishRequest(keepConnectionAlive);
      if (isGuildIcon) {
        emit guildIconDownloadFailed(guildId, errorMessage);
      } else {
        emit avatarDownloadFailed(avatarUserId, errorMessage);
      }
      processNextRequest();
      break;
    }

    if (m_requestType == ChannelMessagesRequest) {
      qDebug() << "[discord-rest] channel messages status" << status;
      if (status == 200) {
        QString parseError;
        QVariantList messages =
            DiscordJsonParser::parseArray(body, &parseError);
        if (!parseError.isEmpty()) {
          failChatRequest(
              QString("Discord REST JSON error: %1").arg(parseError));
          break;
        }

        QString channelId = m_channelId;
        QString beforeMessageId = m_beforeMessageId;
        finishRequest(keepConnectionAlive);
        emit channelMessagesLoaded(channelId, beforeMessageId, messages);
        processNextRequest();
        break;
      }

      failChatRequest(dataErrorMessage("messages", status));
      break;
    }

    if (m_requestType == SendMessageRequest ||
        m_requestType == UploadMessageRequest ||
        m_requestType == EditMessageRequest) {
      bool sending = m_requestType == SendMessageRequest ||
                     m_requestType == UploadMessageRequest;
      qDebug() << "[discord-rest]" << (sending ? "send" : "edit") << "status"
               << status;
      if ((sending && status == 200) || (!sending && status == 200)) {
        QString parseError;
        QVariantMap message = DiscordJsonParser::parseObject(body, &parseError);
        if (!parseError.isEmpty()) {
          failChatRequest(
              QString("Discord REST JSON error: %1").arg(parseError));
          break;
        }

        QString channelId = m_channelId;
        QString nonce = m_nonce;
        RequestType finishedType = m_requestType;
        finishRequest(keepConnectionAlive);
        if (finishedType == SendMessageRequest ||
            finishedType == UploadMessageRequest) {
          emit channelMessageSent(channelId, nonce, message);
        } else {
          emit channelMessageEdited(channelId, message);
        }
        processNextRequest();
        break;
      }

      failChatRequest(
          dataErrorMessage(sending ? "send message" : "edit message", status));
      break;
    }

    if (m_requestType == DeleteMessageRequest) {
      qDebug() << "[discord-rest] delete status" << status;
      if (status == 204 || status == 200) {
        QString channelId = m_channelId;
        QString messageId = m_messageId;
        finishRequest(keepConnectionAlive);
        emit channelMessageDeleted(channelId, messageId);
        processNextRequest();
        break;
      }

      failChatRequest(dataErrorMessage("delete message", status));
      break;
    }

    if (m_requestType == GuildsRequest || m_requestType == DmChannelsRequest ||
        m_requestType == GuildChannelsRequest) {
      QString requestName =
          m_requestType == GuildsRequest
              ? "guilds"
              : (m_requestType == DmChannelsRequest ? "DM channels"
                                                    : "guild channels");
      qDebug() << "[discord-rest]" << requestName << "status" << status;
      if (status == 200) {
        QString parseError;
        QVariantList items = DiscordJsonParser::parseArray(body, &parseError);
        if (!parseError.isEmpty()) {
          failDataRequest(
              QString("Discord REST JSON error: %1").arg(parseError));
          break;
        }

        QString guildId = m_guildId;
        RequestType finishedType = m_requestType;
        finishRequest(keepConnectionAlive);
        if (finishedType == GuildsRequest) {
          emit guildsLoaded(items);
        } else if (finishedType == DmChannelsRequest) {
          emit dmChannelsLoaded(items);
        } else {
          emit guildChannelsLoaded(guildId, items);
        }
        processNextRequest();
        break;
      }

      failDataRequest(dataErrorMessage(requestName, status));
      break;
    }

    if (m_requestType == PasswordLoginRequest && m_awaitingFingerprint) {
      qDebug() << "[discord-rest] fingerprint status" << status;
      m_awaitingFingerprint = false;

      if (status == 200) {
        QString parseError;
        QVariantMap response = DiscordJsonParser::parseObject(body, &parseError);
        if (parseError.isEmpty()) {
          QString fingerprint = response.value("fingerprint").toString();
          if (!fingerprint.isEmpty()) {
            m_fingerprint = fingerprint;
          }
        }
      }

      // Proceed to the real login request either way; the fingerprint is
      // only used to make a CAPTCHA challenge less likely, it is not
      // required for the login itself.
      m_requestSent = false;
      sendPasswordLoginRequest(connection);
      break;
    }

    if (m_requestType == PasswordLoginRequest) {
      qDebug() << "[discord-rest] password login status" << status;
      QString parseError;
      QVariantMap response = DiscordJsonParser::parseObject(body, &parseError);

      if (status == 200 && parseError.isEmpty()) {
        QString token = response.value("token").toString();
        if (!token.isEmpty()) {
          // We already have a token; reuse the existing token-login flow to
          // fetch the current user on this same (keep-alive) connection.
          m_token = token;
          m_requestType = LoginRequest;
          m_requestSent = false;
          sendGetMeRequest(connection);
          break;
        }

        if (response.value("mfa").toBool()) {
          QString ticket = response.value("ticket").toString();
          QString loginInstanceId =
              response.value("login_instance_id").toString();
          if (!ticket.isEmpty()) {
            finishRequest(keepConnectionAlive);
            emit mfaRequired(ticket, loginInstanceId);
            processNextRequest();
            break;
          }
        }

        failWithMessage("Unexpected Discord login response");
        break;
      }

      failWithMessage(authErrorMessage(
          status, response, "Invalid email/phone number or password"));
      break;
    }

    if (m_requestType == MfaTotpRequest) {
      qDebug() << "[discord-rest] mfa totp status" << status;
      QString parseError;
      QVariantMap response = DiscordJsonParser::parseObject(body, &parseError);

      if (status == 200 && parseError.isEmpty()) {
        QString token = response.value("token").toString();
        if (!token.isEmpty()) {
          m_token = token;
          m_requestType = LoginRequest;
          m_requestSent = false;
          sendGetMeRequest(connection);
          break;
        }

        failWithMessage("Unexpected Discord verification response");
        break;
      }

      failWithMessage(
          authErrorMessage(status, response, "Invalid verification code"));
      break;
    }

    qDebug() << "[discord-rest] /users/@me status" << status;

    if (status == 200) {
      QString parseError;
      QVariantMap user = DiscordJsonParser::parseObject(body, &parseError);
      if (!parseError.isEmpty()) {
        failWithMessage(QString("Discord REST JSON error: %1").arg(parseError));
        break;
      }

      succeedWithUser(user);
      break;
    }

    failWithMessage(restErrorMessage(status));
    break;
  }

  case MG_EV_ERROR: {
    QString message = QString::fromUtf8(
        eventData ? static_cast<char *>(eventData) : "Discord REST error");
    failWithMessage(message);
    break;
  }

  case MG_EV_CLOSE:
    if (m_connection == connection) {
      m_connection = NULL;
      if (!m_finished) {
        failWithMessage("Discord REST connection closed");
      } else {
        stopTimerIfIdle();
      }
    }
    break;
  }
}

void DiscordRestClient::startTimerIfNeeded() {
  if (m_timerId == 0) {
    m_timerId = startTimer(kPollIntervalMs);
  }
}

void DiscordRestClient::stopTimerIfIdle() {
  if (m_timerId != 0 && m_connection == NULL) {
    killTimer(m_timerId);
    m_timerId = 0;
  }
}

void DiscordRestClient::finishRequest(bool keepConnectionAlive) {
  if (m_connection != NULL) {
    if (keepConnectionAlive && !m_connection->is_closing) {
      m_idleTicks = 0;
    } else {
      m_connection->fn_data = NULL;
      if (!m_connection->is_closing) {
        m_connection->is_closing = 1;
      }
      m_connection = NULL;
    }
  }
  m_requestType = NoRequest;
  m_requestSent = false;
  m_finished = true;
  m_isProcessing = false;
  m_pollTicks = 0;
  m_requestPath.clear();
  m_requestMethod.clear();
  m_requestBody.clear();
  m_contentType.clear();
  m_guildId.clear();
  m_channelId.clear();
  m_messageId.clear();
  m_beforeMessageId.clear();
  m_nonce.clear();
  m_avatarUserId.clear();
  m_avatarHash.clear();
  m_iconGuildId.clear();
  m_iconHash.clear();
  m_outputPath.clear();
  m_loginEmail.clear();
  m_loginPassword.clear();
  m_mfaTicket.clear();
  m_mfaLoginInstanceId.clear();
  m_mfaCode.clear();
  if (keepConnectionAlive && m_connection != NULL) {
    startTimerIfNeeded();
  }
  stopTimerIfIdle();
}

void DiscordRestClient::failWithMessage(const QString &message) {
  if (m_finished) {
    return;
  }

  QString safeMessage = message.trimmed();
  if (safeMessage.isEmpty()) {
    safeMessage = "Discord REST error";
  }

  finishRequest();
  emit loginFailed(safeMessage);
  processNextRequest();
}

void DiscordRestClient::failDataRequest(const QString &message) {
  if (m_finished) {
    return;
  }

  QString safeMessage = message.trimmed();
  if (safeMessage.isEmpty()) {
    safeMessage = "Discord REST error";
  }

  finishRequest();
  emit requestFailed(safeMessage);
  processNextRequest();
}

void DiscordRestClient::failChatRequest(const QString &message) {
  if (m_finished) {
    return;
  }

  QString safeMessage = message.trimmed();
  if (safeMessage.isEmpty()) {
    safeMessage = "Discord REST error";
  }

  QString operation;
  if (m_requestType == ChannelMessagesRequest) {
    operation = "messages";
  } else if (m_requestType == SendMessageRequest ||
             m_requestType == UploadMessageRequest) {
    operation = "send";
  } else if (m_requestType == EditMessageRequest) {
    operation = "edit";
  } else if (m_requestType == DeleteMessageRequest) {
    operation = "delete";
  }

  QString channelId = m_channelId;
  QString nonce = m_nonce;
  finishRequest();
  emit chatRequestFailed(operation, channelId, nonce, safeMessage);
  processNextRequest();
}
