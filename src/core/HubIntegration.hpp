#ifndef HUBINTEGRATION_HPP
#define HUBINTEGRATION_HPP

#include <QObject>
#include <QString>
#include <QMap>
#include <QSet>
#include <QUrl>

namespace bb { namespace multimedia { class MediaPlayer; } }

// uds_context_t is a void*; forward-declared as void* so the plain C header
// <bb/pim/unified/unified_data_source.h> stays out of this header.
// Hub tab per account + one inbox item per thread. The account target_name
// must be HUB_INVOKE_TARGET (not the app id) or a short tap does nothing.
class HubIntegration : public QObject
{
    Q_OBJECT
public:
    explicit HubIntegration(QObject *parent = 0);
    virtual ~HubIntegration();

    // Opens the UDS connection and registers the account. Safe to call repeatedly.
    // Returns false if UDS is unavailable; other methods then no-op quietly.
    bool init();

    // Adds/updates a conversation row in the Hub tab. Whether a message is
    // notify-worthy is decided by GatewayHandler, not here.
    //   sourceId    : stable item id (channelId of the guild channel or DM)
    //   title       : first line (server or sender name), built by the caller
    //   preview     : description line, built by the caller
    //   timestampMs : UNIX ms timestamp, used for Hub ordering
    void upsertThreadItem(const QString &sourceId, const QString &title,
                          const QString &preview, qint64 timestampMs);

    // Marks the item read (unread_count = 0); the item stays in Hub.
    void markThreadRead(const QString &sourceId);

    // Removes the row from the BBCord tab. Currently unused.
    void removeThreadItem(const QString &sourceId);

    // Plays assets/audio/ping.m4a for every notify-worthy message. Independent
    // of init()/m_ready so a UDS failure never silences it. MediaPlayer is used
    // because SystemSound cannot play a custom file.
    void playPingSound();

    // Public asset folder on device ("/apps/<app-id>/public/hub-icons/"),
    // passed as pAssetPath to uds_register_client().
    static QString publicAssetPath();

private slots:
    // Deferred entry point for playPingSound() (runs in a separate event-loop turn).
    void onPlayPingSoundDeferred();
    // Deferred retry of the first play() while setSourceUrl()/prepare() finish asynchronously.
    void onPingPlayerReadyRetry();

private:
    Q_DISABLE_COPY(HubIntegration)

    // Calls play(); on failure discards m_pingPlayer so the next ping builds a fresh one.
    void playOnPingPlayerOrResetForRetry();

    // uds_item_updated() replaces the whole record, so every update must resend
    // all fields. This struct stores the full current state of an item.
    struct ThreadItemState {
        QString title;
        QString preview;
        qint64  timestampMs;
    };
    QMap<QString, ThreadItemState> m_threadItemState;

    void *m_udsHandle;      // the real uds_context_t, see HubIntegration.cpp
    bool  m_ready;          // true if init() + account_added() succeeded

    // Init is retried a limited number of times per session, since
    // uds_init()/uds_register_client() can fail transiently at startup.
    int   m_initAttemptCount;    // number of init() attempts so far, including failures
    qint64 m_lastInitAttemptMs;  // timestamp of the most recent attempt (0 = never tried)
    static const int   MAX_INIT_ATTEMPTS;        // after this many failures, stop retrying for this session
    static const qint64 INIT_RETRY_INTERVAL_MS;  // minimum gap between two attempts

    // sourceId -> unread_count shown on that item, so pings accumulate.
    QMap<QString, int> m_unreadCounts;
    // sourceIds that have had a successful uds_item_added() — decides
    // add vs update in upsertThreadItem().
    QSet<QString> m_knownSourceIds;

    // Fixed Hub account id for BBCord.
    static const long long ACCOUNT_ID = 5313230001LL;

    // Created in the constructor (not lazily) to avoid racing play() against the
    // async setSourceUrl(). Parented to this, so no manual cleanup.
    bb::multimedia::MediaPlayer *m_pingPlayer;
    // True once m_pingPlayer's source has been set (setup runs only once).
    bool m_pingPlayerSourceSet;

    // MediaPlayer can fail to connect to mm-renderer right after startup;
    // the same ping is retried a few times with a short delay.
    int m_pingRetryCount;
    static const int MAX_PING_RETRIES;
    static const int PING_RETRY_DELAY_MS;
};

#endif // HUBINTEGRATION_HPP
