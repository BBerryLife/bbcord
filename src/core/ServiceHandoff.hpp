#ifndef SERVICEHANDOFF_HPP
#define SERVICEHANDOFF_HPP

// Gateway handoff between the UI app and the BBCordHeadless service.
//
// Only one of them keeps the Discord gateway connection open at a time:
//  - the UI is running  -> it owns the connection, the service drops its own;
//  - the UI is closed   -> the service connects and keeps notifying through Hub.
// The UI pid file is the source of truth. The invokes below only make the service react
// faster (and start it if it is not running); the service also polls the file.
//
// Header-only and shared by both projects (the service includes it from the BBCord tree).

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QString>

#include <bb/system/InvokeManager>
#include <bb/system/InvokeRequest>
#include <bb/system/InvokeTargetReply>

#include <errno.h>
#include <signal.h>
#include <sys/types.h>

#include "HubShared.hpp"

namespace ServiceHandoff {

// Custom actions declared in the service's <invoke-target> filter (bar-descriptor.xml).
static const char *const ACTION_UI_OPENED = "ch.michioxd.bbcord.service.UI_OPENED";
static const char *const ACTION_UI_CLOSED = "ch.michioxd.bbcord.service.UI_CLOSED";

inline QString pidFilePath() { return QDir::homePath() + "/bbcord_ui.pid"; }

inline void markUiAlive()
{
    QFile f(pidFilePath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QByteArray::number(static_cast<qlonglong>(QCoreApplication::applicationPid())));
    }
}

inline void markUiGone() { QFile::remove(pidFilePath()); }

inline bool isUiAlive()
{
    QFile f(pidFilePath());
    if (!f.open(QIODevice::ReadOnly)) return false;
    bool ok = false;
    qlonglong pid = f.readAll().trimmed().toLongLong(&ok);
    if (!ok || pid <= 0) return false;
    // Signal 0 only checks that the process exists (EPERM still means it does).
    return (::kill(static_cast<pid_t>(pid), 0) == 0) || (errno == EPERM);
}

inline bb::system::InvokeTargetReply *pingService(bb::system::InvokeManager *im,
                                                  const char *action)
{
    if (!im) return 0;
    bb::system::InvokeRequest req;
    req.setTarget(HubShared::SERVICE_TARGET);
    req.setAction(action);
    return im->invoke(req);
}

}

#endif
