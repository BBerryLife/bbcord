#ifndef HUBINTEGRATION_HPP
#define HUBINTEGRATION_HPP

#include <QObject>
#include <QString>
#include <QMap>
#include <QSet>
#include <QUrl>

namespace bb { namespace multimedia { class MediaPlayer; } }

// Forward declare thay vì include <bb/pim/unified/unified_data_source.h> ở
// đây — header đó là C API thuần (không phải Cascades/QObject), kéo vào
// header này sẽ leak ra mọi file include HubIntegration.hpp. uds_context_t
// là typedef void*, nên forward declare bằng void* thẳng trong class là đủ,
// #include thật nằm trong HubIntegration.cpp.
//
// Class này là bản port 1:1 kiến trúc từ HubIntegration của Zalo10
// (github.com/BBerryLife/Zalo10, cùng tác giả) sang BBCord — cùng 1 pattern
// UDS (account 1 tab riêng trong Hub + inbox item theo từng thread), chỉ
// đổi account id/tên/icon cho phù hợp Discord. Xem HubIntegration.cpp bên
// Zalo10 để tra lại lịch sử điều tra chi tiết các API quirk của UDS trên
// BB10 (rất nhiều edge case không có trong doc chính thức) — không lặp lại
// toàn bộ ở đây để tránh trùng lặp, chỉ giữ phần liên quan trực tiếp tới
// BBCord/Discord.
//
// LƯU Ý QUAN TRỌNG kế thừa từ Zalo10: tại thời điểm port này, tính năng
// "single-tap vào item trong Hub để mở thẳng app" CHƯA được xác nhận hoạt
// động bên Zalo10 (xem lịch sử "Fix Lần 1..11" trong HubIntegration.cpp) —
// long-press "Open in ..." (item context action) hoạt động ổn định, nhưng
// short-tap từng im lặng hoàn toàn qua nhiều lần test trên thiết bị thật.
// BBCord kế thừa NGUYÊN VẸN cấu hình UDS đã dùng (kể cả các thử nghiệm
// "giả thuyết lần 5" như UDS_PLACEMENT_FIXED, mime type "plain/message",
// context_state) vì đây là cấu hình gần nhất/tốt nhất đã có, nhưng KHÔNG
// coi đây là đã được xác nhận sửa xong cho tới khi test trên thiết bị BB10
// thật với BBCord. Nếu single-tap vẫn im lặng sau khi build/deploy, đây là
// vấn đề đã biết từ trước, không phải lỗi mới phát sinh khi port.
class HubIntegration : public QObject
{
    Q_OBJECT
public:
    explicit HubIntegration(QObject *parent = 0);
    virtual ~HubIntegration();

    // Mở kết nối UDS + đăng ký account "BBCord" nếu chưa có. An toàn để gọi
    // nhiều lần (no-op nếu đã init thành công). Trả về false nếu UDS không
    // khởi tạo được (ví dụ chạy trên Simulator thiếu service) — mọi hàm
    // khác trong class này tự kiểm tra m_ready và no-op êm nếu init lỗi, để
    // Hub integration (tính năng cộng thêm) không bao giờ có thể làm hỏng
    // hay chặn luồng nhận tin nhắn/notification hiện có của app.
    bool init();

    // Thêm/cập nhật dòng hội thoại cho 1 kênh/DM trong tab BBCord của Hub.
    // Gọi mỗi khi có 1 tin nhắn đáng thông báo (ping trực tiếp, @everyone/
    // @here, role-mention, hoặc DM/reply tuỳ ngữ cảnh — logic quyết định
    // "có đáng thông báo hay không" nằm ở GatewayHandler, không phải ở
    // đây; class này chỉ chịu trách nhiệm hiển thị, không quyết định khi
    // nào được gọi).
    //   sourceId    : id ổn định cho dòng item — dùng channelId (kênh
    //                 guild), channelId của DM/group DM. Ổn định qua các
    //                 lần gọi cho cùng 1 cuộc hội thoại.
    //   title       : dòng đầu — "Tên Server" (guild) hoặc "Tên người gửi"
    //                 (DM/group DM). Đã build sẵn ở call site theo đúng 2
    //                 định dạng yêu cầu, class này không tự suy luận.
    //   preview     : dòng mô tả — "Tên ai ping: nội dung" hoặc
    //                 "Replied: nội dung". Đã build sẵn ở call site.
    //   timestampMs : mốc thời gian UNIX ms, quyết định thứ tự trong Hub.
    void upsertThreadItem(const QString &sourceId, const QString &title,
                          const QString &preview, qint64 timestampMs);

    // Đánh dấu đã đọc (unread_count=0) khi user mở channel/thread tương
    // ứng. Không xoá item khỏi Hub, chỉ tắt badge.
    void markThreadRead(const QString &sourceId);

    // Xoá hẳn 1 dòng khỏi tab BBCord (ví dụ khi user rời guild/đóng DM).
    // Hiện chưa có call site bắt buộc — public để dùng khi cần.
    void removeThreadItem(const QString &sourceId);

    // Phát âm thanh assets/audio/ping.m4a cho MỌI tin nhắn đáng thông báo
    // (cùng điều kiện shouldNotify với upsertThreadItem() — do
    // GatewayHandler quyết định, class này chỉ thực thi). Tách riêng khỏi
    // phần UDS/Hub bên trên: gọi được và không phụ thuộc init() hay
    // m_ready, để nếu Hub (UDS) lỗi/thiếu quyền như đã từng gặp trên thiết
    // bị thật, âm thanh vẫn phát bình thường — không có lý do 2 tính năng
    // phải chung 1 điểm hỏng. Xem HubIntegration.cpp về lựa chọn
    // bb::multimedia::MediaPlayer (chơi được file asset tự chọn) thay vì
    // bb::multimedia::SystemSound (chỉ chơi được các âm hệ thống định
    // sẵn, không nhận file custom như ping.m4a của app).
    void playPingSound();

    // Đường dẫn tuyệt đối tới thư mục asset PUBLIC đã cài đặt của app trên
    // máy ("/apps/<app-id>/public/hub-icons/"), dùng làm pAssetPath cho
    // uds_register_client() bên trong class này. Public static để dùng
    // lại nếu chỗ khác cần trỏ tới cùng 1 thư mục icon vật lý.
    static QString publicAssetPath();

private:
    Q_DISABLE_COPY(HubIntegration)

    // uds_item_updated() KHÔNG patch từng field — nó THAY THẾ TOÀN BỘ
    // record bằng đúng những gì được set trong lệnh gọi đó; field nào
    // không set sẽ bị reset về rỗng/0 (bug đã gặp thực tế bên Zalo10: tên
    // rỗng, timestamp về epoch). Vì vậy MỌI lần gọi uds_item_updated()
    // phải cung cấp ĐẦY ĐỦ toàn bộ field hiện tại của item, không chỉ
    // phần muốn đổi — struct này lưu lại đúng những gì cần để tái tạo đầy
    // đủ. Xem struct cùng tên trong HubIntegration.hpp của Zalo10.
    struct ThreadItemState {
        QString title;
        QString preview;
        qint64  timestampMs;
    };
    QMap<QString, ThreadItemState> m_threadItemState;

    void *m_udsHandle;      // uds_context_t thật, xem HubIntegration.cpp
    bool  m_ready;          // true nếu init() + account_added() thành công

    // Retry có giới hạn thay vì chỉ thử 1 lần duy nhất cho cả phiên chạy
    // app. uds_init()/uds_register_client() có thể fail thoáng qua lúc
    // app mới khởi động (ví dụ service Hub của OS chưa sẵn sàng) — nếu
    // chỉ thử 1 lần và latch vĩnh viễn như trước, cả phiên app còn lại
    // mất Hub dù service có thể đã sẵn sàng vài giây sau đó. Xem
    // init() trong HubIntegration.cpp.
    int   m_initAttemptCount;    // số lần đã thử init(), kể cả lần fail
    qint64 m_lastInitAttemptMs;  // mốc thời gian lần thử gần nhất (0 = chưa thử lần nào)
    static const int   MAX_INIT_ATTEMPTS;        // sau ngần này lần fail, dừng thử hẳn cho phiên này
    static const qint64 INIT_RETRY_INTERVAL_MS;  // khoảng cách tối thiểu giữa 2 lần thử

    // sourceId -> unread_count hiện tại đang hiển thị trên item đó, để
    // upsertThreadItem() cộng dồn thay vì Hub luôn nhảy về 1.
    QMap<QString, int> m_unreadCounts;
    // sourceId đã từng uds_item_added() thành công — quyết định add vs
    // update trong upsertThreadItem().
    QSet<QString> m_knownSourceIds;

    // Account id cố định cho BBCord trong Hub (namespace riêng, không
    // trùng với Zalo10's 424242006). Xem comment ACCOUNT_ID trong
    // Zalo10's HubIntegration.hpp để hiểu vì sao giá trị này có thể cần
    // đổi nếu gặp lại đúng các bug đã ghi lại ở đó (account dính state cũ
    // của Hub, độc lập với app, gỡ cài lại không xoá được).
    static const long long ACCOUNT_ID = 5313230001LL;

    // Tạo lazy trong playPingSound() (không tạo sẵn ở constructor — nếu
    // HubIntegration bị tạo ra nhưng không phiên nào có tin nhắn đáng
    // thông báo, không có lý do chiếm tài nguyên audio của OS sớm hơn cần
    // thiết). Parent = this nên tự huỷ theo QObject, không cần dọn tay ở
    // destructor.
    bb::multimedia::MediaPlayer *m_pingPlayer;
};

#endif // HUBINTEGRATION_HPP
