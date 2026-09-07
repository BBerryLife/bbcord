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

// account icon (the BBCord tab in Hub) — a fixed "brand" icon, doesn't
// change with read/unread state. Must live in the PUBLIC asset folder
// passed to uds_register_client() (see publicAssetPath()/init()) —
// declared public="true" separately in bar-descriptor.xml, kept apart
// from the app's general "assets" folder (see the long comment in
// bar-descriptor.xml explaining why this folder must stay separate,
// not nested).
static const char *HUB_ICON_FILE = "HubAccountIcon.png";
// Per-item icons for each inbox item, changing with read/unread state —
// both must also be declared public="true" like HUB_ICON_FILE above, or
// Hub can't read them either (silently falls back to a blank/default
// icon, no error).
static const char *HUB_ICON_UNREAD_FILE = "HubItemUnread.png";
static const char *HUB_ICON_READ_FILE   = "HubItemRead.png";
static const char *HUB_SERVICE_URL = "ch.michioxd.bbcord.hub";

// Mime type used for inbox items — "plain/message" (not "text/plain")
// per the official sample code in unified_data_source.h
// (uds_inbox_item_data_set_mime_type). Kept as-is from the investigation
// findings in Zalo10's HubIntegration.cpp (see the "SINGLE-TAP DOESN'T
// OPEN APP" history there) — at the time of this port, this was the
// best hypothesis available, NOT confirmed to fix the single-tap issue
// on real hardware. The item action (long-press "Open in BBCord") still
// uses "text/plain", kept as-is from Zalo10 since that part IS
// confirmed working correctly.
static const char *HUB_INVOKE_TARGET = "ch.michioxd.bbcord.invoke";
static const char *HUB_APP_ID = "ch.michioxd.bbcord";
static const char *HUB_MIME_TYPE_MESSAGE = "plain/message";

// Context state bits for an item — MUST match the context_mask of the
// "Open in BBCord" action (uds_item_action_data_set_context_mask, see
// init()). Missing the set_context_state call on an item means Hub
// can't find any matching action for it on tap/long-press — see the
// full explanation in Zalo10's HubIntegration.cpp.
static const unsigned int HUB_CONTEXT_STATE_READ   = 0x01;
static const unsigned int HUB_CONTEXT_STATE_UNREAD = 0x02;

// category_id: per the official sample in unified_data_source.h — every
// item of this account shares one single category, with no special
// classification meaning beyond being a required field before an item
// can be "actionable" (see uds_category_added() in init()).
static const long long HUB_CATEGORY_ID = 1;

// Limits init() attempts for the whole app session, at least
// INIT_RETRY_INTERVAL_MS apart, instead of trying only once and
// latching permanently. uds_init()/uds_register_client() have been
// observed failing transiently right at app startup on real devices
// (the OS's Hub service may not be ready yet) — 5 attempts / 3s apart
// gives the app about 15s for the service to come up before giving up
// for that session.
const int HubIntegration::MAX_INIT_ATTEMPTS = 5;
const qint64 HubIntegration::INIT_RETRY_INTERVAL_MS = 3000;

// Maps uds_error_code_t (unified_data_source.h) to a readable name in
// the log — a raw numeric rc isn't enough to know what went wrong when
// reading logs on-device without the header on hand.
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
    : QObject(parent), m_udsHandle(0), m_ready(false),
      m_initAttemptCount(0), m_lastInitAttemptMs(0), m_pingPlayer(0)
{
}

HubIntegration::~HubIntegration()
{
    if (m_udsHandle) {
        uds_context_t h = static_cast<uds_context_t>(m_udsHandle);
        uds_close(&h);
        m_udsHandle = 0;
    }
}

// On BB10, QDir::homePath() returns
// "/accounts/1000/appdata/<real-app-id>/data" — this formula was
// confirmed reliable via real runtime logs on-device in Zalo10 (correct
// in BOTH Debug and Release builds, unlike __progname which was only
// correct by coincidence in Debug — see "FIX ATTEMPT 11" in Zalo10's
// HubIntegration.cpp for the full reasoning). This same formula is kept
// as-is for BBCord.
extern char *__progname;

static QString appIdFromHomePath()
{
    QStringList parts = QDir::homePath().split(QLatin1Char('/'), QString::SkipEmptyParts);
    if (parts.size() >= 2 && parts.last() == QLatin1String("data")) {
        QString appId = parts.at(parts.size() - 2);
        qDebug() << "[Hub] appId from homePath =" << appId << "(homePath=" << QDir::homePath() << ")";
        return appId;
    }
    // Fallback: homePath() isn't in the expected shape -> use
    // __progname as the old formula, better a wrong path than a full
    // crash (Hub integration is an add-on feature, must never break
    // other app functionality).
    qDebug() << "[Hub] homePath not in expected shape (" << QDir::homePath()
              << "), falling back to __progname for publicAssetPath().";
    return QString::fromLatin1(__progname);
}

QString HubIntegration::publicAssetPath()
{
    // "hub-icons" MUST match EXACTLY the dest in bar-descriptor.xml:
    // <asset path="hub-icons" public="true">hub-icons</asset>
    // The name must NOT start with "assets" — see the long comment in
    // bar-descriptor.xml (lesson from Zalo10: Momentics NDK 10.3.1
    // appears to block based on a name prefix matching the already
    // declared "assets" rule).
    return QString("/apps/%1/public/hub-icons/").arg(appIdFromHomePath());
}

// On every reinstall (even same version), BB10 can generate a new
// app-id (the hash suffix changes — see real log: "...testDev_ioxd_
// bbcordd4b95190"), so publicAssetPath() above then points to a
// different physical folder. If Hub still holds a service registration
// (uds_register_client) pointing at the PREVIOUS install's assetPath,
// account/item icons show broken or blank until an account_removed +
// re-registration happens with the new assetPath. The current
// uds_account_removed() remove-before-add (see init() below) only runs
// ONCE PER APP STARTUP, without distinguishing "just reinstalled" from
// "normal Nth run" — still correct, but not enough to clean up a stale
// service registration pointing at the wrong assetPath if Hub caches
// that registration before account_removed gets a chance to run. This
// function stores the app-id used on the most recent successful init()
// in QSettings; if the current app-id differs (or none was ever
// stored), it's treated as "just reinstalled" and returns true so
// init() knows to close any old UDS handle before re-registering from
// scratch, ensuring the asset path always matches the current install.
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
        // Out of attempts for this session. Only logged once, right at
        // the moment the threshold is crossed (see where
        // m_initAttemptCount is incremented below) so this doesn't spam
        // the log on every subsequent message — here it just no-ops
        // quietly.
        return false;
    }

    if (m_initAttemptCount > 0 &&
        (now - m_lastInitAttemptMs) < INIT_RETRY_INTERVAL_MS) {
        // Not due for a retry yet — no-op quietly, no log, to avoid
        // spamming the log when new messages arrive back-to-back while
        // waiting out the cooldown.
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

    // Remove-before-add on every startup — per "Fix Attempt 10" in
    // Zalo10 (ensures Hub always re-resolves icons from the current
    // running build's assetPath, regardless of the sandbox path
    // changing between builds). See the full explanation in Zalo10's
    // HubIntegration.cpp.
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
    uds_account_data_set_target_name(account, HUB_APP_ID);
    // false: this account doesn't support composing new messages
    // directly from Hub (no handler for the "bb.action.CREATE" action
    // on the app side yet) — only shows + opens to an existing
    // channel/thread.
    uds_account_data_set_supports_compose(account, false);
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

    // category_added() MUST run before any item_added() using that
    // category_id — see "FIX ATTEMPT 7" in Zalo10's HubIntegration.cpp.
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

    // Register "Open in BBCord" — item context action (long-press).
    // Registered once at the account level, applies to EVERY item of
    // this account.
    uds_item_action_data_t *openAction = uds_item_action_data_create();
    uds_item_action_data_set_action(openAction, "bb.action.OPEN");
    uds_item_action_data_set_target(openAction, HUB_INVOKE_TARGET);
    // "service": one of the only 2 valid targetType values per the
    // official unified_data_source.h header ("card.composer" or
    // "service") — confirmed in Zalo10's HubIntegration.cpp.
    uds_item_action_data_set_type(openAction, "service");
    uds_item_action_data_set_title(openAction, "Open in BBCord");
    uds_item_action_data_set_image_source(openAction, HUB_ICON_FILE);
    uds_item_action_data_set_mime_type(openAction, "text/plain");
    uds_item_action_data_set_placement(openAction, UDS_PLACEMENT_FIXED);
    uds_item_action_data_set_context_mask(openAction, HUB_CONTEXT_STATE_READ | HUB_CONTEXT_STATE_UNREAD);

    int actionRc = uds_register_item_context_action(m_udsHandle, ACCOUNT_ID, openAction);
    uds_item_action_data_destroy(openAction);
    if (actionRc != UDS_SUCCESS) {
        qDebug() << "[Hub] uds_register_item_context_action (Open in BBCord) failed, rc=" << actionRc
                  << "- item van hien trong Hub nhung co the khong mo duoc khi tap/long-press.";
    } else {
        qDebug() << "[Hub] uds_register_item_context_action (Open in BBCord) OK";
    }

    return true;
}

void HubIntegration::upsertThreadItem(const QString &sourceId, const QString &title,
                                      const QString &preview, qint64 timestampMs)
{
    if (sourceId.isEmpty()) return;
    if (!init()) return; // init() no-ops itself if already ready; false means Hub is unavailable

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
    // false (changed from true): fully disables Hub's "alert effect"
    // bundle for this item — per the official docs
    // (uds_inbox_item_data_set_notification_state in
    // unified_data_source.h), true/false is a SHARED switch for banner
    // + system sound + lock-screen instant preview, sound can't be
    // separated from banner via this API. Changed to false because the
    // app already plays ping.m4a itself via playPingSound() (see
    // Client.cpp) — leaving it true would double up the sound (matches
    // the reported bug: "2 notification sounds playing at once").
    // Trade-off: the item row still lands on Hub/badge normally
    // (uds_item_added/updated doesn't depend on this flag), but Hub's
    // own banner popup + lock-screen instant preview turn off with it
    // too, not just the sound — the API doesn't allow a partial
    // disable. If a banner is needed again later, the only way is to
    // set this back to true and disable playPingSound() instead (can't
    // have both sources at once without collision).
    uds_inbox_item_data_set_notification_state(item, false);

    // Try update first (the more common case — multiple pings on the
    // same channel/thread), fall back to add on failure — Hub has no
    // query API to check beforehand whether an item already exists.
    int rc = uds_item_updated(m_udsHandle, item);
    if (rc != UDS_SUCCESS) {
        rc = uds_item_added(m_udsHandle, item);
    }
    if (rc == UDS_SUCCESS) {
        m_knownSourceIds.insert(sourceId);
        ThreadItemState st;
        st.title = title;
        st.preview = preview;
        st.timestampMs = timestampMs;
        m_threadItemState[sourceId] = st;
    }
    uds_inbox_item_data_destroy(item);

    if (rc != UDS_SUCCESS) {
        qDebug() << "[Hub] upsertThreadItem failed for source" << sourceId
                  << "rc=" << rc << "(" << udsErrorName(rc) << ")";
    }
}

void HubIntegration::markThreadRead(const QString &sourceId)
{
    if (sourceId.isEmpty() || !m_ready) return;
    if (m_unreadCounts.value(sourceId, 0) == 0) return; // already 0 (or never added), avoid a wasted IPC call
    if (!m_threadItemState.contains(sourceId)) {
        // No full state has ever been stored for this item — nothing to
        // reconstruct with, skip instead of sending an update with
        // missing fields (would reset name/description/timestamp to
        // empty — see the ThreadItemState struct comment in the .hpp).
        return;
    }

    m_unreadCounts[sourceId] = 0;
    const ThreadItemState &st = m_threadItemState[sourceId];

    QByteArray sourceIdUtf8 = sourceId.toUtf8();
    QByteArray titleUtf8    = st.title.toUtf8();
    QByteArray previewUtf8  = st.preview.toUtf8();

    uds_inbox_item_data_t *item = uds_inbox_item_data_create();
    uds_inbox_item_data_set_account_id(item, ACCOUNT_ID);
    uds_inbox_item_data_set_source_id(item, const_cast<char*>(sourceIdUtf8.constData()));
    // uds_item_updated() REPLACES THE WHOLE record — must resend ALL
    // fields, exactly as in the most recent upsertThreadItem() call,
    // only actually changing the parts that need it (icon: Read;
    // unread_count: 0; notification_state: false).
    uds_inbox_item_data_set_name(item, titleUtf8.constData());
    uds_inbox_item_data_set_description(item, previewUtf8.constData());
    uds_inbox_item_data_set_mime_type(item, HUB_MIME_TYPE_MESSAGE);
    uds_inbox_item_data_set_category_id(item, HUB_CATEGORY_ID);
    uds_inbox_item_data_set_timestamp(item, st.timestampMs);
    uds_inbox_item_data_set_total_count(item, 0);
    uds_inbox_item_data_set_icon(item, HUB_ICON_READ_FILE);
    uds_inbox_item_data_set_unread_count(item, 0);
    uds_inbox_item_data_set_context_state(item, HUB_CONTEXT_STATE_READ);
    uds_inbox_item_data_set_notification_state(item, false); // only changing the badge, don't want to retrigger effects

    int rc = uds_item_updated(m_udsHandle, item);
    uds_inbox_item_data_destroy(item);

    if (rc != UDS_SUCCESS) {
        qDebug() << "[Hub] markThreadRead: item chua ton tai hoac update loi cho source"
                  << sourceId << "rc=" << rc;
    }
}

void HubIntegration::removeThreadItem(const QString &sourceId)
{
    if (sourceId.isEmpty() || !m_ready) return;

    QByteArray sourceIdUtf8 = sourceId.toUtf8();
    int rc = uds_item_removed(m_udsHandle, ACCOUNT_ID, const_cast<char*>(sourceIdUtf8.constData()));
    if (rc != UDS_SUCCESS) {
        qDebug() << "[Hub] removeThreadItem failed for source" << sourceId << "rc=" << rc;
        return;
    }
    m_knownSourceIds.remove(sourceId);
    m_unreadCounts.remove(sourceId);
    m_threadItemState.remove(sourceId);
}

void HubIntegration::playPingSound()
{
    // Doesn't depend on m_ready/init() — intentional, see the
    // explanation on the declaration in HubIntegration.hpp. Hub (UDS)
    // and sound are two independent paths, no reason for a UDS failure
    // (as has happened before: missing _sys_access_pim_unified
    // permission, rc=501) to also take down the sound.
    if (!m_pingPlayer) {
        m_pingPlayer = new bb::multimedia::MediaPlayer(this);
        // "audio/ping.m4a" (not "assets/audio/ping.m4a"): the "assets"
        // folder declared in bar-descriptor.xml
        // (<asset path="assets">assets</asset>) is the root of the
        // asset:/// scheme and gets stripped from the URL — same
        // convention used throughout the existing code (e.g.
        // "asset:///images/icons/first.png" for the real file at
        // assets/images/icons/first.png, see
        // MainPageController.cpp/ItemMapper.cpp).
        m_pingPlayer->setSourceUrl(QUrl("asset:///audio/ping.m4a"));
    }
    bb::multimedia::MediaError::Type err = m_pingPlayer->play();
    if (err != bb::multimedia::MediaError::None) {
        qDebug() << "[Hub] playPingSound failed, mediaError=" << err;
    }
}

