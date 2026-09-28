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
  // Minimal op:14 payload for member-list sync only. Unlike buildGuildSubscribePayload()
  // it omits "guild_subscriptions", which is not needed here and is suspected of
  // causing close code 4002 next to another subscribe for the same guild.
  static QByteArray buildMemberListSyncPayload(const QString &guildId,
                                               const QString &channelId,
                                               QString *errorMessage = 0);
  // op:14 payload without "channels": "unsubscribes" before resubscribing, since Discord
  // does not resend SYNC for a subscribe identical to the existing one
  // (see DiscordGateway::sendMemberListSync()).
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
