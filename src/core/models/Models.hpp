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

  DiscordRole() : position(0), hoisted(false) {}
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
  QString nonce;
  QString timestamp;
  QString editedTimestamp;
  QString replyMessageId;
  QString replyAuthor;
  QString replyContent;
  QList<DiscordAttachment> attachments;
  bool pending;
  bool failed;
  bool isGroupStart;
  bool isGroupEnd;
  bool showAvatar;
  bool showUsername;
  bool showTimestamp;

  DiscordMessage()
      : pending(false), failed(false), isGroupStart(true), isGroupEnd(true),
        showAvatar(true), showUsername(true), showTimestamp(true) {}

  bool isEdited() const;
  qint64 timestampMs() const;
  QString displayTime() const;
  QString authorInitials() const;
  QVariantMap toVariantMap() const;
  static DiscordMessage fromVariantMap(const QVariantMap &data);
};

#endif /* Models_HPP_ */
