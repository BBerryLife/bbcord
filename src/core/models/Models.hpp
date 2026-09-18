#ifndef Models_HPP_
#define Models_HPP_

#include <QList>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

struct DiscordAttachment {
  QString id;
  QString filename;
  QString url;
  QString proxyUrl;
  QString contentType;
  int size;
  int width;
  int height;

  DiscordAttachment() : size(0), width(0), height(0) {}

  bool isImage() const;
  QVariantMap toVariantMap() const;
  static DiscordAttachment fromVariantMap(const QVariantMap &data);
};

struct DiscordUser {
  QString id;
  QString username;
  QString discriminator;
  QString globalName;
  QString avatarHash;
  bool bot;

  DiscordUser() : bot(false) {}

  QString displayName() const;
};

struct DiscordGuild {
  QString id;
  QString name;
  QString iconHash;
  bool unavailable;

  DiscordGuild() : unavailable(false) {}
};

struct DiscordRole {
  QString id;
  QString guildId;
  QString name;
  // Hex color "#RRGGBB", empty if the role has no custom color (color
  // == 0 in the Discord payload — the real client falls back to the
  // default text color then, not black).
  QString color;
  // Role position in the hierarchy — higher number means "higher up".
  // Used to: (1) pick the highest-position role's color for a member's
  // display name (2) group members by role in the Members sheet, same
  // as the real Discord client.
  int position;
  // true if Discord separates this role into its own group in the
  // member list ("hoist" in the raw payload) — only hoisted roles get
  // their own heading in the Members sheet; non-hoisted roles fall
  // into the "Online"/"Offline" group.
  bool hoisted;
  // Raw permissions bitfield for this role ("permissions" in the
  // payload, a stringified int64 — Qt's QVariant::toLongLong() parses
  // it fine straight from the JSON string). Needed to compute whether
  // a channel is actually visible to the current user: Discord's REST
  // "guild channels" endpoint returns every channel in the guild
  // regardless of permissions, so channel visibility has to be derived
  // client-side from base role permissions + each channel's
  // permission_overwrites (see PermissionUtils::canViewChannel()).
  qint64 permissions;

  DiscordRole() : position(0), hoisted(false), permissions(0) {}
};

// A single flattened "member" row in the Members sheet, merged from the
// guild member object + user object in the GUILD_MEMBER_LIST_UPDATE
// payload. Not a 1:1 map of the raw payload — only keeps the fields the
// UI actually needs, to avoid dragging nested objects (presence/
// activities/...) into QML.
struct DiscordMember {
  QString userId;
  QString displayName; // nick if set, falls back to username/global_name
  QString avatarUrl;   // empty when using the default avatar (initials)
  QString status;      // "online" | "idle" | "dnd" | "offline"
  // roleId of the highest-position hoisted role the member has — used
  // to group the member under the right heading and color their name.
  // Empty if the member has no hoisted role (falls into "Online"/
  // "Offline").
  QString primaryRoleId;


  DiscordMember() {}
};

struct DiscordChannel {
  enum ChannelType {
    GuildText = 0,
    Dm = 1,
    GuildVoice = 2,
    GroupDm = 3,
    GuildCategory = 4,
    GuildAnnouncement = 5,
    AnnouncementThread = 10,
    PublicThread = 11,
    PrivateThread = 12,
    GuildForum = 15,
    GuildMedia = 16,
    Unknown = -1
  };

  QString id;
  QString guildId;
  QString name;
  ChannelType type;
  int position;
  // Parent channel ID — empty for top-level channels, or pointing to
  // the GuildText/GuildForum/GuildAnnouncement channel containing this
  // one if it's a thread (AnnouncementThread/PublicThread/PrivateThread).
  // Already present at the QVariantMap level via
  // ItemMapper::guildChannelToItem() ("parentId") — added here too so
  // the struct is accurate, used when grouping threads under their
  // parent channel or checking "is this channel a thread".
  QString parentId;

  DiscordChannel() : type(Unknown), position(0) {}

  bool isThread() const {
    return type == AnnouncementThread || type == PublicThread ||
           type == PrivateThread;
  }
};

struct DiscordMessage {
  QString id;
  QString channelId;
  QString guildId;
  DiscordUser author;
  QString content;
  // Fix: Discord's raw "content" contains unresolved mention syntax
  // like "<@921017648869957654>" - the real client renders these as
  // "@username" using the message's own "mentions"/"mention_roles"/
  // "mention_channels" arrays (each entry already carries the
  // mentioned user/role/channel's display name, no extra lookup
  // needed). These raw arrays are kept on the message (rather than
  // resolving immediately in fromVariantMap()) so toVariantMap() can
  // run resolveMentions() right before building messageHtml - see
  // resolveMentions() below and its call site in toVariantMap().
  QVariantList mentions;
  QVariantList mentionRoles;
  QVariantList mentionChannels;
  // Fix: "mention_everyone" is true when the message used @everyone or
  // @here - needed (together with mentions/mentionRoles) to compute
  // whether THIS message pings the current user, for the yellow
  // highlight in MessageBubble.qml. Discord doesn't distinguish
  // @everyone from @here in this boolean field (both set it to true) -
  // the real client highlights for both the same way, so no further
  // distinction is needed here either.
  bool mentionEveryone;
  QString nonce;
  QString timestamp;
  QString editedTimestamp;
  QString replyMessageId;
  QString replyAuthor;
  QString replyContent;
  // Fix: same purpose as mentions/mentionRoles/mentionChannels above,
  // but for the REFERENCED message's own content (the small quoted
  // preview shown above a reply) - Discord nests a full message object
  // under "referenced_message", with its own independent "mentions"/
  // "mention_roles"/"mention_channels" arrays, not shared with the
  // reply itself.
  QVariantList replyMentions;
  QVariantList replyMentionRoles;
  QVariantList replyMentionChannels;
  QList<DiscordAttachment> attachments;
  bool pending;
  bool failed;
  bool isGroupStart;
  bool isGroupEnd;
  bool showAvatar;
  bool showUsername;
  bool showTimestamp;

  DiscordMessage()
      : mentionEveryone(false), pending(false), failed(false),
        isGroupStart(true), isGroupEnd(true), showAvatar(true),
        showUsername(true), showTimestamp(true) {}

  bool isEdited() const;
  qint64 timestampMs() const;
  QString displayTime() const;
  QString authorInitials() const;
  QVariantMap toVariantMap() const;
  static DiscordMessage fromVariantMap(const QVariantMap &data);
  // Replaces every "<@id>"/"<@!id>" (user), "<@&id>" (role), and
  // "<#id>" (channel) token in rawContent with a "@name"/"#name" form,
  // using the display names supplied in mentions/mentionRoles/
  // mentionChannels (each a raw Discord array of {id, ...} objects, as
  // found on the message payload's own "mentions"/"mention_roles"/
  // "mention_channels" fields). A token whose id isn't found in the
  // matching array is left as-is (rather than silently dropped) - this
  // shouldn't normally happen since Discord always includes an entry
  // for every mention actually present in the content, but a channel
  // the current user can't see would still appear as a resolvable
  // channel in "mention_channels" without a name in rare edge cases,
  // and leaving the raw token is safer than guessing.
  static QString resolveMentions(const QString &rawContent,
                                 const QVariantList &mentions,
                                 const QVariantList &mentionRoles,
                                 const QVariantList &mentionChannels);
};

#endif /* Models_HPP_ */
