#ifndef SERVICESESSION_HPP
#define SERVICESESSION_HPP

// State the BBCord UI leaves behind so the headless service can take over the SAME Discord
// gateway session with a RESUME (op 6) instead of a fresh IDENTIFY.
//
// Why: the service is limited to ~3 MB of heap (RLIMIT_DATA) but the READY payload that
// answers an IDENTIFY is ~5 MB, so a service that has to IDENTIFY can never finish. A RESUME
// does not produce a READY at all - Discord just replays the few events missed since `seq`.
//
// Two small files in the app data folder (shared by the UI and the service):
//   bbcord_session.json  written by DiscordGateway: session_id, resume_gateway_url, seq
//   bbcord_profile.json  written by the UI:        what a notification needs to be built
//                         (own user id, guild names, own role ids, DM names)
//
// Header-only: it is included from both projects.

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <bb/data/JsonDataAccess>

#include "AppStore.hpp"
#include "models/Models.hpp"

namespace ServiceSession {

inline QString sessionPath() { return QDir::homePath() + "/bbcord_session.json"; }
inline QString profilePath() { return QDir::homePath() + "/bbcord_profile.json"; }

// Writes via a temp file + rename so a reader never sees a half-written file.
inline bool writeJsonFile(const QString &path, const QVariantMap &root)
{
    bb::data::JsonDataAccess json;
    QByteArray bytes;
    json.saveToBuffer(root, &bytes);
    if (json.hasError() || bytes.isEmpty()) return false;

    const QString tmp = path + ".tmp";
    QFile f(tmp);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    bool ok = (f.write(bytes) == bytes.size());
    f.close();
    if (!ok) { QFile::remove(tmp); return false; }
    QFile::remove(path);
    return QFile::rename(tmp, path);
}

inline bool readJsonFile(const QString &path, QVariantMap *root)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QByteArray bytes = f.readAll();
    f.close();
    bb::data::JsonDataAccess json;
    QVariant parsed = json.loadFromBuffer(bytes);
    if (json.hasError()) return false;
    *root = parsed.toMap();
    return !root->isEmpty();
}

// ---- gateway session (written by DiscordGateway) -------------------------------------

struct GatewayState {
    QString sessionId;
    QString resumeUrl;
    int     seq;
    qint64  savedMs;
    GatewayState() : seq(-1), savedMs(0) {}
};

inline bool saveGateway(const QString &sessionId, const QString &resumeUrl, int seq)
{
    if (sessionId.isEmpty()) return false;
    QVariantMap m;
    m["session_id"] = sessionId;
    m["resume_gateway_url"] = resumeUrl;
    m["seq"] = seq;
    // Kept as a string: JSON numbers go through double in some parsers.
    m["saved_ms"] = QString::number(QDateTime::currentMSecsSinceEpoch());
    return writeJsonFile(sessionPath(), m);
}

inline bool loadGateway(GatewayState *out)
{
    QVariantMap m;
    if (!readJsonFile(sessionPath(), &m)) return false;
    out->sessionId = m.value("session_id").toString();
    out->resumeUrl = m.value("resume_gateway_url").toString();
    out->seq = m.value("seq", -1).toInt();
    out->savedMs = m.value("saved_ms").toString().toLongLong();
    return !out->sessionId.isEmpty() && out->seq >= 0;
}

inline void clearGateway() { QFile::remove(sessionPath()); }

// ---- notification profile (written by the UI, read by the service) --------------------

inline bool saveProfile(AppStore *store)
{
    if (!store || store->currentUserId().isEmpty()) return false;

    QVariantMap root;
    root["user_id"] = store->currentUserId();

    QVariantList guilds;
    QVariantMap roles;
    const QVariantList src = store->guilds();
    for (int i = 0; i < src.size(); ++i) {
        QVariantMap g = src.at(i).toMap();
        const QString id = g.value("id").toString();
        if (id.isEmpty()) continue;
        QVariantMap o;
        o["id"] = id;
        o["name"] = g.value("name").toString();
        guilds.append(o);
        const QStringList r = store->currentUserRoleIdsForGuild(id);
        if (!r.isEmpty()) roles[id] = r;
    }
    root["guilds"] = guilds;
    root["roles"] = roles;

    QVariantList dms;
    const QVariantList dsrc = store->dmChannels();
    for (int i = 0; i < dsrc.size(); ++i) {
        QVariantMap d = dsrc.at(i).toMap();
        QVariantMap o;
        o["id"] = d.value("id").toString();
        o["name"] = d.value("name").toString();
        o["isGroup"] = d.value("isGroup").toBool();
        dms.append(o);
    }
    root["dms"] = dms;
    return writeJsonFile(profilePath(), root);
}

// Fills `store` with just enough for GatewayHandler::buildMentionNotification().
inline bool loadProfile(AppStore *store, QString *userId)
{
    QVariantMap root;
    if (!readJsonFile(profilePath(), &root)) return false;
    const QString uid = root.value("user_id").toString();
    if (uid.isEmpty()) return false;

    DiscordUser user;
    user.id = uid;
    store->setCurrentUser(user);
    store->setGuilds(root.value("guilds").toList());
    store->setDmChannels(root.value("dms").toList());

    const QVariantMap roles = root.value("roles").toMap();
    for (QVariantMap::const_iterator it = roles.constBegin(); it != roles.constEnd(); ++it) {
        store->setCurrentUserRoleIdsForGuild(it.key(), it.value().toStringList());
    }
    if (userId) *userId = uid;
    return true;
}

}

#endif
