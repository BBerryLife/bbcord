#ifndef HUBINTEGRATION_HPP
#define HUBINTEGRATION_HPP

#include <QObject>
#include <QString>
#include <QMap>
#include <QSet>
#include <QUrl>

namespace bb { namespace multimedia { class MediaPlayer; } }

// Forward declared instead of including
// <bb/pim/unified/unified_data_source.h> here — that header is a plain
// C API (not Cascades/QObject), pulling it into this header would leak
// out to every file that includes HubIntegration.hpp. uds_context_t is
// a void* typedef, so forward-declaring with void* directly in the
// class is enough - the real #include lives in HubIntegration.cpp.
//
// This class is a 1:1 architecture port from Zalo10's HubIntegration
// (github.com/BBerryLife/Zalo10, same author) to BBCord — same UDS
// pattern (a dedicated Hub tab per account + one inbox item per
// thread), just swapping account id/name/icon for Discord. See
// HubIntegration.cpp in Zalo10 for the detailed investigation history
// of UDS API quirks on BB10 (many edge cases not in the official docs)
// — not repeated in full here to avoid duplication, only keeping what's
// directly relevant to BBCord/Discord.
//
// IMPORTANT NOTE inherited from Zalo10: at the time of this port,
// "single-tap a Hub item to open the app directly" was NOT confirmed
// working on Zalo10 (see the "Fix Attempt 1..11" history in
// HubIntegration.cpp) — long-press "Open in ..." (item context action)
// worked reliably, but short-tap stayed completely silent across
// repeated real-device testing. BBCord inherits the UDS config used
// there AS-IS (including "attempt 5 hypothesis" experiments like
// UDS_PLACEMENT_FIXED, mime type "plain/message", context_state) since
// it's the closest/best config available so far, but this should NOT
// be treated as confirmed fixed until tested on real BB10 hardware with
// BBCord. If single-tap is still silent after build/deploy, this is a
// known pre-existing issue, not a new bug introduced by the port.
class HubIntegration : public QObject
{
    Q_OBJECT
public:
    explicit HubIntegration(QObject *parent = 0);
    virtual ~HubIntegration();

    // Opens the UDS connection + registers the "BBCord" account if not
    // already present. Safe to call multiple times (no-op if already
    // init'd successfully). Returns false if UDS fails to initialize
    // (e.g. running on a Simulator missing the service) — every other
    // function in this class checks m_ready itself and no-ops quietly
    // on init failure, so Hub integration (an add-on feature) can never
    // break or block the app's existing message/notification pipeline.
    bool init();

    // Adds/updates a conversation row for a channel/DM in BBCord's Hub
    // tab. Called whenever there's a notify-worthy message (a direct
    // ping, @everyone/@here, role-mention, or DM/reply depending on
    // context — the logic deciding "is this worth notifying" lives in
    // GatewayHandler, not here; this class is only responsible for
    // display, not deciding when it's called).
    //   sourceId    : stable id for the item row — uses the guild
    //                 channelId, or the channelId of a DM/group DM.
    //                 Stable across calls for the same conversation.
    //   title       : first line — "Server Name" (guild) or "Sender
    //                 Name" (DM/group DM). Already built at the call
    //                 site in the required format, this class doesn't
    //                 infer it.
    //   preview     : description line — "Who pinged: content" or
    //                 "Replied: content". Already built at the call
    //                 site.
    //   timestampMs : UNIX timestamp in ms, determines ordering in Hub.
    void upsertThreadItem(const QString &sourceId, const QString &title,
                          const QString &preview, qint64 timestampMs);

    // Marks read (unread_count=0) when the user opens the matching
    // channel/thread. Doesn't remove the item from Hub, only clears the
    // badge.
    void markThreadRead(const QString &sourceId);

    // Removes a row from the BBCord tab entirely (e.g. when the user
    // leaves a guild/closes a DM). No mandatory call site currently —
    // public for use when needed.
    void removeThreadItem(const QString &sourceId);

    // Plays assets/audio/ping.m4a for EVERY notify-worthy message (same
    // shouldNotify condition as upsertThreadItem() — decided by
    // GatewayHandler, this class only executes it). Kept separate from
    // the UDS/Hub logic above: callable without depending on init() or
    // m_ready, so if Hub (UDS) fails/lacks permission as has happened
    // on real devices before, the sound still plays normally — no
    // reason for the two features to share a single point of failure.
    // See HubIntegration.cpp for why bb::multimedia::MediaPlayer was
    // chosen (can play a custom asset file) over
    // bb::multimedia::SystemSound (only plays predefined system sounds,
    // doesn't accept a custom file like the app's ping.m4a).
    void playPingSound();

    // Absolute path to the app's installed PUBLIC asset folder on
    // device ("/apps/<app-id>/public/hub-icons/"), used as pAssetPath
    // for uds_register_client() inside this class. Public static so it
    // can be reused elsewhere that needs to point at the same physical
    // icon folder.
    static QString publicAssetPath();

private slots:
    // Fix: deferred entry point for playPingSound() - see the long
    // comment there for why the whole call (not just the final
    // play()) is pushed to a separate event-loop turn.
    void onPlayPingSoundDeferred();
    // Fix: deferred retry for the FIRST ping's play() call - see the
    // long comment in onPlayPingSoundDeferred() for why a short delay
    // (rather than a mediaStateChanged() signal connection, whose
    // exact enum values this codebase can't currently verify) is used
    // to work around setSourceUrl()/prepare() being asynchronous.
    void onPingPlayerReadyRetry();

private:
    Q_DISABLE_COPY(HubIntegration)

    // Fix: shared by onPlayPingSoundDeferred() (first play attempt
    // once source is set) and onPingPlayerReadyRetry() (deferred first
    // play) - calls play() and, on failure, discards m_pingPlayer so
    // the NEXT ping constructs a fresh one instead of repeating calls
    // on a connection already known to be broken. See the long
    // comment on its definition in HubIntegration.cpp.
    void playOnPingPlayerOrResetForRetry();

    // uds_item_updated() does NOT patch individual fields — it REPLACES
    // THE WHOLE record with exactly what's set in that call; any field
    // not set gets reset to empty/0 (a real bug hit in Zalo10: name
    // went empty, timestamp reset to epoch). So EVERY uds_item_updated()
    // call must supply the item's FULL current set of fields, not just
    // the part being changed — this struct stores exactly what's needed
    // to reconstruct that. See the same-named struct in Zalo10's
    // HubIntegration.hpp.
    struct ThreadItemState {
        QString title;
        QString preview;
        qint64  timestampMs;
    };
    QMap<QString, ThreadItemState> m_threadItemState;

    void *m_udsHandle;      // the real uds_context_t, see HubIntegration.cpp
    bool  m_ready;          // true if init() + account_added() succeeded

    // Limited retries instead of only trying once per app session.
    // uds_init()/uds_register_client() can fail transiently right at
    // app startup (e.g. the OS's Hub service isn't ready yet) — trying
    // only once and latching permanently, as before, would lose Hub for
    // the rest of the session even if the service becomes ready a few
    // seconds later. See init() in HubIntegration.cpp.
    int   m_initAttemptCount;    // number of init() attempts so far, including failures
    qint64 m_lastInitAttemptMs;  // timestamp of the most recent attempt (0 = never tried)
    static const int   MAX_INIT_ATTEMPTS;        // after this many failures, stop retrying for this session
    static const qint64 INIT_RETRY_INTERVAL_MS;  // minimum gap between two attempts

    // sourceId -> current unread_count shown on that item, so
    // upsertThreadItem() can accumulate instead of the Hub always
    // jumping back to 1.
    QMap<QString, int> m_unreadCounts;
    // sourceIds that have had a successful uds_item_added() — decides
    // add vs update in upsertThreadItem().
    QSet<QString> m_knownSourceIds;

    // Fixed account id for BBCord in Hub (own namespace, distinct from
    // Zalo10's 424242006). See the ACCOUNT_ID comment in Zalo10's
    // HubIntegration.hpp for why this value might need changing if the
    // same bugs documented there resurface (account stuck with stale
    // Hub state, independent of the app, not cleared by reinstall).
    static const long long ACCOUNT_ID = 5313230001LL;

    // Fix: created (and its source set) in the constructor now, not
    // lazily in playPingSound() - confirmed via real logs that
    // creating it AND calling play() back-to-back in the same call (as
    // playPingSound() used to do) raced against
    // MediaPlayer::setSourceUrl()'s async nature, surfacing as an
    // "attach input" failure followed by a play() error on the actual
    // first ping. Parent = this so it self-destructs via QObject, no
    // manual cleanup needed in the destructor.
    bb::multimedia::MediaPlayer *m_pingPlayer;
    // Fix: true once m_pingPlayer's source has been set at least once -
    // guards the one-time setSourceUrl()+prepare() setup in
    // playPingSound() from re-running on every ping.
    bool m_pingPlayerSourceSet;

    // Fix: confirmed via real logs that MediaPlayer's constructor can
    // fail to connect to mm-renderer (the simulator's audio service)
    // for a stretch right after app/Hub startup - "Unable to connect
    // to MMR", every call on that instance then fails with "MMR
    // context is null". Previously this meant total silence for the
    // rest of the ping that triggered it (discard, wait for the NEXT
    // notify-worthy message to try again) - if mm-renderer just wasn't
    // ready yet at startup, that could mean no ping sound at all until
    // some later message happened to arrive after it recovered.
    // Instead, retry the SAME failed ping a few times with a short
    // backoff, giving mm-renderer a chance to come up, before finally
    // giving up on it.
    int m_pingRetryCount;
    static const int MAX_PING_RETRIES;
    static const int PING_RETRY_DELAY_MS;
};

#endif // HUBINTEGRATION_HPP
