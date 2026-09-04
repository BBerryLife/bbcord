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

// icon account (tab BBCord trong Hub) — icon "thương hiệu" chung, không đổi
// theo trạng thái đọc/chưa đọc. Phải nằm trong thư mục asset PUBLIC truyền
// vào uds_register_client() (xem publicAssetPath()/init()) — khai báo
// public="true" riêng trong bar-descriptor.xml, tách khỏi thư mục "assets"
// chung của app (xem comment dài trong bar-descriptor.xml giải thích vì
// sao thư mục này phải tách riêng, không được lồng).
static const char *HUB_ICON_FILE = "HubAccountIcon.png";
// icon riêng cho từng inbox item, đổi theo trạng thái đọc/chưa đọc — cả 2
// đều phải khai báo public="true" giống HUB_ICON_FILE ở trên, nếu không
// Hub cũng không đọc được (im lặng dùng icon rỗng/mặc định, không báo
// lỗi).
static const char *HUB_ICON_UNREAD_FILE = "HubItemUnread.png";
static const char *HUB_ICON_READ_FILE   = "HubItemRead.png";
static const char *HUB_SERVICE_URL = "ch.michioxd.bbcord.hub";

// Mime type dùng cho inbox item — "plain/message" (không phải "text/plain")
// theo đúng mẫu code chính thức trong unified_data_source.h
// (uds_inbox_item_data_set_mime_type). Kế thừa nguyên trạng từ kết luận
// điều tra bên Zalo10's HubIntegration.cpp (xem comment ở đó, phần lịch sử
// "SINGLE-TAP KHÔNG MỞ APP") — tại thời điểm port, đây là giả thuyết tốt
// nhất đã có, CHƯA được xác nhận sửa xong vấn đề single-tap trên thiết bị
// thật. Item action (long-press "Open in BBCord") vẫn dùng "text/plain",
// giữ nguyên như Zalo10 vì đó là phần ĐÃ xác nhận hoạt động đúng.
static const char *HUB_INVOKE_TARGET = "ch.michioxd.bbcord.invoke";
static const char *HUB_APP_ID = "ch.michioxd.bbcord";
static const char *HUB_MIME_TYPE_MESSAGE = "plain/message";

// Bit context state cho item — PHẢI khớp với context_mask của action "Open
// in BBCord" (uds_item_action_data_set_context_mask, xem init()). Thiếu
// dòng set_context_state trên item khiến Hub không tìm được action nào
// khớp cho item đó khi tap/long-press — xem giải thích đầy đủ trong
// Zalo10's HubIntegration.cpp.
static const unsigned int HUB_CONTEXT_STATE_READ   = 0x01;
static const unsigned int HUB_CONTEXT_STATE_UNREAD = 0x02;

// category_id: theo đúng mẫu chính thức trong unified_data_source.h — mọi
// item của account này dùng chung 1 category duy nhất, không có ý nghĩa
// phân loại đặc biệt nào khác ngoài việc đây là field bắt buộc trước khi
// item có thể "actionable" (xem uds_category_added() trong init()).
static const long long HUB_CATEGORY_ID = 1;

// Giới hạn số lần thử init() cho cả phiên app, cách nhau tối thiểu
// INIT_RETRY_INTERVAL_MS mỗi lần, thay vì chỉ thử đúng 1 lần rồi latch
// vĩnh viễn. uds_init()/uds_register_client() từng ghi nhận fail thoáng
// qua lúc app mới khởi động trên thiết bị thật (service Hub của OS có
// thể chưa kịp sẵn sàng) — 5 lần / cách nhau 3s cho app khoảng 15s để
// service kịp lên trước khi bỏ cuộc hẳn cho phiên đó.
const int HubIntegration::MAX_INIT_ATTEMPTS = 5;
const qint64 HubIntegration::INIT_RETRY_INTERVAL_MS = 3000;

// Map mã lỗi uds_error_code_t (unified_data_source.h) sang tên dễ đọc
// trong log — rc số không đủ để biết ngay là lỗi gì khi đọc log trên
// thiết bị mà không có header bên cạnh.
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

// QDir::homePath() trên BB10 trả về
// "/accounts/1000/appdata/<app-id-thật>/data" — đây là công thức đã được
// xác nhận đáng tin cậy qua log runtime thực tế trên thiết bị bên Zalo10
// (đúng ở CẢ Debug lẫn Release build, khác với __progname vốn chỉ đúng
// tình cờ ở Debug — xem "FIX LẦN 11" trong Zalo10's HubIntegration.cpp để
// biết lý do đầy đủ). Kế thừa nguyên công thức này cho BBCord.
extern char *__progname;

static QString appIdFromHomePath()
{
    QStringList parts = QDir::homePath().split(QLatin1Char('/'), QString::SkipEmptyParts);
    if (parts.size() >= 2 && parts.last() == QLatin1String("data")) {
        QString appId = parts.at(parts.size() - 2);
        qDebug() << "[Hub] appId tu homePath =" << appId << "(homePath=" << QDir::homePath() << ")";
        return appId;
    }
    // Fallback: homePath() không đúng dạng mong đợi -> dùng __progname như
    // công thức cũ, thà sai path còn hơn crash hoàn toàn (Hub integration
    // là tính năng cộng thêm, không được phép làm app hỏng chức năng
    // khác).
    qDebug() << "[Hub] homePath khong dung dang mong doi (" << QDir::homePath()
              << "), fallback ve __progname cho publicAssetPath().";
    return QString::fromLatin1(__progname);
}

QString HubIntegration::publicAssetPath()
{
    // "hub-icons" phải khớp CHÍNH XÁC với dest trong bar-descriptor.xml:
    // <asset path="hub-icons" public="true">hub-icons</asset>
    // Tên KHÔNG được bắt đầu bằng chữ "assets" — xem comment dài trong
    // bar-descriptor.xml (bài học từ Zalo10: Momentics NDK 10.3.1 có vẻ
    // chặn theo tiền tố tên chuỗi trùng với rule "assets" đã khai báo).
    return QString("/apps/%1/public/hub-icons/").arg(appIdFromHomePath());
}

// Mỗi lần cài lại (kể cả cùng version), BB10 có thể sinh ra 1 app-id mới
// (hậu tố hash đổi — xem log thực tế: "...testDev_ioxd_bbcordd4b95190"),
// nên publicAssetPath() ở trên trỏ tới 1 thư mục vật lý khác. Nếu Hub vẫn
// còn giữ service registration (uds_register_client) trỏ tới assetPath
// của lần cài TRƯỚC, icon account/item sẽ hiển thị vỡ hoặc rỗng cho tới
// khi có 1 lệnh account_removed + đăng ký lại từ đầu với assetPath mới.
// uds_account_removed() remove-before-add hiện tại (xem init() bên dưới)
// chỉ chạy MỖI LẦN APP KHỞI ĐỘNG, không phân biệt "vừa cài lại" hay "chạy
// bình thường lần thứ N" — vẫn đúng, nhưng không đủ để dọn 1 service
// registration cũ đã trỏ sai assetPath nếu Hub cache lại registration đó
// trước khi account_removed kịp chạy. Hàm này lưu app-id đã dùng ở lần
// init() thành công gần nhất vào QSettings; nếu app-id hiện tại khác (hoặc
// chưa từng lưu), coi là "vừa cài lại" và trả về true để init() biết cần
// đóng bất kỳ handle UDS cũ nào trước khi đăng ký lại từ đầu, đảm bảo
// asset path luôn khớp bản cài hiện tại.
static bool detectFreshInstallAndRememberAppId(const QString &currentAppId)
{
    QSettings settings;
    const char *kLastAppIdKey = "hub/lastAppId";
    QString lastAppId = settings.value(kLastAppIdKey).toString();

    bool isFreshInstall = (lastAppId != currentAppId);
    if (isFreshInstall) {
        qDebug() << "[Hub] app-id doi tu lan chay truoc (" << lastAppId
                  << "->" << currentAppId
                  << ") - coi la vua cai lai, se dong handle UDS cu (neu co) "
                     "truoc khi dang ky lai.";
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
        // Đã hết lượt thử cho phiên này. Chỉ log đúng 1 lần ngay tại thời
        // điểm vượt ngưỡng (xem chỗ tăng m_initAttemptCount bên dưới) để
        // không spam log này lại mỗi tin nhắn tới sau đó — ở đây chỉ
        // no-op êm.
        return false;
    }

    if (m_initAttemptCount > 0 &&
        (now - m_lastInitAttemptMs) < INIT_RETRY_INTERVAL_MS) {
        // Chưa tới hạn thử lại — no-op êm, không log, tránh spam log mỗi
        // khi có tin nhắn mới dồn dập trong lúc đang chờ cooldown.
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
            qDebug() << "[Hub] Da het" << MAX_INIT_ATTEMPTS
                      << "lan thu init() - Hub integration TAT HAN cho phien app nay."
                      << "Khoi dong lai app de thu lai tu dau.";
        }
        return false;
    }
    m_udsHandle = handle;

    QString assetPath = publicAssetPath();
    QString currentAppId = appIdFromHomePath();
    bool isFreshInstall = detectFreshInstallAndRememberAppId(currentAppId);

    rc = uds_register_client(m_udsHandle, HUB_SERVICE_URL, "" /* libPath, không dùng */,
                              assetPath.toUtf8().constData());
    if (rc != UDS_SUCCESS) {
        qDebug() << "[Hub] uds_register_client failed, rc=" << rc << "(" << udsErrorName(rc) << ")"
                  << "attempt" << m_initAttemptCount << "/" << MAX_INIT_ATTEMPTS
                  << "assetPath=" << assetPath;
        uds_context_t h = static_cast<uds_context_t>(m_udsHandle);
        uds_close(&h);
        m_udsHandle = 0;
        if (m_initAttemptCount >= MAX_INIT_ATTEMPTS) {
            qDebug() << "[Hub] Da het" << MAX_INIT_ATTEMPTS
                      << "lan thu init() - Hub integration TAT HAN cho phien app nay."
                      << "Khoi dong lai app de thu lai tu dau.";
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

    // Remove-before-add mỗi lần khởi động — theo đúng "Fix Lần 10" bên
    // Zalo10 (đảm bảo icon luôn được Hub re-resolve từ assetPath hiện tại
    // của build đang chạy, bất kể sandbox path đổi giữa các build). Xem
    // giải thích đầy đủ trong Zalo10's HubIntegration.cpp.
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
    // false: account này không hỗ trợ tạo tin nhắn mới thẳng từ Hub (chưa
    // có handler cho action "bb.action.CREATE" phía app) — chỉ hiển thị +
    // mở tới channel/thread có sẵn.
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
            qDebug() << "[Hub] Da het" << MAX_INIT_ATTEMPTS
                      << "lan thu init() - Hub integration TAT HAN cho phien app nay."
                      << "Khoi dong lai app de thu lai tu dau.";
        }
        return false;
    }

    qDebug() << "[Hub] BBCord account registered in BlackBerry Hub, id=" << ACCOUNT_ID
              << "(thanh cong sau" << m_initAttemptCount << "lan thu)";
    m_ready = true;

    // category_added() PHẢI chạy trước item_added() nào dùng category_id
    // đó — xem "FIX LẦN 7" trong Zalo10's HubIntegration.cpp.
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

    // Đăng ký "Open in BBCord" — item context action (long-press). Đăng
    // ký 1 lần ở cấp account, áp dụng cho MỌI item của account này.
    uds_item_action_data_t *openAction = uds_item_action_data_create();
    uds_item_action_data_set_action(openAction, "bb.action.OPEN");
    uds_item_action_data_set_target(openAction, HUB_INVOKE_TARGET);
    // "service": 1 trong 2 giá trị hợp lệ duy nhất cho targetType theo
    // header unified_data_source.h chính thức ("card.composer" hoặc
    // "service") — xem xác nhận trong Zalo10's HubIntegration.cpp.
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
    if (!init()) return; // init() tự no-op nếu đã ready; false nghĩa là Hub không khả dụng

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
    // false (đổi từ true): tắt hẳn bundle "hiệu ứng cảnh báo" của Hub cho
    // item này — theo doc chính thức
    // (uds_inbox_item_data_set_notification_state trong
    // unified_data_source.h), true/false là công tắc CHUNG cho cả
    // banner + âm thanh hệ thống + lock-screen instant preview, không
    // tách riêng được sound khỏi banner qua API này. Đổi thành false vì
    // app tự phát ping.m4a qua playPingSound() (xem Client.cpp) rồi — để
    // true sẽ ra 2 tiếng chồng nhau (đúng bug đã báo: "2 thông báo bị
    // chơi cùng lúc"). Đánh đổi: dòng item vẫn lên Hub/badge bình thường
    // (uds_item_added/updated không phụ thuộc cờ này), nhưng banner popup
    // + lock-screen instant preview của riêng Hub cũng tắt theo, không chỉ
    // mỗi âm thanh — API không cho tắt 1 phần. Nếu sau này cần lại banner,
    // cách duy nhất là bật true lại và tắt playPingSound() thay vào đó
    // (không thể có cả 2 nguồn cùng lúc mà không đụng độ).
    uds_inbox_item_data_set_notification_state(item, false);

    // Thử update trước (trường hợp phổ biến hơn — nhiều ping cùng 1
    // channel/thread), fail thì add — Hub không có query API để hỏi
    // trước item đã tồn tại hay chưa.
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
    if (m_unreadCounts.value(sourceId, 0) == 0) return; // đã 0 sẵn (hoặc chưa từng add), tránh gọi IPC thừa
    if (!m_threadItemState.contains(sourceId)) {
        // Chưa từng có state đầy đủ nào được lưu cho item này — không có
        // gì để tái tạo đầy đủ, bỏ qua thay vì gửi 1 update thiếu field
        // (sẽ reset tên/mô tả/timestamp về rỗng — xem comment struct
        // ThreadItemState trong .hpp).
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
    // uds_item_updated() THAY THẾ TOÀN BỘ record — phải gửi lại ĐẦY ĐỦ
    // field, y hệt lần upsertThreadItem() gần nhất, chỉ đổi đúng phần
    // muốn thay đổi thật sự (icon: Read; unread_count: 0;
    // notification_state: false).
    uds_inbox_item_data_set_name(item, titleUtf8.constData());
    uds_inbox_item_data_set_description(item, previewUtf8.constData());
    uds_inbox_item_data_set_mime_type(item, HUB_MIME_TYPE_MESSAGE);
    uds_inbox_item_data_set_category_id(item, HUB_CATEGORY_ID);
    uds_inbox_item_data_set_timestamp(item, st.timestampMs);
    uds_inbox_item_data_set_total_count(item, 0);
    uds_inbox_item_data_set_icon(item, HUB_ICON_READ_FILE);
    uds_inbox_item_data_set_unread_count(item, 0);
    uds_inbox_item_data_set_context_state(item, HUB_CONTEXT_STATE_READ);
    uds_inbox_item_data_set_notification_state(item, false); // chỉ đổi badge, không muốn trigger lại effects

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
    // Không phụ thuộc m_ready/init() — cố ý, xem giải thích ở khai báo
    // trong HubIntegration.hpp. Hub (UDS) và âm thanh là 2 con đường độc
    // lập, không có lý do UDS lỗi (như đã từng gặp: thiếu quyền
    // _sys_access_pim_unified, rc=501) lại kéo theo mất luôn âm thanh.
    if (!m_pingPlayer) {
        m_pingPlayer = new bb::multimedia::MediaPlayer(this);
        // "audio/ping.m4a" (không phải "assets/audio/ping.m4a"): thư mục
        // "assets" khai trong bar-descriptor.xml
        // (<asset path="assets">assets</asset>) là gốc của scheme
        // asset:///, tự bị lược khỏi URL — đúng convention đã dùng xuyên
        // suốt code hiện có (vd "asset:///images/icons/first.png" cho file
        // thật nằm ở assets/images/icons/first.png, xem
        // MainPageController.cpp/ItemMapper.cpp).
        m_pingPlayer->setSourceUrl(QUrl("asset:///audio/ping.m4a"));
    }
    bb::multimedia::MediaError::Type err = m_pingPlayer->play();
    if (err != bb::multimedia::MediaError::None) {
        qDebug() << "[Hub] playPingSound failed, mediaError=" << err;
    }
}

