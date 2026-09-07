#include "AboutController.hpp"

#include <bb/ApplicationInfo>
#include <bb/data/JsonDataAccess>

#include <QDateTime>
#include <QTimerEvent>
#include <QVariant>

extern "C" {
#include "mongoose.h"
}

#include <string.h>

namespace {
const char *kGitHubApiHost = "api.github.com";
const char *kGitHubApiUrl = "https://api.github.com";
const char *kLatestReleasePath = "/repos/BBerryLife/bbcord/releases?per_page=1";
const int kPollIntervalMs = 10;
const int kRequestTimeoutTicks = 600;

QByteArray httpBodyToBytes(const struct mg_http_message *message) {
  return QByteArray(message->body.buf, static_cast<int>(message->body.len));
}
} // namespace

AboutController::AboutController(QObject *parent)
    : QObject(parent), m_mgr(new mg_mgr), m_connection(0), m_timerId(0),
      m_pollTicks(0), m_checking(false), m_checked(false), m_requestSent(false),
      m_updateAvailable(false), m_statusText(tr("Checking for update...")) {
  mg_mgr_init(m_mgr);
  mg_log_set(MG_LL_NONE);
}

AboutController::~AboutController() {
  finishRequest();
  mg_mgr_free(m_mgr);
  delete m_mgr;
}

bool AboutController::checking() const { return m_checking; }

bool AboutController::checked() const { return m_checked; }

bool AboutController::updateAvailable() const { return m_updateAvailable; }

QString AboutController::statusText() const { return m_statusText; }

QString AboutController::latestVersion() const { return m_latestVersion; }

QString AboutController::publishedAtText() const { return m_publishedAtText; }

QString AboutController::sizeText() const { return m_sizeText; }

QString AboutController::releaseUrl() const { return m_releaseUrl; }

QString AboutController::downloadUrl() const { return m_downloadUrl; }

void AboutController::checkForUpdates(bool force) {
  try {
    if (m_checking) {
      return;
    }

    if (m_checked && !force) {
      return;
    }

    finishRequest();

    m_checked = false;
    m_updateAvailable = false;
    m_requestSent = false;
    m_pollTicks = 0;
    m_statusText = tr("Checking for update...");
    m_latestVersion.clear();
    m_publishedAtText.clear();
    m_sizeText.clear();
    m_releaseUrl.clear();
    m_downloadUrl.clear();
    setChecking(true);

    m_connection = mg_http_connect(m_mgr, kGitHubApiUrl,
                                   AboutController::eventHandler, this);
    if (m_connection == 0) {
      setErrorState(tr("Could not create update connection"));
      return;
    }

    if (m_timerId == 0) {
      m_timerId = startTimer(kPollIntervalMs);
    }
  } catch (...) {
    setErrorState(tr("Unknown update check error"));
  }
}

void AboutController::timerEvent(QTimerEvent *event) {
  Q_UNUSED(event);

  try {
    if (m_connection == 0) {
      if (m_timerId != 0) {
        killTimer(m_timerId);
        m_timerId = 0;
      }
      return;
    }

    mg_mgr_poll(m_mgr, 0);
    ++m_pollTicks;
    if (m_pollTicks > kRequestTimeoutTicks) {
      setErrorState(tr("Update check timeout"));
    }
  } catch (...) {
    setErrorState(tr("Unknown update check error"));
  }
}

void AboutController::eventHandler(struct mg_connection *connection, int event,
                                   void *eventData) {
  AboutController *controller =
      static_cast<AboutController *>(connection->fn_data);
  if (controller != 0) {
    controller->handleEvent(connection, event, eventData);
  }
}

void AboutController::handleEvent(struct mg_connection *connection, int event,
                                  void *eventData) {
  try {
    switch (event) {
    case MG_EV_CONNECT:
      if (connection->is_tls) {
        struct mg_tls_opts opts;
        memset(&opts, 0, sizeof(opts));
        opts.name = mg_str(kGitHubApiHost);
        opts.skip_verification = true;
        mg_tls_init(connection, &opts);
      }
      break;

    case MG_EV_TLS_HS:
      sendUpdateRequest(connection);
      break;

    case MG_EV_HTTP_MSG: {
      struct mg_http_message *message =
          static_cast<struct mg_http_message *>(eventData);
      int status = mg_http_status(message);
      QByteArray body = httpBodyToBytes(message);
      finishRequest();
      if (status == 200) {
        parseResponse(body);
      } else {
        setErrorState(QString("GitHub update error %1").arg(status));
      }
      break;
    }

    case MG_EV_ERROR: {
      const char *error = static_cast<const char *>(eventData);
      QString message =
          error != 0 ? QString::fromUtf8(error) : tr("Update connection error");
      setErrorState(message);
      break;
    }

    case MG_EV_CLOSE:
      if (m_connection == connection) {
        m_connection = 0;
      }
      break;

    default:
      break;
    }
  } catch (...) {
    setErrorState(tr("Unknown update check error"));
  }
}

void AboutController::sendUpdateRequest(struct mg_connection *connection) {
  if (connection == 0 || m_requestSent) {
    return;
  }

  m_requestSent = true;
  mg_printf(connection,
            "GET %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: BBCord\r\n"
            "Accept: application/vnd.github+json\r\n"
            "Connection: close\r\n\r\n",
            kLatestReleasePath, kGitHubApiHost);
}

void AboutController::finishRequest() {
  if (m_timerId != 0) {
    killTimer(m_timerId);
    m_timerId = 0;
  }

  if (m_connection != 0) {
    m_connection->fn_data = 0;
    if (!m_connection->is_closing) {
      m_connection->is_closing = 1;
    }
    m_connection = 0;
  }

  m_requestSent = false;
  m_pollTicks = 0;
}

void AboutController::setChecking(bool checking) {
  m_checking = checking;
  emit updateStateChanged();
}

void AboutController::setLatestState() {
  m_checked = true;
  m_updateAvailable = false;
  m_statusText = tr("Yay! You are on latest version");
  finishRequest();
  setChecking(false);
}

void AboutController::setErrorState(const QString &message) {
  m_checked = true;
  m_updateAvailable = false;
  m_statusText = tr("Could not check for update") + ": " + message;
  finishRequest();
  setChecking(false);
}

void AboutController::parseResponse(const QByteArray &bytes) {
  try {
    bb::data::JsonDataAccess json;
    QVariant parsed = json.loadFromBuffer(bytes);

    if (json.hasError()) {
      setErrorState(json.error().errorMessage());
      return;
    }

    QVariantList releases = parsed.toList();
    if (releases.isEmpty()) {
      setLatestState();
      return;
    }

    QVariantMap release = releases.first().toMap();
    m_latestVersion = release.value("tag_name").toString();
    m_releaseUrl = release.value("html_url").toString();
    m_publishedAtText =
        formatPublishedAt(release.value("published_at").toString());

    QVariantList assets = release.value("assets").toList();
    if (!assets.isEmpty()) {
      QVariantMap asset = assets.first().toMap();
      m_sizeText = formatSize(asset.value("size").toLongLong());
      m_downloadUrl = asset.value("browser_download_url").toString();
    }

    m_checked = true;
    m_updateAvailable = isRemoteVersionNewer(m_latestVersion);
    if (m_updateAvailable) {
      m_statusText = tr("A new version is available: v%1").arg(m_latestVersion);
    } else {
      m_statusText = tr("Yay! You are on latest version");
    }
    setChecking(false);
  } catch (...) {
    setErrorState(tr("Unknown update parse error"));
  }
}

bool AboutController::isRemoteVersionNewer(const QString &remoteVersion) const {
  QStringList remoteParts = remoteVersion.split('.');
  QStringList currentParts = currentVersion().split('.');
  int count = qMax(remoteParts.size(), currentParts.size());

  for (int i = 0; i < count; ++i) {
    int remote = i < remoteParts.size() ? remoteParts.at(i).toInt() : 0;
    int current = i < currentParts.size() ? currentParts.at(i).toInt() : 0;
    if (remote > current) {
      return true;
    }
    if (remote < current) {
      return false;
    }
  }

  return false;
}

QString AboutController::currentVersion() const {
  bb::ApplicationInfo info;
  return info.version();
}

QString AboutController::formatPublishedAt(const QString &publishedAt) const {
  QDateTime date = QDateTime::fromString(publishedAt, Qt::ISODate);
  if (!date.isValid()) {
    return publishedAt;
  }

  return date.toLocalTime().toString("yyyy/MM/dd h:mm:ss");
}

QString AboutController::formatSize(qint64 bytes) const {
  return QString::number(bytes / 1048576.0, 'f', 2) + " MB";
}
