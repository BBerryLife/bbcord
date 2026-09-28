#ifndef EmojiUtils_HPP_
#define EmojiUtils_HPP_

#include <QList>
#include <QString>

// Custom emoji arrive in message content as <:name:id> (static) and <a:name:id>
// (animated). Label(TextFormat.Html) has no <img>, so they are extracted as
// segments and drawn as separate ImageViews (see
// ChatController::prepareMessageForModel() and MessageBubble.qml).
// CDN: static emoji are served as ".png" (no WebP decoding needed); animated ones as
// ".gif" (readable by Cascades). Only Discord's sticker CDN (WebP) would need the
// vendored libwebpdecoder in third_party/webp/.
class EmojiUtils {
public:
  struct EmojiToken {
    QString name;
    QString id;
    bool animated;
    int start;   // index into the original string
    int length;  // length of the raw "<:name:id>" / "<a:name:id>" token
  };

  // Scans `content` left to right and returns every custom-emoji token in order, with
  // byte offsets into `content` (safe for splitting into text/emoji segments).
  static QList<EmojiToken> findTokens(const QString &content);

  // True if `content` is only emoji tokens (and whitespace); Discord renders these
  // "jumbo".
  static bool isEmojiOnly(const QString &content);

  // cdn.discordapp.com URL for a custom emoji: ".png" for static, ".gif" for animated.
  static QString cdnUrl(const QString &id, bool animated);
};

#endif /* EmojiUtils_HPP_ */
