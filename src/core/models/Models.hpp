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
  // Hex color "#RRGGBB"; empty for color == 0 (the client uses the default text color, not black).
  QString color;
  // Role position in the hierarchy (higher = higher up). Picks the color of a
  // member's name and groups members in the Members sheet.
  int position;
  // True if the role has its own group in the member list ("hoist"); other roles
  // fall into "Online"/"Offline".
  bool hoisted;
  // Raw permissions bitfield ("permissions" is a stringified int64; toLongLong()
  // parses it). Channel visibility is computed client-side from role permissions and
  // permission_overwrites (see PermissionUtils::canViewChannel()).
  qint64 permissions;

  DiscordRole() : position(0), hoisted(false), permissions(0) {}
};

// A flattened Members-sheet row merged from the member and user objects of
// GUILD_MEMBER_LIST_UPDATE; keeps only the fields the UI needs.
struct DiscordMember {
  QString userId;
  QString displayName; // nick if set, falls back to username/global_name
  QString avatarUrl;   // empty when using the default avatar (initials)
  QString status;      // "online" | "idle" | "dnd" | "offline"
  // roleId of the member's highest-position hoisted role (heading and name color).
  // Empty if none ("Online"/"Offline").
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
  // Parent channel ID: empty for top-level channels, or the channel containing this
  // thread. Also present as "parentId" from ItemMapper::guildChannelToItem().
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
  // Raw "content" has unresolved mention syntax ("<@id>"). The raw mentions/
  // mention_roles/mention_channels arrays are kept so toVariantMap() can call
  // resolveMentions() before building messageHtml.
  QVariantList mentions;
  QVariantList mentionRoles;
  QVariantList mentionChannels;
  // mention_everyone: true for @everyone or @here (Discord does not distinguish
  // them). Used with mentions/mentionRoles to detect pings for the yellow highlight.
  bool mentionEveryone;
  QString nonce;
  QString timestamp;
  QString editedTimestamp;
  QString replyMessageId;
  QString replyAuthor;
  QString replyContent;
  // Same as above, for the referenced (replied-to) message, which has its own
  // independent mentions/mention_roles/mention_channels arrays.
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
  // Replaces "<@id>"/"<@!id>" (user), "<@&id>" (role) and "<#id>" (channel) tokens in
  // rawContent with "@name"/"#name" using the given arrays. Tokens whose id is not
  // found are left as-is rather than guessed.
  static QString resolveMentions(const QString &rawContent,
                                 const QVariantList &mentions,
                                 const QVariantList &mentionRoles,
                                 const QVariantList &mentionChannels);
};

#endif /* Models_HPP_ */
