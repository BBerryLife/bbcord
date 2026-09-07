#ifndef JsonParser_HPP_
#define JsonParser_HPP_

#include <QByteArray>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

class DiscordJsonParser {
public:
  struct GatewayPayload {
    GatewayPayload() : op(-1), sequence(-1), valid(false) {}

    int op;
    int sequence;
    QString eventName;
    QVariantMap data;
    QString errorMessage;
    bool valid;
  };

  static GatewayPayload parseGatewayPayload(const QByteArray &bytes);
  static QVariantMap parseObject(const QByteArray &bytes,
                                 QString *errorMessage = 0);
  static QVariantList parseArray(const QByteArray &bytes,
                                 QString *errorMessage = 0);
  static QByteArray buildIdentifyPayload(const QString &token,
                                         QString *errorMessage = 0);
  static QByteArray buildGuildSubscribePayload(const QString &guildId,
                                               const QString &channelId,
                                               QString *errorMessage = 0);
  // Minimal op:14 payload, used ONLY for member-list sync (Members
  // sheet). Unlike buildGuildSubscribePayload(), it does NOT send the
  // "guild_subscriptions" field — not needed for fetching a member
  // list by channel, and suspected to cause Discord's closeCode 4002
  // (decode error) when sent alongside another subscribe request
  // already sent for the same guild (see
  // DiscordGateway::sendMemberListSync()).
  static QByteArray buildMemberListSyncPayload(const QString &guildId,
                                               const QString &channelId,
                                               QString *errorMessage = 0);
  // op:14 payload WITHOUT "channels" — used to "unsubscribe" a channel
  // from the member list before resubscribing (see
  // DiscordGateway::sendMemberListSync()). Discord won't re-send
  // GUILD_MEMBER_LIST_UPDATE (SYNC) if the subscribe request exactly
  // matches an existing subscription (same guild_id + same channel
  // range) - the subscribe state needs to change first to force the
  // server to treat it as an actual change needing resync.
  static QByteArray buildMemberListUnsubscribePayload(
      const QString &guildId, QString *errorMessage = 0);
  static int valueToInt(const QVariant &value, int fallback);
  static bool hasJsonToken(const QByteArray &bytes, const char *compactToken,
                           const char *spacedToken);
  static bool isLargeReadyPayload(const QByteArray &bytes);
  static QString extractStringField(const QByteArray &bytes,
                                    const char *fieldName);
  static bool extractBoolField(const QByteArray &bytes, const char *fieldName,
                               bool fallback = false);
  static QByteArray extractObjectField(const QByteArray &bytes,
                                       const char *fieldName);
  static QVariantList extractArrayField(const QByteArray &bytes,
                                        const char *fieldName);
};

#endif /* JsonParser_HPP_ */
