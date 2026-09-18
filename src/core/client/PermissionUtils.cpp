#include "PermissionUtils.hpp"

namespace {

qint64 parseBitfield(const QVariant &value) {
  // Discord sends allow/deny/permissions as stringified int64s (the
  // values exceed int32 range) - toLongLong() parses a numeric QString
  // directly, same convention used for role.permissions in Client.cpp.
  bool ok = false;
  qint64 parsed = value.toLongLong(&ok);
  return ok ? parsed : 0;
}

} // namespace

namespace PermissionUtils {

qint64 basePermissions(const QString &guildId, const QVariantList &guildRoles,
                       const QStringList &currentUserRoleIds) {
  qint64 permissions = 0;
  QString safeGuildId = guildId.trimmed();

  for (int i = 0; i < guildRoles.size(); ++i) {
    QVariantMap role = guildRoles.at(i).toMap();
    QString roleId = role.value("id").toString().trimmed();
    if (roleId.isEmpty()) {
      continue;
    }

    // The @everyone role's id is always the guild's own id - its
    // permissions apply to every member regardless of currentUserRoleIds.
    bool isEveryone = roleId == safeGuildId;
    if (isEveryone || currentUserRoleIds.contains(roleId)) {
      permissions |= parseBitfield(role.value("permissions"));
    }
  }

  return permissions;
}

qint64 applyChannelOverwrites(qint64 basePerms, const QString &guildId,
                              const QString &userId,
                              const QStringList &currentUserRoleIds,
                              const QVariantList &permissionOverwrites) {
  if (basePerms & kPermissionAdministrator) {
    return basePerms;
  }

  qint64 permissions = basePerms;
  QString safeGuildId = guildId.trimmed();
  QString safeUserId = userId.trimmed();

  // Step 1: @everyone overwrite (its overwrite entry id == guildId,
  // same convention as the @everyone role itself).
  for (int i = 0; i < permissionOverwrites.size(); ++i) {
    QVariantMap overwrite = permissionOverwrites.at(i).toMap();
    if (overwrite.value("type").toInt() == kOverwriteTypeRole &&
        overwrite.value("id").toString().trimmed() == safeGuildId) {
      permissions &= ~parseBitfield(overwrite.value("deny"));
      permissions |= parseBitfield(overwrite.value("allow"));
      break;
    }
  }

  // Step 2: role overwrites for the member's other roles - deny bits
  // across all matching overwrites are combined first, then allow
  // bits across all matching overwrites, per Discord's documented
  // permission overwrite resolution order (role overwrites are NOT
  // applied one role at a time; they're merged as a set before being
  // applied to the base).
  qint64 roleDeny = 0;
  qint64 roleAllow = 0;
  for (int i = 0; i < permissionOverwrites.size(); ++i) {
    QVariantMap overwrite = permissionOverwrites.at(i).toMap();
    if (overwrite.value("type").toInt() != kOverwriteTypeRole) {
      continue;
    }
    QString overwriteId = overwrite.value("id").toString().trimmed();
    if (overwriteId == safeGuildId || !currentUserRoleIds.contains(overwriteId)) {
      continue;
    }
    roleDeny |= parseBitfield(overwrite.value("deny"));
    roleAllow |= parseBitfield(overwrite.value("allow"));
  }
  permissions &= ~roleDeny;
  permissions |= roleAllow;

  // Step 3: member-specific overwrite, if any.
  for (int i = 0; i < permissionOverwrites.size(); ++i) {
    QVariantMap overwrite = permissionOverwrites.at(i).toMap();
    if (overwrite.value("type").toInt() == kOverwriteTypeMember &&
        overwrite.value("id").toString().trimmed() == safeUserId) {
      permissions &= ~parseBitfield(overwrite.value("deny"));
      permissions |= parseBitfield(overwrite.value("allow"));
      break;
    }
  }

  return permissions;
}

bool canViewChannel(const QString &guildId, const QString &userId,
                    const QVariantList &guildRoles,
                    const QStringList &currentUserRoleIds,
                    const QVariantList &permissionOverwrites) {
  qint64 base = basePermissions(guildId, guildRoles, currentUserRoleIds);
  qint64 effective = applyChannelOverwrites(
      base, guildId, userId, currentUserRoleIds, permissionOverwrites);
  return (effective & kPermissionAdministrator) ||
         (effective & kPermissionViewChannel);
}

} // namespace PermissionUtils
