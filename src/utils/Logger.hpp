#ifndef Logger_HPP_
#define Logger_HPP_

#include <QString>

/** Logs the whole app to a file with a timestamp per line.
 *  Call install() at the start of main(), before any qDebug/qWarning/qCritical/qFatal,
 *  so no messages are missed; they are still printed to the console. */
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
