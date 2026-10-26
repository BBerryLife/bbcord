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
// Base64 JSON "Super Properties" sent as the X-Super-Properties header by the real
// client. A browser User-Agent without it looks like automation to Discord; exact
// values matter less than it being present and consistent with the User-Agent.
// Returns the full header line ("X-Super-Properties: ...\r\n"), like desktopUserAgentHeader().
QByteArray superPropertiesHeader();
// Single source of truth for the client identity, so the REST headers
// (X-Super-Properties) and the gateway IDENTIFY properties never disagree. Update the
// build number here when it gets stale.
int clientBuildNumber();
QString clientBrowserName();
QString clientBrowserVersion();
QString clientLocale();
// Extra headers a normal client sends on every authenticated REST request
// (X-Super-Properties, X-Discord-Locale, Accept-Language), as complete header lines
// ending in "\r\n". Not for the CDN requests.
QByteArray apiClientHeaders();

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
