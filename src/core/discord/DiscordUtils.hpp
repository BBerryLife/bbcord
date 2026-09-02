#ifndef DiscordUtils_HPP_
#define DiscordUtils_HPP_

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace DiscordUtils {

QByteArray desktopUserAgent();
QByteArray desktopUserAgentHeader();
// Base64-encoded JSON "Super Properties" object describing this client
// (os/browser/build number/etc.), sent as the X-Super-Properties header on
// every request the desktop/web client makes - including auth. Discord's
// anti-abuse systems treat a User-Agent claiming to be a browser but never
// sending this header as a strong bot/automation signal; the exact field
// values matter far less than the header simply being present and
// internally consistent with the User-Agent string. Returns the full
// header line ("X-Super-Properties: ...\r\n") ready to splice into a
// request, same convention as desktopUserAgentHeader().
QByteArray superPropertiesHeader();

QString firstLetter(const QString &text);
QString firstTwoWordLetters(const QString &text);
QString cleanSnowflake(const QVariant &value);
void appendUniqueGuildId(QStringList *guildIds, const QString &guildId);
void appendGuildIdsFromUserSettingsProto(QStringList *orderedGuildIds,
                                         const QString &base64Proto);
QVariantList guildFoldersFromUserSettingsProto(const QString &base64Proto);
bool positionShouldMoveBefore(const QVariantMap &left,
                              const QVariantMap &right);
bool dmShouldMoveBefore(const QVariantMap &left, const QVariantMap &right);
void stableSortItems(QVariantList *items,
                     bool (*shouldMoveBefore)(const QVariantMap &left,
                                              const QVariantMap &right));

} // namespace DiscordUtils

#endif /* DiscordUtils_HPP_ */
