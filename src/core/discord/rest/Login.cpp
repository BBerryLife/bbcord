#include "../RestClient.hpp"

#include "../DiscordUtils.hpp"

#include <bb/data/JsonDataAccess>

extern "C" {
#include "mongoose.h"
}

namespace {
// Discord's /auth/login and /auth/mfa/totp endpoints accept small JSON
// payloads. Reuse bb::data::JsonDataAccess the same way the message-sending
// code does, since these payloads are simple and always serialize cleanly.
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

void DiscordRestClient::sendGetMeRequest(struct mg_connection *connection) {
  if (m_requestType != LoginRequest || connection == NULL || m_requestSent) {
    return;
  }

  m_requestSent = true;

  QByteArray tokenBytes = m_token.toUtf8();
  QByteArray pathBytes = apiRequestPath("/api/v9/users/@me").toUtf8();
  QByteArray hostBytes = hostHeader(apiBaseUrl()).toUtf8();
  QByteArray userAgent = DiscordUtils::desktopUserAgent();
  mg_printf(connection,
            "GET %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Authorization: %s\r\n"
            "User-Agent: %s\r\n"
            "Accept: application/json\r\n"
            "Connection: keep-alive\r\n\r\n",
            pathBytes.constData(), hostBytes.constData(),
            tokenBytes.constData(), userAgent.constData());
}

void DiscordRestClient::sendFingerprintRequest(
    struct mg_connection *connection) {
  if (m_requestType != PasswordLoginRequest || connection == NULL ||
      m_requestSent) {
    return;
  }

  m_requestSent = true;
  m_awaitingFingerprint = true;

  // Best-effort lookup of an anonymous fingerprint, sent back as
  // X-Fingerprint on the actual login request below. This mirrors what the
  // official/other unofficial clients do before a password login and makes
  // a CAPTCHA challenge less likely. It is optional: if this fails for any
  // reason we still proceed straight to the login request.
  QByteArray pathBytes =
      apiRequestPath("/api/v9/experiments?with_guild_experiments=true")
          .toUtf8();
  QByteArray hostBytes = hostHeader(apiBaseUrl()).toUtf8();
  QByteArray userAgent = DiscordUtils::desktopUserAgent();
  mg_printf(connection,
            "GET %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: %s\r\n"
            "Accept: application/json\r\n"
            "Connection: keep-alive\r\n\r\n",
            pathBytes.constData(), hostBytes.constData(),
            userAgent.constData());
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

  // The plaintext password only needs to live long enough to be serialized
  // above; drop it from memory as soon as the request body is built.
  m_loginPassword.fill(QLatin1Char('0'));
  m_loginPassword.clear();

  QByteArray pathBytes = apiRequestPath("/api/v9/auth/login").toUtf8();
  QByteArray hostBytes = hostHeader(apiBaseUrl()).toUtf8();
  QByteArray userAgent = DiscordUtils::desktopUserAgent();
  QByteArray fingerprintHeader =
      m_fingerprint.isEmpty()
          ? QByteArray()
          : ("X-Fingerprint: " + m_fingerprint.toUtf8() + "\r\n");

  mg_printf(connection,
            "POST %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: %s\r\n"
            "%s"
            "Accept: application/json\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %d\r\n"
            "Connection: keep-alive\r\n\r\n",
            pathBytes.constData(), hostBytes.constData(),
            userAgent.constData(), fingerprintHeader.constData(),
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
  QByteArray fingerprintHeader =
      m_fingerprint.isEmpty()
          ? QByteArray()
          : ("X-Fingerprint: " + m_fingerprint.toUtf8() + "\r\n");

  mg_printf(connection,
            "POST %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: %s\r\n"
            "%s"
            "Accept: application/json\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %d\r\n"
            "Connection: keep-alive\r\n\r\n",
            pathBytes.constData(), hostBytes.constData(),
            userAgent.constData(), fingerprintHeader.constData(),
            static_cast<int>(bodyBytes.size()));
  mg_send(connection, bodyBytes.constData(),
          static_cast<size_t>(bodyBytes.size()));
}

void DiscordRestClient::succeedWithUser(const QVariantMap &user) {
  if (m_finished) {
    return;
  }

  finishRequest();
  emit loginSucceeded(user);
  processNextRequest();
}
