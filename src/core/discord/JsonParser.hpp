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
  // Payload op:14 tối giản, CHỈ dành cho member-list sync (sheet
  // Members). Khác buildGuildSubscribePayload() ở chỗ KHÔNG gửi field
  // "guild_subscriptions" — field này không cần thiết cho việc lấy
  // member list theo channel, và bị nghi là nguyên nhân Discord trả
  // closeCode 4002 (decode error) khi gọi song song với 1 subscribe
  // request khác đã gửi trước đó cho cùng guild (xem
  // DiscordGateway::sendMemberListSync()).
  static QByteArray buildMemberListSyncPayload(const QString &guildId,
                                               const QString &channelId,
                                               QString *errorMessage = 0);
  // Payload op:14 KHÔNG kèm "channels" — dùng để "unsubscribe" channel
  // khỏi member list trước khi subscribe lại (xem
  // DiscordGateway::sendMemberListSync()). Discord không phát lại
  // GUILD_MEMBER_LIST_UPDATE (SYNC) nếu request subscribe trùng hệt
  // subscription đã có (cùng guild_id + cùng range channel) - cần đổi
  // trạng thái subscribe trước để buộc server coi đây là thay đổi thực
  // sự cần đồng bộ lại.
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
