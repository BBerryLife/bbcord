#include "Logger.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>
#include <QtGlobal>

#include <cstdio>
#include <cstdlib>

namespace {

const char *kLogDirName = "data/logs";
const char *kLogFileName = "bbcord.log";
const char *kTimestampFormat = "yyyy-MM-dd HH:mm:ss.zzz";

QMutex g_logMutex;
bool g_installed = false;

QString logDirPath() {
  return QDir(QDir::homePath()).absoluteFilePath(kLogDirName);
}

QString logFilePathInternal() {
  return QDir(logDirPath()).absoluteFilePath(kLogFileName);
}

void ensureLogDirectory() {
  QDir dir(logDirPath());
  if (!dir.exists()) {
    dir.mkpath(".");
  }
}

const char *levelLabel(QtMsgType type) {
  switch (type) {
  case QtDebugMsg:
    return "DEBUG";
  case QtWarningMsg:
    return "WARN";
  case QtCriticalMsg:
    return "CRITICAL";
  case QtFatalMsg:
    return "FATAL";
  default:
    // Qt4 chỉ có 4 mức trên (không có QtInfoMsg, chỉ xuất hiện từ
    // Qt5). Giữ default để an toàn nếu enum có thêm giá trị mới.
    return "LOG";
  }
}

void appendLine(const QString &line) {
  QMutexLocker locker(&g_logMutex);
  ensureLogDirectory();

  QFile file(logFilePathInternal());
  if (!file.open(QIODevice::Append | QIODevice::Text)) {
    return;
  }

  QTextStream stream(&file);
  stream << line << "\n";
  file.close();
}

void messageHandler(QtMsgType type, const char *message) {
  QString timestamp = QDateTime::currentDateTime().toString(kTimestampFormat);
  QString line = QString("[%1] [%2] %3")
                     .arg(timestamp)
                     .arg(levelLabel(type))
                     .arg(QString::fromUtf8(message));

  appendLine(line);

  // Vẫn in ra stderr như hành vi mặc định của Qt để không phá vỡ việc
  // xem log qua console khi debug bằng Momentics/slog2.
  fprintf(stderr, "%s\n", line.toLocal8Bit().constData());

  if (type == QtFatalMsg) {
    std::abort();
  }
}

} // namespace

namespace Logger {

void install() {
  QMutexLocker locker(&g_logMutex);
  if (g_installed) {
    return;
  }

  ensureLogDirectory();
  qInstallMsgHandler(messageHandler);
  g_installed = true;
}

QString logFilePath() { return logFilePathInternal(); }

void write(const QString &message) {
  QString timestamp = QDateTime::currentDateTime().toString(kTimestampFormat);
  appendLine(QString("[%1] [INFO] %2").arg(timestamp).arg(message));
}

} // namespace Logger
