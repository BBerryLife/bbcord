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
  // Màu hex "#RRGGBB", rỗng nếu role không có màu riêng (color == 0 trong
  // payload Discord — client thật fallback về màu chữ mặc định khi đó,
  // không phải màu đen).
  QString color;
  // Vị trí role trong hierarchy — số càng lớn càng "cao". Dùng để: (1)
  // chọn role có màu cao nhất cho tên hiển thị của member (2) sắp xếp
  // nhóm member theo role trong sheet Members, giống Discord client thật.
  int position;
  // true nếu role được Discord tách nhóm riêng trong danh sách member
  // ("hoist" trong payload gốc) — chỉ những role này mới tạo thành 1
  // heading riêng trong sheet Members; role không hoist gộp chung vào
  // nhóm "Online"/"Offline".
  bool hoisted;

  DiscordRole() : position(0), hoisted(false) {}
};

// 1 dòng "member" đã được làm phẳng trong sheet Members, ghép từ guild
// member object + user object trong payload GUILD_MEMBER_LIST_UPDATE.
// Không map 1-1 với payload gốc — chỉ giữ field thực sự cần cho UI, để
// tránh vác nguyên object lồng nhau (presence/activities/...) qua QML.
struct DiscordMember {
  QString userId;
  QString displayName; // nick nếu có, fallback về username/global_name
  QString avatarUrl;   // rỗng nếu dùng avatar mặc định (chữ cái viết tắt)
  QString status;      // "online" | "idle" | "dnd" | "offline"
  // roleId của role hoisted có position cao nhất mà member sở hữu — dùng
  // để nhóm member vào đúng heading và tô màu tên theo role đó. Rỗng nếu
  // member không có role hoisted nào (rơi vào nhóm "Online"/"Offline").
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
    GuildForum = 15,
    GuildMedia = 16,
    Unknown = -1
  };

  QString id;
  QString guildId;
  QString name;
  ChannelType type;
  int position;

  DiscordChannel() : type(Unknown), position(0) {}
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
