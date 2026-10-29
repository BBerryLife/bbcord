#ifndef HUBSHARED_HPP
#define HUBSHARED_HPP

// Hub (UDS) pieces shared by the UI app (HubIntegration) and the BBCordHeadless service,
// which compiles HubShared.cpp straight from this folder. Keeping them in one place means
// both processes agree on the account id, targets, icons and on the persisted item state.
//
// Why two processes: Hub's item actions (Mark Read / Mark Unread / Delete) are brokered by
// the *account's* target. If that target were the UI app, every tap on them would launch
// and foreground BBCord. The headless service is the account target instead: it handles
// those actions silently and forwards OPEN/VIEW to the UI.
//
// Nothing here includes the UDS header (plain C API); HubShared.cpp does.

#include <QString>
#include <QStringList>

namespace HubShared {

// Must never change once released: Hub keys the account and its items by this id.
static const long long ACCOUNT_ID  = 5313230001LL;
// All items share a single category (required for actionable items).
static const long long CATEGORY_ID = 1;

static const char *const SERVICE_URL    = "ch.michioxd.bbcord.hub";
// <invoke-target id> of the UI (type "application") and of the headless service
// (type "application.headless"); both are declared in bar-descriptor.xml.
static const char *const UI_TARGET      = "ch.michioxd.bbcord.invoke";
static const char *const SERVICE_TARGET = "ch.michioxd.bbcord.service";
// App-specific vendor mime type: a generic one made short-tap on a Hub item do nothing.
static const char *const MIME_TYPE      = "application/vnd.bbcord.hub.chat";

// Item icons; they live in the public hub-icons/ asset folder.
static const char *const ICON_UNREAD = "HubItemUnread.png";
static const char *const ICON_READ   = "HubItemRead.png";

// Context state bits; must match the context_mask of the registered item actions.
static const unsigned int CONTEXT_READ   = 0x01;
static const unsigned int CONTEXT_UNREAD = 0x02;

// Action names Hub sends (InvokeRequest::action()); must match <action> in bar-descriptor.xml.
static const char *const ACTION_OPEN        = "bb.action.OPEN";
static const char *const ACTION_VIEW        = "bb.action.VIEW";
static const char *const ACTION_MARK_READ   = "bb.action.MARKREAD";
static const char *const ACTION_MARK_UNREAD = "bb.action.MARKUNREAD";
static const char *const ACTION_DELETE      = "bb.action.DELETE";

// True for the actions the headless service handles itself (they never open the chat).
bool isItemAction(const QString &action);

// uds_item_updated() REPLACES the whole record, so every update has to resend all fields.
// This is what is needed to rebuild them, persisted per channel (see saveState()).
struct ItemState {
    QString title;
    QString preview;
    qint64  timestampMs;
    int     unread;
    int     total;
    ItemState() : timestampMs(0), unread(0), total(0) {}
};

// Absolute path of the app's public hub-icons folder, as uds_register_client() expects.
QString publicAssetPath();

// Persistent item state, shared by both processes through QSettings("BBerryLife","BBCordHub").
// loadState() returns false when nothing is stored for the channel.
bool loadState(const QString &channelId, ItemState &state);
void saveState(const QString &channelId, const ItemState &state);
void forgetState(const QString &channelId);
// The UI recreates the Hub account at every start (which empties it), so it clears the rest.
void clearAllState();

// App id the Hub account was last created for (set by the process that creates it). The
// service attaches to the existing account only when this matches the current app id.
QString accountAppId();
void setAccountAppId(const QString &appId);

// Tiny service -> UI mailbox: the service records what it did to an item so a running UI can
// mirror it in the app (unread dots). Each entry is "<action>\t<channelId>".
void postEvent(const QString &action, const QString &channelId);
QStringList takeEvents();

// Minimal UDS client for processes that only need to touch existing items (the service):
// uds_init + uds_register_client, no account/category/action registration. Returns 0 if Hub
// is unavailable. Release with closeUds().
void *openUds();
void  closeUds(void *udsHandle);

// Sends the full stored state of an item to Hub (icon, counts and context state follow
// state.unread). notify=true lets Hub raise a banner/sound; allowAdd falls back to
// uds_item_added() when the item is not in Hub.
bool publishItem(void *udsHandle, const QString &channelId, const ItemState &state,
                 bool notify, bool allowAdd);
bool removeItem(void *udsHandle, const QString &channelId);

}

#endif
