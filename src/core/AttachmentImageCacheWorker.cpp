#include "AttachmentImageCacheWorker.hpp"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QTimerEvent>
#include <QUrl>

extern "C" {
#include "mongoose.h"
}

#include <string.h>

namespace {
const int kPollIntervalMs = 50;
const int kRequestTimeoutTicks = 300;

// See RestClient.cpp for why mongoose's 3-second default DNS timeout is
// raised here too. Attachment hosts vary (CDN, arbitrary embeds), so slow
// resolution is at least as likely here as on the main API connection.
const int kDnsTimeoutMs = 10000;

void sendImageRequest(struct mg_connection *connection,
                      ImageDownloadContext *context) {
  if (context->requestSent) {
    return;
  }

  QByteArray host = context->host.toUtf8();
  QByteArray path = context->path.toUtf8();
  mg_printf(connection,
            "GET %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: BBCord/1.0\r\n"
            "Accept: image/png,image/jpeg,image/*\r\n"
            "Connection: close\r\n\r\n",
            path.constData(), host.constData());
  context->requestSent = true;
}
} // namespace

AttachmentImageCacheWorker::AttachmentImageCacheWorker(QObject *parent)
    : QObject(parent), m_timerId(0) {
  m_mgr = new mg_mgr;
  mg_mgr_init(m_mgr);
  m_mgr->dnstimeout = kDnsTimeoutMs;
  mg_log_set(MG_LL_NONE);
}

AttachmentImageCacheWorker::~AttachmentImageCacheWorker() {
  cancelAll();
  mg_mgr_free(m_mgr);
  delete m_mgr;
}

void AttachmentImageCacheWorker::requestImage(const QString &url,
                                              const QString &path,
                                              qint64 maxBytes) {
  QString safeUrl = url.trimmed();
  QString safePath = path.trimmed();
  if (safeUrl.isEmpty() || safePath.isEmpty()) {
    emit imageFailed(safeUrl);
    return;
  }

  QFileInfo cachedFile(safePath);
  if (cachedFile.exists() && cachedFile.size() > 0) {
    emit imageCached(safeUrl, safePath);
    return;
  }

  QDir dir = cachedFile.dir();
  if (!dir.exists() && !dir.mkpath(".")) {
    emit imageFailed(safeUrl);
    return;
  }

  m_cancelledUrls.remove(safeUrl);

  QUrl parsed(safeUrl);
  QString host = parsed.host().trimmed();
  QString requestPath = parsed.encodedPath();
  QString query = parsed.encodedQuery();
  if (host.isEmpty() || requestPath.isEmpty()) {
    emit imageFailed(safeUrl);
    return;
  }

  if (!query.isEmpty()) {
    requestPath += "?" + query;
  }

  QString scheme = parsed.scheme().toLower();
  QString connectHost = host;
  if (parsed.port() > 0) {
    connectHost += QString(":%1").arg(parsed.port());
  }
  QByteArray connectUrl = QString("%1://%2")
                              .arg(scheme == "http" ? "http" : "https")
                              .arg(connectHost)
                              .toUtf8();

  ImageDownloadContext *context = new ImageDownloadContext;
  context->url = safeUrl;
  context->filePath = safePath;
  context->host = connectHost;
  context->path = requestPath;
  context->receivedBytes = 0;
  context->totalBytes = -1;
  context->maxBytes = maxBytes;
  context->status = 0;
  context->ticks = 0;
  context->requestSent = false;
  context->finished = false;
  context->failed = false;
  context->worker = this;

  struct mg_connection *connection =
      mg_http_connect(m_mgr, connectUrl.constData(),
                      AttachmentImageCacheWorker::imageEventHandler, context);
  if (connection == NULL) {
    delete context;
    emit imageFailed(safeUrl);
    return;
  }

  m_activeDownloads.insert(connection, context);
  startTimerIfNeeded();
}

void AttachmentImageCacheWorker::cancelImage(const QString &url) {
  QString safeUrl = url.trimmed();
  if (safeUrl.isEmpty()) {
    return;
  }

  m_cancelledUrls.insert(safeUrl);
  QList<struct mg_connection *> connections;
  QMap<struct mg_connection *, ImageDownloadContext *>::const_iterator it;
  for (it = m_activeDownloads.constBegin(); it != m_activeDownloads.constEnd();
       ++it) {
    if (it.value()->url == safeUrl) {
      connections.append(it.key());
    }
  }

  for (int i = 0; i < connections.size(); ++i) {
    finishDownload(connections.at(i), false);
  }
}

void AttachmentImageCacheWorker::cancelAll() {
  QList<struct mg_connection *> connections = m_activeDownloads.keys();
  for (int i = 0; i < connections.size(); ++i) {
    ImageDownloadContext *context = m_activeDownloads.value(connections.at(i));
    if (context != 0) {
      m_cancelledUrls.insert(context->url);
    }
    finishDownload(connections.at(i), false);
  }
  stopTimerIfIdle();
}

void AttachmentImageCacheWorker::onReadyRead() {}

void AttachmentImageCacheWorker::onFinished() {}

void AttachmentImageCacheWorker::timerEvent(QTimerEvent *event) {
  if (event != 0 && event->timerId() != m_timerId) {
    QObject::timerEvent(event);
    return;
  }

  if (m_activeDownloads.isEmpty()) {
    stopTimerIfIdle();
    return;
  }

  mg_mgr_poll(m_mgr, 0);

  QList<struct mg_connection *> timedOut;
  QMap<struct mg_connection *, ImageDownloadContext *>::iterator it;
  for (it = m_activeDownloads.begin(); it != m_activeDownloads.end(); ++it) {
    ImageDownloadContext *context = it.value();
    if (context != 0 && !context->finished) {
      ++context->ticks;
      if (context->ticks > kRequestTimeoutTicks) {
        timedOut.append(it.key());
      }
    }
  }

  for (int i = 0; i < timedOut.size(); ++i) {
    finishDownload(timedOut.at(i), false);
  }

  stopTimerIfIdle();
}

void AttachmentImageCacheWorker::startTimerIfNeeded() {
  if (m_timerId == 0) {
    m_timerId = startTimer(kPollIntervalMs);
  }
}

void AttachmentImageCacheWorker::stopTimerIfIdle() {
  if (m_timerId != 0 && m_activeDownloads.isEmpty()) {
    killTimer(m_timerId);
    m_timerId = 0;
  }
}

void AttachmentImageCacheWorker::finishDownload(
    struct mg_connection *connection, bool success) {
  ImageDownloadContext *context = m_activeDownloads.take(connection);
  if (context == 0) {
    return;
  }

  context->finished = true;
  if (connection != NULL) {
    connection->fn_data = NULL;
    connection->is_closing = 1;
  }

  bool wasCancelled = m_cancelledUrls.contains(context->url);
  bool ok =
      success && !context->failed && context->status == 200 &&
      !context->body.isEmpty() &&
      (context->maxBytes <= 0 || context->body.size() <= context->maxBytes) &&
      !wasCancelled;

  if (ok) {
    QFile file(context->filePath);
    if (file.open(QIODevice::WriteOnly)) {
      qint64 written = file.write(context->body);
      file.close();
      ok = written == context->body.size();
    } else {
      ok = false;
    }
  }

  if (ok) {
    emit imageCached(context->url, context->filePath);
  } else {
    QFile::remove(context->filePath);
    if (!wasCancelled) {
      emit imageFailed(context->url);
    }
  }

  delete context;
  stopTimerIfIdle();
}

void AttachmentImageCacheWorker::imageEventHandler(
    struct mg_connection *connection, int event, void *eventData) {
  ImageDownloadContext *context =
      static_cast<ImageDownloadContext *>(connection->fn_data);
  if (context == 0) {
    return;
  }

  switch (event) {
  case MG_EV_CONNECT:
    if (connection->is_tls) {
      QByteArray host = context->host.toUtf8();
      struct mg_tls_opts opts;
      memset(&opts, 0, sizeof(opts));
      opts.name = mg_str(host.constData());
      opts.skip_verification = true;
      mg_tls_init(connection, &opts);
    } else {
      sendImageRequest(connection, context);
    }
    break;

  case MG_EV_TLS_HS:
    sendImageRequest(connection, context);
    break;

  case MG_EV_READ:
    context->receivedBytes = static_cast<qint64>(connection->recv.len);
    emit context->worker->imageProgress(context->url, context->receivedBytes,
                                        context->totalBytes);
    break;

  case MG_EV_HTTP_HDRS: {
    struct mg_http_message *message =
        static_cast<struct mg_http_message *>(eventData);
    qint64 totalBytes = -1;
    struct mg_str *contentLength =
        mg_http_get_header(message, "Content-Length");
    if (contentLength != NULL && contentLength->len > 0) {
      totalBytes = QString::fromLatin1(contentLength->buf,
                                       static_cast<int>(contentLength->len))
                       .toLongLong();
    }
    context->totalBytes = totalBytes;
    emit context->worker->imageProgress(context->url, context->receivedBytes,
                                        context->totalBytes);
    break;
  }

  case MG_EV_HTTP_MSG: {
    struct mg_http_message *message =
        static_cast<struct mg_http_message *>(eventData);
    context->status = mg_http_status(message);
    if (context->status == 200 && message->body.len > 0) {
      context->body =
          QByteArray(message->body.buf, static_cast<int>(message->body.len));
      context->receivedBytes = static_cast<qint64>(message->body.len);
      context->totalBytes = static_cast<qint64>(message->body.len);
      emit context->worker->imageProgress(context->url, context->receivedBytes,
                                          context->totalBytes);
    }
    context->worker->finishDownload(connection, true);
    break;
  }

  case MG_EV_ERROR:
  case MG_EV_CLOSE:
    context->failed = true;
    context->worker->finishDownload(connection, false);
    break;

  default:
    break;
  }
}
