#ifndef PERMISSIONUTILS_HPP_
#define PERMISSIONUTILS_HPP_

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// Client-side channel permission computation. GET /guilds/{id}/channels returns every
// channel regardless of visibility, so a user-token client must apply Discord's rule
// itself: a channel is visible if the computed permissions have VIEW_CHANNEL, after:
//   1. base = OR of @everyone (role id == guildId) and all the member's roles
//   2. the channel's @everyone overwrite (deny, then allow)
//   3. the union of the member's role overwrites (all deny, then all allow)
//   4. the member-specific overwrite (deny, then allow)
// ADMINISTRATOR (0x8) short-circuits to "can see everything".
namespace PermissionUtils {

const qint64 kPermissionAdministrator = 0x8;
const qint64 kPermissionViewChannel = 0x400;

// permission_overwrites entry "type": 0 == role, 1 == member (matches
// the raw Discord payload's numeric type field).
const int kOverwriteTypeRole = 0;
const int kOverwriteTypeMember = 1;

// Computes the user's base guild permissions: OR of @everyone (roleId == guildId)
// and every role in currentUserRoleIds, looked up in guildRoles (needs "id" and "permissions").
qint64 basePermissions(const QString &guildId, const QVariantList &guildRoles,
                       const QStringList &currentUserRoleIds);

// Applies a channel's permission_overwrites (raw array of "id"/"type"/"allow"/"deny"
// strings) on top of basePerms for the given userId and roles.
qint64 applyChannelOverwrites(qint64 basePerms, const QString &guildId,
                              const QString &userId,
                              const QStringList &currentUserRoleIds,
                              const QVariantList &permissionOverwrites);

// Convenience wrapper: true if VIEW_CHANNEL ends up set (or the user
// has ADMINISTRATOR) after applying all overwrites.
bool canViewChannel(const QString &guildId, const QString &userId,
                    const QVariantList &guildRoles,
                    const QStringList &currentUserRoleIds,
                    const QVariantList &permissionOverwrites);

} // namespace PermissionUtils

#endif /* PERMISSIONUTILS_HPP_ */
