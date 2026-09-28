#include "../RestClient.hpp"

#include "../DiscordUtils.hpp"

#include <bb/data/JsonDataAccess>

extern "C" {
#include "mongoose.h"
}

namespace {
// /auth/login and /auth/mfa/totp take small JSON payloads; reuse bb::data::JsonDataAccess as the message-sending code does.
QByteArray buildAuthJsonBody(const QVariantMap &data) {
  bb::data::JsonDataAccess json;
  QByteArray body;
  json.saveToBuffer(data, &body);
  return body;
}
} // namespace

void DiscordRestClient::loginWithToken(const QString &token) {
  RestRequest request;
  request.token = token.trimmed();
  if (request.token.isEmpty()) {
    emit loginFailed("Token is empty");
    return;
  }

  request.type = LoginRequest;
  enqueueRequest(request);
}

void DiscordRestClient::loginWithPassword(const QString &email,
                                          const QString &password) {
  RestRequest request;
  request.loginEmail = email.trimmed();
  request.loginPassword = password;
  if (request.loginEmail.isEmpty() || request.loginPassword.isEmpty()) {
    emit loginFailed("Email/phone number and password are required");
    return;
  }

  request.type = PasswordLoginRequest;
  enqueueRequest(request);
}

void DiscordRestClient::submitMfaCode(const QString &ticket,
                                      const QString &loginInstanceId,
                                      const QString &code) {
  RestRequest request;
  request.mfaTicket = ticket.trimmed();
  request.mfaLoginInstanceId = loginInstanceId.trimmed();
  request.mfaCode = code.trimmed();
  if (request.mfaTicket.isEmpty() || request.mfaCode.isEmpty()) {
    emit loginFailed("Verification code is required");
    return;
  }

  request.type = MfaTotpRequest;
  enqueueRequest(request);
}

void DiscordRestClient::submitCaptchaKey(const QString &captchaKey) {
  QString trimmedKey = captchaKey.trimmed();
  if (trimmedKey.isEmpty()) {
    emit loginFailed("CAPTCHA was not completed");
    return;
  }

  if (!m_hasPendingCaptchaRequest) {
    emit loginFailed("No login attempt is waiting for a CAPTCHA");
    return;
  }

  RestRequest request = m_pendingCaptchaRequest;
  request.captchaKey = trimmedKey;
  m_pendingCaptchaRequest = RestRequest();
  m_hasPendingCaptchaRequest = false;
  enqueueRequest(request);
}

void DiscordRestClient::sendGetMeRequest(struct mg_connection *connection) {
  if (m_requestType != LoginRequest || connection == NULL || m_requestSent) {
    return;
  }

  m_requestSent = true;

  QByteArray tokenBytes = m_token.toUtf8();
  QByteArray pathBytes = apiRequestPath("/api/v9/users/@me").toUtf8();
  QByteArray hostBytes = hostHeader(apiBaseUrl()).toUtf8();
  QByteArray userAgent = DiscordUtils::desktopUserAgent();
  QByteArray superProperties = DiscordUtils::superPropertiesHeader();
  mg_printf(connection,
            "GET %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Authorization: %s\r\n"
            "User-Agent: %s\r\n"
            "%s"
            "Accept: application/json\r\n"
            "Connection: keep-alive\r\n\r\n",
            pathBytes.constData(), hostBytes.constData(),
            tokenBytes.constData(), userAgent.constData(),
            superProperties.constData());
}

void DiscordRestClient::sendFingerprintRequest(
    struct mg_connection *connection) {
  if (m_requestType != PasswordLoginRequest || connection == NULL ||
      m_requestSent) {
    return;
  }

  m_requestSent = true;
  m_awaitingFingerprint = true;

  // Best-effort anonymous fingerprint, sent as X-Fingerprint on the login request to
  // make a CAPTCHA less likely. Optional: on failure the login proceeds anyway.
  QByteArray pathBytes =
      apiRequestPath("/api/v9/experiments?with_guild_experiments=true")
          .toUtf8();
  QByteArray hostBytes = hostHeader(apiBaseUrl()).toUtf8();
  QByteArray userAgent = DiscordUtils::desktopUserAgent();
  QByteArray superProperties = DiscordUtils::superPropertiesHeader();
  mg_printf(connection,
            "GET %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: %s\r\n"
            "%s"
            "Accept: application/json\r\n"
            "Connection: keep-alive\r\n\r\n",
            pathBytes.constData(), hostBytes.constData(),
            userAgent.constData(), superProperties.constData());
}

void DiscordRestClient::sendPasswordLoginRequest(
    struct mg_connection *connection) {
  if (m_requestType != PasswordLoginRequest || connection == NULL ||
      m_requestSent) {
    return;
  }

  m_requestSent = true;

  QVariantMap payload;
  payload["login"] = m_loginEmail;
  payload["password"] = m_loginPassword;
  payload["undelete"] = false;
  QByteArray bodyBytes = buildAuthJsonBody(payload);

  // The plaintext password is not wiped here: a CAPTCHA retry needs it (copied into
  // m_pendingCaptchaRequest by tryHandleCaptcha()). It is wiped in finishRequest().

  QByteArray pathBytes = apiRequestPath("/api/v9/auth/login").toUtf8();
  QByteArray hostBytes = hostHeader(apiBaseUrl()).toUtf8();
  QByteArray userAgent = DiscordUtils::desktopUserAgent();
  QByteArray superProperties = DiscordUtils::superPropertiesHeader();

  // Temporary: log the decoded X-Super-Properties JSON to tell a malformed payload
  // from a bad login body on a 400 "Invalid Form Body". Remove once login is reliable.
  qDebug() << "[discord-rest] super properties header"
           << superProperties.trimmed();
  QByteArray fingerprintHeader =
      m_fingerprint.isEmpty()
          ? QByteArray()
          : ("X-Fingerprint: " + m_fingerprint.toUtf8() + "\r\n");
  // Attached only on a retry after the user solved a CAPTCHA (see captchaRequired()/
  // submitCaptchaKey()); cleared after every attempt so a stale key is never resent.
  QByteArray captchaHeader =
      m_captchaKey.isEmpty()
          ? QByteArray()
          : ("X-Captcha-Key: " + m_captchaKey.toUtf8() + "\r\n");

  mg_printf(connection,
            "POST %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: %s\r\n"
            "%s"
            "%s"
            "%s"
            "Accept: application/json\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %d\r\n"
            "Connection: keep-alive\r\n\r\n",
            pathBytes.constData(), hostBytes.constData(),
            userAgent.constData(), superProperties.constData(),
            fingerprintHeader.constData(), captchaHeader.constData(),
            static_cast<int>(bodyBytes.size()));
  mg_send(connection, bodyBytes.constData(),
          static_cast<size_t>(bodyBytes.size()));
}

void DiscordRestClient::sendMfaTotpRequest(struct mg_connection *connection) {
  if (m_requestType != MfaTotpRequest || connection == NULL ||
      m_requestSent) {
    return;
  }

  m_requestSent = true;

  QVariantMap payload;
  payload["code"] = m_mfaCode;
  payload["ticket"] = m_mfaTicket;
  if (!m_mfaLoginInstanceId.isEmpty()) {
    payload["login_instance_id"] = m_mfaLoginInstanceId;
  }
  QByteArray bodyBytes = buildAuthJsonBody(payload);

  QByteArray pathBytes = apiRequestPath("/api/v9/auth/mfa/totp").toUtf8();
  QByteArray hostBytes = hostHeader(apiBaseUrl()).toUtf8();
  QByteArray userAgent = DiscordUtils::desktopUserAgent();
  QByteArray superProperties = DiscordUtils::superPropertiesHeader();
  QByteArray fingerprintHeader =
      m_fingerprint.isEmpty()
          ? QByteArray()
          : ("X-Fingerprint: " + m_fingerprint.toUtf8() + "\r\n");
  QByteArray captchaHeader =
      m_captchaKey.isEmpty()
          ? QByteArray()
          : ("X-Captcha-Key: " + m_captchaKey.toUtf8() + "\r\n");

  mg_printf(connection,
            "POST %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: %s\r\n"
            "%s"
            "%s"
            "%s"
            "Accept: application/json\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %d\r\n"
            "Connection: keep-alive\r\n\r\n",
            pathBytes.constData(), hostBytes.constData(),
            userAgent.constData(), superProperties.constData(),
            fingerprintHeader.constData(), captchaHeader.constData(),
            static_cast<int>(bodyBytes.size()));
  mg_send(connection, bodyBytes.constData(),
          static_cast<size_t>(bodyBytes.size()));
}

void DiscordRestClient::succeedWithUser(const QVariantMap &user) {
  if (m_finished) {
    return;
  }

  finishRequest();
  // m_token is set by now (login response or loginWithToken()) and is not cleared by
  // finishRequest(). Passed explicitly because Client::m_token was never set after a
  // password+MFA login, which made connectGateway() fail with "Discord token is empty".
  emit loginSucceeded(user, m_token);
  processNextRequest();
}
