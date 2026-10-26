#include "../RestClient.hpp"

#include <bb/data/JsonDataAccess>

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

namespace {
const qint64 kMaxAttachmentBytes = 8 * 1024 * 1024;
const int kMaxAttachments = 10;

QByteArray buildJsonBody(const QVariantMap &data, QString *errorMessage) {
  bb::data::JsonDataAccess json;
  QByteArray body;
  json.saveToBuffer(data, &body);
  if (json.hasError()) {
    if (errorMessage != 0) {
      *errorMessage = json.error().errorMessage();
    }
    return QByteArray();
  }

  if (errorMessage != 0) {
    errorMessage->clear();
  }
  return body;
}
} // namespace

void DiscordRestClient::fetchChannelMessages(const QString &token,
                                             const QString &channelId,
                                             int limit,
                                             const QString &beforeMessageId) {
  RestRequest restRequest;
  restRequest.token = token.trimmed();
  restRequest.channelId = channelId.trimmed();
  restRequest.beforeMessageId = beforeMessageId.trimmed();
  if (restRequest.token.isEmpty() || restRequest.channelId.isEmpty()) {
    emit chatRequestFailed("messages", restRequest.channelId, QString(),
                           "Message request is empty");
    return;
  }

  if (limit <= 0) {
    limit = 50;
  } else if (limit > 100) {
    limit = 100;
  }

  restRequest.requestMethod = "GET";
  restRequest.requestPath = QString("/api/v9/channels/%1/messages?limit=%2")
                                .arg(restRequest.channelId)
                                .arg(limit);
  if (!restRequest.beforeMessageId.isEmpty()) {
    restRequest.requestPath +=
        QString("&before=%1").arg(restRequest.beforeMessageId);
  }

  restRequest.type = ChannelMessagesRequest;
  enqueueRequest(restRequest);
}

void DiscordRestClient::sendChannelMessage(const QString &token,
                                           const QString &channelId,
                                           const QString &content,
                                           const QString &nonce,
                                           const QString &replyMessageId,
                                           const QStringList &attachmentPaths) {
  RestRequest restRequest;
  restRequest.token = token.trimmed();
  restRequest.channelId = channelId.trimmed();
  restRequest.nonce = nonce.trimmed();
  QString safeContent = content.trimmed();
  QStringList safeAttachmentPaths;
  for (int i = 0; i < attachmentPaths.size(); ++i) {
    QString path = attachmentPaths.at(i).trimmed();
    if (!path.isEmpty()) {
      safeAttachmentPaths.append(path);
    }
  }
  if (restRequest.token.isEmpty() || restRequest.channelId.isEmpty() ||
      (safeContent.isEmpty() && safeAttachmentPaths.isEmpty()) ||
      restRequest.nonce.isEmpty()) {
    emit chatRequestFailed("send", restRequest.channelId, restRequest.nonce,
                           "Send message request is empty");
    return;
  }

  if (safeAttachmentPaths.isEmpty()) {
    QVariantMap body;
    body["content"] = safeContent;
    body["nonce"] = restRequest.nonce;
    body["tts"] = false;
    QString safeReplyMessageId = replyMessageId.trimmed();
    if (!safeReplyMessageId.isEmpty()) {
      QVariantMap reference;
      reference["channel_id"] = restRequest.channelId;
      reference["message_id"] = safeReplyMessageId;
      body["message_reference"] = reference;
    }

    QString jsonError;
    QByteArray jsonBody = buildJsonBody(body, &jsonError);
    restRequest.requestBody = jsonBody;
    if (!jsonError.isEmpty()) {
      emit chatRequestFailed(
          "send", restRequest.channelId, restRequest.nonce,
          QString("Discord REST JSON error: %1").arg(jsonError));
      return;
    }

    restRequest.contentType = "application/json";
    restRequest.type = SendMessageRequest;
  } else {
    QString multipartContentType;
    QString multipartError;
    restRequest.requestBody = buildMultipartMessageBody(
        safeContent, restRequest.nonce, restRequest.channelId, replyMessageId,
        safeAttachmentPaths, &multipartContentType, &multipartError);
    if (!multipartError.isEmpty()) {
      emit chatRequestFailed("send", restRequest.channelId, restRequest.nonce,
                             multipartError);
      return;
    }

    restRequest.contentType = multipartContentType;
    restRequest.type = UploadMessageRequest;
  }

  restRequest.requestMethod = "POST";
  restRequest.requestPath =
      QString("/api/v9/channels/%1/messages").arg(restRequest.channelId);
  enqueueRequest(restRequest);
}

void DiscordRestClient::editChannelMessage(const QString &token,
                                           const QString &channelId,
                                           const QString &messageId,
                                           const QString &content) {
  RestRequest restRequest;
  restRequest.token = token.trimmed();
  restRequest.channelId = channelId.trimmed();
  restRequest.messageId = messageId.trimmed();
  QString safeContent = content.trimmed();
  if (restRequest.token.isEmpty() || restRequest.channelId.isEmpty() ||
      restRequest.messageId.isEmpty() || safeContent.isEmpty()) {
    emit chatRequestFailed("edit", restRequest.channelId, QString(),
                           "Edit message request is empty");
    return;
  }

  QVariantMap body;
  body["content"] = safeContent;
  QString jsonError;
  QByteArray jsonBody = buildJsonBody(body, &jsonError);
  restRequest.requestBody = jsonBody;
  if (!jsonError.isEmpty()) {
    emit chatRequestFailed(
        "edit", restRequest.channelId, QString(),
        QString("Discord REST JSON error: %1").arg(jsonError));
    return;
  }

  restRequest.requestMethod = "PATCH";
  restRequest.requestPath = QString("/api/v9/channels/%1/messages/%2")
                                .arg(restRequest.channelId)
                                .arg(restRequest.messageId);
  restRequest.contentType = "application/json";
  restRequest.type = EditMessageRequest;
  enqueueRequest(restRequest);
}

void DiscordRestClient::deleteChannelMessage(const QString &token,
                                             const QString &channelId,
                                             const QString &messageId) {
  RestRequest request;
  request.token = token.trimmed();
  request.channelId = channelId.trimmed();
  request.messageId = messageId.trimmed();
  if (request.token.isEmpty() || request.channelId.isEmpty() ||
      request.messageId.isEmpty()) {
    emit chatRequestFailed("delete", request.channelId, QString(),
                           "Delete message request is empty");
    return;
  }

  request.requestMethod = "DELETE";
  request.requestPath = QString("/api/v9/channels/%1/messages/%2")
                            .arg(request.channelId)
                            .arg(request.messageId);
  request.type = DeleteMessageRequest;
  enqueueRequest(request);
}

void DiscordRestClient::removeQueuedChannelMessageRequestsExcept(
    const QString &channelId) {
  QString safeChannelId = channelId.trimmed();
  if (safeChannelId.isEmpty() || m_requestQueue.isEmpty()) {
    return;
  }

  for (int i = m_requestQueue.size() - 1; i >= 0; --i) {
    const RestRequest &request = m_requestQueue.at(i);
    if (request.type == ChannelMessagesRequest &&
        request.channelId != safeChannelId) {
      m_requestQueue.removeAt(i);
    }
  }
}

QByteArray DiscordRestClient::buildMultipartMessageBody(
    const QString &content, const QString &nonce, const QString &channelId,
    const QString &replyMessageId, const QStringList &attachmentPaths,
    QString *contentType, QString *errorMessage) const {
  if (attachmentPaths.isEmpty()) {
    if (errorMessage != 0) {
      *errorMessage = QString("No attachments selected");
    }
    return QByteArray();
  }

  if (attachmentPaths.size() > kMaxAttachments) {
    if (errorMessage != 0) {
      *errorMessage = QString("Maximum 10 attachments");
    }
    return QByteArray();
  }

  QList<QByteArray> fileBodies;
  QList<QFileInfo> fileInfos;
  for (int i = 0; i < attachmentPaths.size(); ++i) {
    QFileInfo fileInfo(attachmentPaths.at(i));
    QFile file(attachmentPaths.at(i));
    if (!file.open(QIODevice::ReadOnly)) {
      if (errorMessage != 0) {
        *errorMessage =
            QString("Could not open attachment: %1").arg(fileInfo.fileName());
      }
      return QByteArray();
    }

    if (file.size() > kMaxAttachmentBytes) {
      if (errorMessage != 0) {
        *errorMessage =
            QString("Attachment is too large: %1").arg(fileInfo.fileName());
      }
      return QByteArray();
    }

    fileBodies.append(file.readAll());
    fileInfos.append(fileInfo);
  }

  QVariantMap payload;
  payload["content"] = content;
  payload["nonce"] = nonce;
  payload["tts"] = false;
  QString safeReplyMessageId = replyMessageId.trimmed();
  if (!safeReplyMessageId.isEmpty()) {
    QVariantMap reference;
    reference["channel_id"] = channelId;
    reference["message_id"] = safeReplyMessageId;
    payload["message_reference"] = reference;
  }

  QVariantList attachments;
  for (int i = 0; i < fileInfos.size(); ++i) {
    QVariantMap attachment;
    attachment["id"] = QString::number(i);
    attachment["filename"] = fileInfos.at(i).fileName();
    attachments.append(attachment);
  }
  payload["attachments"] = attachments;

  QString jsonError;
  QByteArray payloadBytes = buildJsonBody(payload, &jsonError);
  if (!jsonError.isEmpty()) {
    if (errorMessage != 0) {
      *errorMessage = QString("Discord REST JSON error: %1").arg(jsonError);
    }
    return QByteArray();
  }

  QByteArray boundary =
      QString("----BBCord%1").arg(QDateTime::currentMSecsSinceEpoch()).toUtf8();
  QByteArray body;
  body.append("--" + boundary + "\r\n");
  body.append("Content-Disposition: form-data; name=\"payload_json\"\r\n");
  body.append("Content-Type: application/json\r\n\r\n");
  body.append(payloadBytes);
  for (int i = 0; i < fileBodies.size(); ++i) {
    body.append("\r\n--" + boundary + "\r\n");
    body.append("Content-Disposition: form-data; name=\"files[");
    body.append(QString::number(i).toUtf8());
    body.append("]\"; filename=\"");
    body.append(fileInfos.at(i).fileName().toUtf8());
    body.append("\"\r\n");
    body.append("Content-Type: application/octet-stream\r\n\r\n");
    body.append(fileBodies.at(i));
  }
  body.append("\r\n--" + boundary + "--\r\n");

  if (contentType != 0) {
    *contentType = QString("multipart/form-data; boundary=%1")
                       .arg(QString::fromUtf8(boundary));
  }
  if (errorMessage != 0) {
    errorMessage->clear();
  }
  return body;
}

void DiscordRestClient::sendTyping(const QString &token,
                                   const QString &channelId) {
  RestRequest request;
  request.token = token.trimmed();
  request.channelId = channelId.trimmed();
  if (request.token.isEmpty() || request.channelId.isEmpty()) {
    return;
  }

  // One queued typing ping per channel is enough.
  for (int i = 0; i < m_requestQueue.size(); ++i) {
    if (m_requestQueue.at(i).type == TypingRequest &&
        m_requestQueue.at(i).channelId == request.channelId) {
      return;
    }
  }

  request.requestMethod = "POST";
  request.requestPath =
      QString("/api/v9/channels/%1/typing").arg(request.channelId);
  request.type = TypingRequest;
  enqueueRequest(request);
}
