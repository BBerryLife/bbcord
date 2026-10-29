#include "HubShared.hpp"

#include <bb/pim/unified/unified_data_source.h>

#include <QByteArray>
#include <QDebug>
#include <QDir>
#include <QSettings>
#include <QStringList>

extern char *__progname;

namespace HubShared {

bool isItemAction(const QString &action)
{
    return action == QLatin1String(ACTION_MARK_READ)
        || action == QLatin1String(ACTION_MARK_UNREAD)
        || action == QLatin1String(ACTION_DELETE);
}

// On BB10, QDir::homePath() is "/accounts/1000/appdata/<real-app-id>/data" for the UI and
// for the headless service alike (same package), so both derive the same app id.
QString publicAssetPath()
{
    QString appId;
    QStringList parts = QDir::homePath().split(QLatin1Char('/'), QString::SkipEmptyParts);
    if (parts.size() >= 2 && parts.last() == QLatin1String("data")) {
        appId = parts.at(parts.size() - 2);
    } else {
        // __progname is the entry point name, not the app id; a wrong path is still better
        // than crashing (Hub is an optional feature).
        appId = QString::fromLatin1(__progname);
    }
    // Must match the dest of the public asset in bar-descriptor.xml; the name must not
    // start with "assets" (NDK 10.3.1 rejects it).
    return QString("/apps/%1/public/hub-icons/").arg(appId);
}

static QSettings *openSettings()
{
    // Explicit organization/application: the UI and the service must open the same file
    // regardless of what each process set as its application name.
    return new QSettings(QLatin1String("BBerryLife"), QLatin1String("BBCordHub"));
}

bool loadState(const QString &channelId, ItemState &state)
{
    if (channelId.isEmpty()) return false;
    QSettings *s = openSettings();
    s->sync(); // pick up what the other process wrote
    s->beginGroup(QLatin1String("items"));
    s->beginGroup(channelId);
    bool found = s->contains(QLatin1String("title"));
    if (found) {
        state.title       = s->value(QLatin1String("title")).toString();
        state.preview     = s->value(QLatin1String("preview")).toString();
        state.timestampMs = s->value(QLatin1String("ts")).toLongLong();
        state.unread      = s->value(QLatin1String("unread"), 0).toInt();
        state.total       = s->value(QLatin1String("total"), qMax(state.unread, 1)).toInt();
    }
    delete s;
    return found;
}

void saveState(const QString &channelId, const ItemState &state)
{
    if (channelId.isEmpty()) return;
    QSettings *s = openSettings();
    s->beginGroup(QLatin1String("items"));
    s->beginGroup(channelId);
    s->setValue(QLatin1String("title"), state.title);
    s->setValue(QLatin1String("preview"), state.preview);
    s->setValue(QLatin1String("ts"), state.timestampMs);
    s->setValue(QLatin1String("unread"), state.unread);
    s->setValue(QLatin1String("total"), state.total);
    s->sync();
    delete s;
}

void forgetState(const QString &channelId)
{
    if (channelId.isEmpty()) return;
    QSettings *s = openSettings();
    s->beginGroup(QLatin1String("items"));
    s->remove(channelId);
    s->sync();
    delete s;
}

void clearAllState()
{
    QSettings *s = openSettings();
    s->remove(QLatin1String("items"));
    s->remove(QLatin1String("events"));
    s->sync();
    delete s;
}

QString accountAppId()
{
    QSettings *s = openSettings();
    s->sync();
    QString v = s->value(QLatin1String("account/appId")).toString();
    delete s;
    return v;
}

void setAccountAppId(const QString &appId)
{
    QSettings *s = openSettings();
    s->setValue(QLatin1String("account/appId"), appId);
    s->sync();
    delete s;
}

void postEvent(const QString &action, const QString &channelId)
{
    if (action.isEmpty() || channelId.isEmpty()) return;
    QSettings *s = openSettings();
    s->sync();
    QStringList list = s->value(QLatin1String("events/list")).toStringList();
    list.append(action + QLatin1Char('\t') + channelId);
    // The UI drains this every few seconds; if it is not running, keep only the newest.
    while (list.size() > 50) list.removeFirst();
    s->setValue(QLatin1String("events/list"), list);
    s->sync();
    delete s;
}

QStringList takeEvents()
{
    QSettings *s = openSettings();
    s->sync();
    QStringList list = s->value(QLatin1String("events/list")).toStringList();
    if (!list.isEmpty()) {
        s->remove(QLatin1String("events/list"));
        s->sync();
    }
    delete s;
    return list;
}

void *openUds()
{
    uds_context_t handle = 0;
    int rc = uds_init(&handle, false /* synchronous */);
    if (rc != UDS_SUCCESS || !handle) {
        qWarning() << "[HubShared] uds_init failed, rc=" << rc;
        return 0;
    }

    QByteArray assetPath = publicAssetPath().toUtf8();
    rc = uds_register_client(handle, SERVICE_URL, "" /* libPath, unused */,
                             assetPath.constData());
    if (rc != UDS_SUCCESS) {
        qWarning() << "[HubShared] uds_register_client failed, rc=" << rc;
        uds_close(&handle);
        return 0;
    }
    return handle;
}

void closeUds(void *udsHandle)
{
    if (!udsHandle) return;
    uds_context_t h = static_cast<uds_context_t>(udsHandle);
    uds_close(&h);
}

bool publishItem(void *udsHandle, const QString &channelId, const ItemState &st,
                 bool notify, bool allowAdd)
{
    if (!udsHandle || channelId.isEmpty()) return false;

    QByteArray idUtf8      = channelId.toUtf8();
    QByteArray titleUtf8   = st.title.toUtf8();
    QByteArray previewUtf8 = st.preview.toUtf8();

    uds_inbox_item_data_t *item = uds_inbox_item_data_create();
    uds_inbox_item_data_set_account_id(item, ACCOUNT_ID);
    uds_inbox_item_data_set_source_id(item, const_cast<char *>(idUtf8.constData()));
    uds_inbox_item_data_set_name(item, titleUtf8.constData());
    uds_inbox_item_data_set_description(item, previewUtf8.constData());
    uds_inbox_item_data_set_mime_type(item, MIME_TYPE);
    uds_inbox_item_data_set_category_id(item, CATEGORY_ID);
    uds_inbox_item_data_set_timestamp(item, st.timestampMs);
    uds_inbox_item_data_set_icon(item, st.unread > 0 ? ICON_UNREAD : ICON_READ);
    uds_inbox_item_data_set_unread_count(item, st.unread);
    uds_inbox_item_data_set_total_count(item, qMax(st.total, qMax(st.unread, 1)));
    uds_inbox_item_data_set_context_state(item, st.unread > 0 ? CONTEXT_UNREAD : CONTEXT_READ);
    uds_inbox_item_data_set_notification_state(item, notify);

    int rc = uds_item_updated(static_cast<uds_context_t>(udsHandle), item);
    if (rc != UDS_SUCCESS && allowAdd) {
        rc = uds_item_added(static_cast<uds_context_t>(udsHandle), item);
    }
    uds_inbox_item_data_destroy(item);

    if (rc != UDS_SUCCESS) {
        qWarning() << "[HubShared] publishItem failed for" << channelId << "rc=" << rc;
    }
    return rc == UDS_SUCCESS;
}

bool removeItem(void *udsHandle, const QString &channelId)
{
    if (!udsHandle || channelId.isEmpty()) return false;
    QByteArray idUtf8 = channelId.toUtf8();
    int rc = uds_item_removed(static_cast<uds_context_t>(udsHandle), ACCOUNT_ID,
                              const_cast<char *>(idUtf8.constData()));
    return rc == UDS_SUCCESS;
}

}
