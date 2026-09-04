#ifndef Logger_HPP_
#define Logger_HPP_

#include <QString>

/*!
 * @brief Ghi log toàn app ra file, kèm timestamp cho từng dòng.
 *
 * install() cần được gọi sớm nhất có thể (đầu main()), trước khi bất kỳ
 * qDebug/qWarning/qCritical/qFatal nào được gọi, để không bỏ sót log nào.
 * Mọi message Qt log framework (qDebug, qWarning, qCritical, qFatal) sẽ
 * tự động được ghi vào file log kèm timestamp, đồng thời vẫn in ra
 * console/stderr như bình thường.
 */
namespace Logger {

// Cài đặt message handler ghi log ra file. An toàn khi gọi nhiều lần
// (chỉ cài lần đầu).
void install();

// Đường dẫn tuyệt đối tới file log hiện tại
// (home/data/logs/bbcord.log).
QString logFilePath();

// Ghi thủ công 1 dòng log kèm timestamp, không thông qua qDebug/...
// Dùng cho các mốc quan trọng (vd: app start/stop, login, logout).
void write(const QString &message);

} // namespace Logger

#endif /* Logger_HPP_ */
