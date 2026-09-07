#ifndef Logger_HPP_
#define Logger_HPP_

#include <QString>

/*!
 * @brief Logs the whole app to a file, with a timestamp per line.
 *
 * install() should be called as early as possible (start of main()),
 * before any qDebug/qWarning/qCritical/qFatal call, so no logs are missed.
 * All Qt log framework messages (qDebug, qWarning, qCritical, qFatal) are
 * automatically written to the log file with a timestamp, while still
 * being printed to console/stderr as usual.
 */
namespace Logger {

// Installs the message handler that logs to a file. Safe to call
// multiple times (only installs on first call).
void install();

// Absolute path to the current log file
// (home/data/logs/bbcord.log).
QString logFilePath();

// Manually writes a timestamped log line, bypassing qDebug/etc.
// Used for key events (e.g. app start/stop, login, logout).
void write(const QString &message);

} // namespace Logger

#endif /* Logger_HPP_ */
