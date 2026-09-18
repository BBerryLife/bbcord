#ifndef PERMISSIONUTILS_HPP_
#define PERMISSIONUTILS_HPP_

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// Client-side computation of Discord's channel permission overwrites.
//
// Why this exists: GET /guilds/{id}/channels returns EVERY channel in
// the guild, regardless of whether the requesting user can see it -
// unlike the official desktop/web client (which gets a pre-filtered
// READY payload via the privileged gateway intents a real client
// session has). A user-token client has to replicate Discord's own
// visibility rule locally: a channel is visible only if the computed
// permission bitfield for the current user has the VIEW_CHANNEL bit
// set, after applying overwrites in Discord's documented order:
//   1. base = OR of permissions of @everyone + all of the member's
//      roles (the guild's base role, @everyone, has id == guildId)
//   2. apply the @everyone overwrite on the channel, if any (deny
//      then allow)
//   3. apply the union of the member's role overwrites on the channel
//      (deny across all first, then allow across all)
//   4. apply the member-specific overwrite on the channel, if any
//      (deny then allow)
// ADMINISTRATOR (bit 0x8) short-circuits to "can see everything", same
// as real Discord.
namespace PermissionUtils {

const qint64 kPermissionAdministrator = 0x8;
const qint64 kPermissionViewChannel = 0x400;

// permission_overwrites entry "type": 0 == role, 1 == member (matches
// the raw Discord payload's numeric type field).
const int kOverwriteTypeRole = 0;
const int kOverwriteTypeMember = 1;

// Computes the current user's base guild permissions: OR of @everyone
// (roleId == guildId) plus every role in currentUserRoleIds, looked up
// by id in guildRoles (each entry as produced by Client.cpp's
// roleMap - needs "id" and "permissions").
qint64 basePermissions(const QString &guildId, const QVariantList &guildRoles,
                       const QStringList &currentUserRoleIds);

// Applies a channel's permission_overwrites (raw Discord array, each
// entry with "id"/"type"/"allow"/"deny" as strings) on top of
// basePerms, for a user with the given userId and roles.
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
