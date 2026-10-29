#include "HubIntegration.hpp"

#include <bb/pim/unified/unified_data_source.h>
#include <bb/multimedia/MediaPlayer>

#include <QDebug>
#include <QByteArray>
#include <QLatin1String>
#include <QDateTime>
#include <QDir>
#include <QSettings>
#include <QStringList>
#include <QTimer>

// Account (tab) icon: fixed brand icon. Must live in the public asset folder
// passed to uds_register_client() (declared public="true" in bar-descriptor.xml).
static const char *HUB_ICON_FILE = "HubAccountIcon.png";
// Per-item icons for read/unread state; must also be public or Hub shows a blank icon.
static const char *HUB_ICON_UNREAD_FILE = HubShared::ICON_UNREAD;
// Item context-menu icons (hub-icons/ is public, same rule as above): "Mark Read" shows
// the opened-envelope icon, "Mark Unread" the closed one, "Reply" the reply arrow.
static const char *HUB_ICON_MARK_OPENED_FILE   = "ic_mark_opened.png";
static const char *HUB_ICON_MARK_UNOPENED_FILE = "ic_mark_unopened.png";
static const char *HUB_ICON_REPLY_FILE         = "ic_reply.png";
static const char *HUB_SERVICE_URL = HubShared::SERVICE_URL;

// App-specific vendor mime type (a generic type made short-tap on a Hub
// item do nothing). Used for both the item and its action.
static const char *HUB_INVOKE_TARGET = HubShared::UI_TARGET;
// Headless service (BBCordHeadless): account target and target of the read-state actions.
static const char *HUB_SERVICE_TARGET = HubShared::SERVICE_TARGET;
// The app's <id>. Not a valid invoke target; use HUB_INVOKE_TARGET, the
// <invoke-target id> declared in bar-descriptor.xml.
static const char *HUB_APP_ID = "ch.michioxd.bbcord";
static const char *HUB_MIME_TYPE_MESSAGE = HubShared::MIME_TYPE;

// Context state bits; must match the context_mask of the "Open in BBCord"
// action, or Hub finds no action for the item.
static const unsigned int HUB_CONTEXT_STATE_READ   = HubShared::CONTEXT_READ;
static const unsigned int HUB_CONTEXT_STATE_UNREAD = HubShared::CONTEXT_UNREAD;

// All items share a single category (required for actionable items).
static const long long HUB_CATEGORY_ID = HubShared::CATEGORY_ID;

// Limits init() attempts per session, INIT_RETRY_INTERVAL_MS apart, since the
// Hub service may not be ready at startup (5 x 3s = ~15s).
const int HubIntegration::MAX_INIT_ATTEMPTS = 5;
const qint64 HubIntegration::INIT_RETRY_INTERVAL_MS = 3000;

// Same idea as above for mm-renderer: MediaPlayer can fail to connect right
// after startup, so the same ping is retried a few times.
const int HubIntegration::MAX_PING_RETRIES = 4;
const int HubIntegration::PING_RETRY_DELAY_MS = 500;

// Registers one item context action (long-press menu entry) for the account. Each action
// is only offered while the item's context state matches contextMask.
static int registerItemAction(void *udsHandle, long long accountId, const char *action,
                              const char *target, const char *icon, const char *title,
                              unsigned int contextMask)
{
    uds_item_action_data_t *a = uds_item_action_data_create();
    uds_item_action_data_set_action(a, action);
    uds_item_action_data_set_target(a, target);
    uds_item_action_data_set_type(a, "service");
    uds_item_action_data_set_title(a, title);
    uds_item_action_data_set_image_source(a, icon);
    uds_item_action_data_set_mime_type(a, HUB_MIME_TYPE_MESSAGE);
    uds_item_action_data_set_placement(a, UDS_PLACEMENT_DEFAULT);
    uds_item_action_data_set_context_mask(a, contextMask);
    int rc = uds_register_item_context_action(static_cast<uds_context_t>(udsHandle),
                                              accountId, a);
    uds_item_action_data_destroy(a);
    return rc;
}

// Maps uds_error_code_t to a readable name for logs.
static QString udsErrorName(int rc)
{
    switch (rc) {
    case 0:   return QLatin1String("UDS_SUCCESS");
    case 501: return QLatin1String("UDS_ERROR_FAILED");
    case 502: return QLatin1String("UDS_ERROR_DISCONNECTED");
    case 503: return QLatin1String("UDS_ERROR_INVALID_ITEM");
    case 504: return QLatin1String("UDS_ERROR_NOT_SUPPORTED");
    case 505: return QLatin1String("UDS_ERROR_TIMEOUT");
    case 601: return QLatin1String("UDS_DUPLICATE_CONFIG");
    case 602: return QLatin1String("UDS_INVALID_SERVICE_ID");
    case 603: return QLatin1String("UDS_INVALID_ACCOUNT_ID");
    default:  return QLatin1String("UDS_UNKNOWN");
    }
}

HubIntegration::HubIntegration(QObject *parent)
    : QObject(parent), m_serviceEventTimer(new QTimer(this)), m_serviceMode(false),
      m_udsHandle(0),
      m_ready(false), m_initAttemptCount(0), m_lastInitAttemptMs(0), m_pingPlayer(0),
      m_pingPlayerSourceSet(false), m_pingRetryCount(0)
{
    // Started by init() once the account exists; see pollServiceEvents().
    connect(m_serviceEventTimer, SIGNAL(timeout()), this, SLOT(pollServiceEvents()));
}

HubIntegration::~HubIntegration()
{
    if (m_udsHandle) {
        uds_context_t h = static_cast<uds_context_t>(m_udsHandle);
        uds_close(&h);
        m_udsHandle = 0;
    }
}

// On BB10, QDir::homePath() is "/accounts/1000/appdata/<real-app-id>/data";
// the app id is derived from it (reliable in Debug and Release builds).
extern char *__progname;

static QString appIdFromHomePath()
{
    QStringList parts = QDir::homePath().split(QLatin1Char('/'), QString::SkipEmptyParts);
    if (parts.size() >= 2 && parts.last() == QLatin1String("data")) {
        QString appId = parts.at(parts.size() - 2);
        qDebug() << "[Hub] appId from homePath =" << appId << "(homePath=" << QDir::homePath() << ")";
        return appId;
    }
    // Fallback: use __progname; a wrong path is better than crashing.
    qDebug() << "[Hub] homePath not in expected shape (" << QDir::homePath()
              << "), falling back to __progname for publicAssetPath().";
    return QString::fromLatin1(__progname);
}

QString HubIntegration::publicAssetPath()
{
    // Must match the dest in bar-descriptor.xml exactly and must not start
    // with "assets" (NDK 10.3.1 rejects such a name).
    return QString("/apps/%1/public/hub-icons/").arg(appIdFromHomePath());
}

// A reinstall can change the app id, leaving Hub with a stale assetPath.
// Compares the app id of the last successful init() (in QSettings) with the
// current one and returns true if it changed, so init() re-registers from scratch.
static bool detectFreshInstallAndRememberAppId(const QString &currentAppId)
{
    QSettings settings;
    const char *kLastAppIdKey = "hub/lastAppId";
    QString lastAppId = settings.value(kLastAppIdKey).toString();

    bool isFreshInstall = (lastAppId != currentAppId);
    if (isFreshInstall) {
        qDebug() << "[Hub] app-id changed from previous run (" << lastAppId
                  << "->" << currentAppId
                  << ") - treating as a fresh install, will close the old "
                     "UDS handle (if any) before re-registering.";
        settings.setValue(kLastAppIdKey, currentAppId);
        settings.sync();
    }
    return isFreshInstall;
}

bool HubIntegration::init()
{
    if (m_ready) return true;

    qint64 now = QDateTime::currentMSecsSinceEpoch();

    if (m_initAttemptCount >= MAX_INIT_ATTEMPTS) {
        // Out of attempts for this session (logged once when the limit is reached).
        return false;
    }

    if (m_initAttemptCount > 0 &&
        (now - m_lastInitAttemptMs) < INIT_RETRY_INTERVAL_MS) {
        // Not due for a retry yet; no-op without logging.
        return false;
    }

    m_initAttemptCount++;
    m_lastInitAttemptMs = now;
    qDebug() << "[Hub] init() attempt" << m_initAttemptCount << "/" << MAX_INIT_ATTEMPTS;

    uds_context_t handle = 0;
    int rc = uds_init(&handle, false /* synchronous */);
    if (rc != UDS_SUCCESS || !handle) {
        qDebug() << "[Hub] uds_init failed, rc=" << rc << "(" << udsErrorName(rc) << ")"
                  << "attempt" << m_initAttemptCount << "/" << MAX_INIT_ATTEMPTS
                  << "- app se tiep tuc hoat dong binh thuong, chi khong co "
                     "tab rieng trong Hub (cho toi khi thu lai thanh cong).";
        if (m_initAttemptCount >= MAX_INIT_ATTEMPTS) {
            qDebug() << "[Hub] Exhausted" << MAX_INIT_ATTEMPTS
                      << "lan thu init() - Hub integration TAT HAN cho phien app nay."
                      << "Restart the app to retry from scratch.";
        }
        return false;
    }
    m_udsHandle = handle;

    QString assetPath = publicAssetPath();
    QString currentAppId = appIdFromHomePath();
    bool isFreshInstall = detectFreshInstallAndRememberAppId(currentAppId);

    rc = uds_register_client(m_udsHandle, HUB_SERVICE_URL, "" /* libPath, unused */,
                              assetPath.toUtf8().constData());
    if (rc != UDS_SUCCESS) {
        qDebug() << "[Hub] uds_register_client failed, rc=" << rc << "(" << udsErrorName(rc) << ")"
                  << "attempt" << m_initAttemptCount << "/" << MAX_INIT_ATTEMPTS
                  << "assetPath=" << assetPath;
        uds_context_t h = static_cast<uds_context_t>(m_udsHandle);
        uds_close(&h);
        m_udsHandle = 0;
        if (m_initAttemptCount >= MAX_INIT_ATTEMPTS) {
            qDebug() << "[Hub] Exhausted" << MAX_INIT_ATTEMPTS
                      << "lan thu init() - Hub integration TAT HAN cho phien app nay."
                      << "Restart the app to retry from scratch.";
        }
        return false;
    }

    int regStatus = uds_get_service_status(m_udsHandle);
    int serviceId = uds_get_service_id(m_udsHandle);
    qDebug() << "[Hub] uds_register_client OK, serviceId=" << serviceId
              << "status=" << regStatus
              << "(1=NEW 2=EXISTS)"
              << "assetPath=" << assetPath
              << "accountId=" << ACCOUNT_ID;

    if (m_serviceMode && HubShared::accountAppId() == currentAppId) {
        // The UI (or an earlier service run) already created the account for this install:
        // keep it, and with it every item Hub currently shows. Recreating it from here
        // would wipe the user's notifications each time the service starts.
        m_ready = true;
        qDebug() << "[Hub] service attached to the existing account (appId" << currentAppId << ")";
        return true;
    }

    // Remove-before-add on every startup so Hub re-resolves icons from the current assetPath.
    int removeRc = uds_account_removed(m_udsHandle, ACCOUNT_ID);
    if (isFreshInstall) {
        qDebug() << "[Hub] uds_account_removed (fresh-install cleanup, app-id "
                     "vua doi) rc=" << removeRc
                  << "(bo qua neu account chua tung ton tai / lan cai dat dau tien)";
    } else {
        qDebug() << "[Hub] uds_account_removed (pre-add cleanup) rc=" << removeRc
                  << "(bo qua neu account chua tung ton tai / lan cai dat dau tien)";
    }

    uds_account_data_t *account = uds_account_data_create();
    uds_account_data_set_id(account, ACCOUNT_ID);
    uds_account_data_set_name(account, "BBCord");
    uds_account_data_set_description(account, "BBCord notifications");
    uds_account_data_set_icon(account, HUB_ICON_FILE);
    // target_name is the generic target for account-level actions (short tap, and the
    // built-in Delete button), so it must be an <invoke-target> id, not the app id. It is
    // the headless service so these never launch the UI; the service forwards OPEN/VIEW
    // to HUB_INVOKE_TARGET.
    uds_account_data_set_target_name(account, HUB_SERVICE_TARGET);
    // false: composing new messages from Hub is not supported.
    uds_account_data_set_supports_compose(account, false);
    // IM account type (ordering of the account tab only; no effect on tap behavior).
    uds_account_data_set_type(account, UDS_ACCOUNT_TYPE_IM);

    rc = uds_account_added(m_udsHandle, account);
    uds_account_data_destroy(account);

    if (rc != UDS_SUCCESS) {
        qDebug() << "[Hub] account add failed, rc=" << rc << "(" << udsErrorName(rc) << ")"
                  << "attempt" << m_initAttemptCount << "/" << MAX_INIT_ATTEMPTS;
        uds_context_t h = static_cast<uds_context_t>(m_udsHandle);
        uds_close(&h);
        m_udsHandle = 0;
        if (m_initAttemptCount >= MAX_INIT_ATTEMPTS) {
            qDebug() << "[Hub] Exhausted" << MAX_INIT_ATTEMPTS
                      << "lan thu init() - Hub integration TAT HAN cho phien app nay."
                      << "Restart the app to retry from scratch.";
        }
        return false;
    }

    qDebug() << "[Hub] BBCord account registered in BlackBerry Hub, id=" << ACCOUNT_ID
              << "(succeeded after" << m_initAttemptCount << "attempts)";
    m_ready = true;

    // The account was just recreated, which emptied it: nothing persisted from the last
    // session describes a real Hub item anymore. Then start listening to the service.
    HubShared::clearAllState();
    HubShared::setAccountAppId(currentAppId);
    m_threadItemState.clear();
    m_unreadCounts.clear();
    // Only the UI listens to the service mailbox.
    if (!m_serviceMode) {
        m_serviceEventTimer->start(2500);
    }

    // uds_category_added() must run before any item_added() using that category_id.
    {
        uds_category_data_t *category = uds_category_data_create();
        uds_category_data_set_id(category, HUB_CATEGORY_ID);
        uds_category_data_set_account_id(category, ACCOUNT_ID);
        uds_category_data_set_name(category, "BBCord");
        int categoryRc = uds_category_added(m_udsHandle, category);
        if (categoryRc != UDS_SUCCESS) categoryRc = uds_category_updated(m_udsHandle, category);
        uds_category_data_destroy(category);
        qDebug() << "[Hub] uds_category_added rc=" << categoryRc
                  << "id=" << HUB_CATEGORY_ID;
    }

    // Register the "Reply" item context action (long-press); it opens the chat and
    // applies to every item of this account.
    uds_item_action_data_t *openAction = uds_item_action_data_create();
    uds_item_action_data_set_action(openAction, "bb.action.OPEN");
    uds_item_action_data_set_target(openAction, HUB_INVOKE_TARGET);
    // "service" is one of the two valid targetType values ("card.composer" or "service").
    uds_item_action_data_set_type(openAction, "service");
    uds_item_action_data_set_title(openAction, "Reply");
    uds_item_action_data_set_image_source(openAction, HUB_ICON_REPLY_FILE);
    // Same vendor mime type as the item, so long-press and short-tap resolve identically.
    uds_item_action_data_set_mime_type(openAction, HUB_MIME_TYPE_MESSAGE);
    uds_item_action_data_set_placement(openAction, UDS_PLACEMENT_DEFAULT);
    uds_item_action_data_set_context_mask(openAction, HUB_CONTEXT_STATE_READ | HUB_CONTEXT_STATE_UNREAD);

    int actionRc = uds_register_item_context_action(m_udsHandle, ACCOUNT_ID, openAction);
    uds_item_action_data_destroy(openAction);
    if (actionRc != UDS_SUCCESS) {
        qDebug() << "[Hub] uds_register_item_context_action (Reply) failed, rc=" << actionRc
                  << "- item van hien trong Hub nhung co the khong mo duoc khi tap/long-press.";
    } else {
        qDebug() << "[Hub] uds_register_item_context_action (Reply) OK";
    }

    // "Mark Read" is offered only for unread items, "Mark Unread" only for read ones
    // (context_mask vs the item's context_state). Failure is not fatal: the other actions
    // keep working.
    // They target the headless service, so choosing them never opens the app.
    int markReadRc = registerItemAction(m_udsHandle, ACCOUNT_ID, HubShared::ACTION_MARK_READ,
                                        HUB_SERVICE_TARGET, HUB_ICON_MARK_OPENED_FILE,
                                        "Mark Read", HUB_CONTEXT_STATE_UNREAD);
    int markUnreadRc = registerItemAction(m_udsHandle, ACCOUNT_ID, HubShared::ACTION_MARK_UNREAD,
                                          HUB_SERVICE_TARGET, HUB_ICON_MARK_UNOPENED_FILE,
                                          "Mark Unread", HUB_CONTEXT_STATE_READ);
    qDebug() << "[Hub] item actions Mark Read rc=" << markReadRc
              << "Mark Unread rc=" << markUnreadRc;

    return true;
}

void HubIntegration::upsertThreadItem(const QString &sourceId, const QString &title,
                                      const QString &preview, qint64 timestampMs)
{
    if (sourceId.isEmpty()) return;
    if (!init()) return; // init() no-ops itself if already ready; false means Hub is unavailable

    // Re-read from disk: the service may have marked the item read/unread or deleted it
    // since this process last touched it, and the count must continue from that.
    m_threadItemState.remove(sourceId);
    m_unreadCounts.remove(sourceId);
    ensureState(sourceId);
    int unread = m_unreadCounts.value(sourceId, 0) + 1;
    m_unreadCounts[sourceId] = unread;

    QByteArray sourceIdUtf8 = sourceId.toUtf8();
    QByteArray titleUtf8    = title.toUtf8();
    QByteArray previewUtf8  = preview.toUtf8();

    uds_inbox_item_data_t *item = uds_inbox_item_data_create();
    uds_inbox_item_data_set_account_id(item, ACCOUNT_ID);
    uds_inbox_item_data_set_source_id(item, const_cast<char*>(sourceIdUtf8.constData()));
    uds_inbox_item_data_set_name(item, titleUtf8.constData());
    uds_inbox_item_data_set_description(item, previewUtf8.constData());
    uds_inbox_item_data_set_icon(item, HUB_ICON_UNREAD_FILE);
    uds_inbox_item_data_set_mime_type(item, HUB_MIME_TYPE_MESSAGE);
    uds_inbox_item_data_set_category_id(item, HUB_CATEGORY_ID);
    uds_inbox_item_data_set_timestamp(item, timestampMs);
    uds_inbox_item_data_set_unread_count(item, unread);
    uds_inbox_item_data_set_total_count(item, unread);
    uds_inbox_item_data_set_context_state(item, HUB_CONTEXT_STATE_UNREAD);
    // false disables Hub's alert bundle (banner, system sound, lock-screen
    // preview) for this item. The app already plays ping.m4a itself, so true
    // would play the sound twice. Trade-off: no Hub banner either.
    // The UI plays its own ping (false). The service has no player, so it lets Hub raise
    // the alert (banner / LED / sound as configured in Hub).
    uds_inbox_item_data_set_notification_state(item, m_serviceMode);

    // Try update first (the common case), fall back to add; Hub has no existence query.
    int rc = uds_item_updated(m_udsHandle, item);
    if (rc != UDS_SUCCESS) {
        rc = uds_item_added(m_udsHandle, item);
    }
    if (rc == UDS_SUCCESS) {
        m_knownSourceIds.insert(sourceId);
        HubShared::ItemState st;
        st.title = title;
        st.preview = preview;
        st.timestampMs = timestampMs;
        st.unread = unread;
        st.total = unread;
        m_threadItemState[sourceId] = st;
        HubShared::saveState(sourceId, st);
    }
    uds_inbox_item_data_destroy(item);

    if (rc != UDS_SUCCESS) {
        qDebug() << "[Hub] upsertThreadItem failed for source" << sourceId
                  << "rc=" << rc << "(" << udsErrorName(rc) << ")";
    }
}

bool HubIntegration::publishItemState(const QString &sourceId)
{
    if (!m_ready || !m_threadItemState.contains(sourceId)) return false;
    // Only the badge/state changes: notify=false so Hub does not retrigger its alerts.
    return HubShared::publishItem(m_udsHandle, sourceId, m_threadItemState[sourceId],
                                  false /* notify */, false /* allowAdd */);
}

bool HubIntegration::ensureState(const QString &sourceId)
{
    if (m_threadItemState.contains(sourceId)) return true;
    HubShared::ItemState st;
    if (!HubShared::loadState(sourceId, st)) return false;
    m_threadItemState[sourceId] = st;
    if (!m_unreadCounts.contains(sourceId)) m_unreadCounts[sourceId] = st.unread;
    return true;
}

void HubIntegration::reloadThread(const QString &sourceId)
{
    m_threadItemState.remove(sourceId);
    m_unreadCounts.remove(sourceId);
    m_knownSourceIds.remove(sourceId);
}

void HubIntegration::pollServiceEvents()
{
    QStringList events = HubShared::takeEvents();
    for (int i = 0; i < events.size(); ++i) {
        int tab = events.at(i).indexOf(QLatin1Char('\t'));
        if (tab <= 0) continue;
        QString action = events.at(i).left(tab);
        QString channelId = events.at(i).mid(tab + 1);
        qDebug() << "[Hub] service handled" << action << "for" << channelId;
        reloadThread(channelId);
        emit itemActionFromService(action, channelId);
    }
}

void HubIntegration::markThreadRead(const QString &sourceId)
{
    if (sourceId.isEmpty() || !m_ready) return;
    // No stored state means the item was never published (or was deleted); skip rather
    // than send an update with empty fields.
    if (!ensureState(sourceId)) return;
    if (m_threadItemState[sourceId].unread == 0 && m_unreadCounts.value(sourceId, 0) == 0) {
        return; // already read, avoid a wasted IPC call
    }

    m_unreadCounts[sourceId] = 0;
    m_threadItemState[sourceId].unread = 0;
    HubShared::saveState(sourceId, m_threadItemState[sourceId]);
    publishItemState(sourceId);
}

void HubIntegration::removeThreadItem(const QString &sourceId)
{
    if (sourceId.isEmpty() || !m_ready) return;

    if (!HubShared::removeItem(m_udsHandle, sourceId)) {
        qDebug() << "[Hub] removeThreadItem failed for source" << sourceId;
        return;
    }
    m_knownSourceIds.remove(sourceId);
    m_unreadCounts.remove(sourceId);
    m_threadItemState.remove(sourceId);
    HubShared::forgetState(sourceId);
}

void HubIntegration::playPingSound()
{
    if (m_serviceMode) return; // Hub alerts instead (see upsertThreadItem())
    // Independent of m_ready/init(): a UDS failure (e.g. missing
    // _sys_access_pim_unified permission) must not silence the sound.
    qDebug() << "[Hub] playPingSound() called";
    // Reset the retry budget so each ping gets its own full retries.
    m_pingRetryCount = 0;
    QTimer::singleShot(0, this, SLOT(onPlayPingSoundDeferred()));
}

void HubIntegration::onPlayPingSoundDeferred()
{
    qDebug() << "[Hub] onPlayPingSoundDeferred() running, m_pingPlayer="
             << (void *) m_pingPlayer << "sourceSet=" << m_pingPlayerSourceSet;
    if (!m_pingPlayer) {
        m_pingPlayer = new bb::multimedia::MediaPlayer(this);
        qDebug() << "[Hub] created new MediaPlayer" << (void *) m_pingPlayer;
    }

    if (!m_pingPlayerSourceSet) {
        // "audio/ping.m4a": the "assets" root is stripped from asset:/// URLs.
        m_pingPlayer->setSourceUrl(QUrl("asset:///audio/ping.m4a"));
        m_pingPlayer->prepare();
        m_pingPlayerSourceSet = true;
        qDebug() << "[Hub] source set + prepare() called, will retry play() in 300ms";
        QTimer::singleShot(300, this, SLOT(onPingPlayerReadyRetry()));
        return;
    }

    playOnPingPlayerOrResetForRetry();
}

void HubIntegration::onPingPlayerReadyRetry()
{
    qDebug() << "[Hub] onPingPlayerReadyRetry() running, m_pingPlayer="
             << (void *) m_pingPlayer;
    if (!m_pingPlayer) {
        return;
    }
    playOnPingPlayerOrResetForRetry();
}

void HubIntegration::playOnPingPlayerOrResetForRetry()
{
    bb::multimedia::MediaError::Type err = m_pingPlayer->play();
    qDebug() << "[Hub] play() called, mediaError=" << err;
    if (err == bb::multimedia::MediaError::None) {
        m_pingRetryCount = 0;
        return;
    }

    qDebug() << "[Hub] playPingSound failed, mediaError=" << err;
    // A player that failed to connect to mm-renderer never recovers, so discard
    // it and clear m_pingPlayerSourceSet; the next attempt builds a new MediaPlayer.
    // deleteLater() because this may run inside a slot of m_pingPlayer.
    m_pingPlayer->deleteLater();
    m_pingPlayer = 0;
    m_pingPlayerSourceSet = false;

    // mm-renderer can be unreachable for a couple of seconds after startup, so retry the same ping.
    if (m_pingRetryCount < MAX_PING_RETRIES) {
        m_pingRetryCount++;
        qDebug() << "[Hub] retrying playPingSound, attempt" << m_pingRetryCount
                 << "/" << MAX_PING_RETRIES;
        QTimer::singleShot(PING_RETRY_DELAY_MS, this,
                           SLOT(onPlayPingSoundDeferred()));
    } else {
        qDebug() << "[Hub] Exhausted" << MAX_PING_RETRIES
                 << "ping retries, giving up until the next notify-worthy message";
        m_pingRetryCount = 0;
    }
}
