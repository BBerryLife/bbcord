#ifndef EmojiUtils_HPP_
#define EmojiUtils_HPP_

#include <QList>
#include <QString>
#include <QVariantList>

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

  // Marker for one custom emoji inside HTML from MarkdownParser::toHtml(..., true):
  // U+E002, 'a' or 's' (animated/static), ':', the id, U+E003. The id is enough to find
  // the cached image in "emojiSegments".
  static QString marker(const QString &id, bool animated);

  // Splits that HTML into layout tokens so QML can place text and emoji images in one
  // line (Label cannot draw inline <img>, and Cascades has no wrapping flow layout).
  // Each token is a QVariantMap:
  //   {t:"w", html, w}  word; html is self-contained (open tags are re-opened and
  //                     closed at the token edges), w is its estimated width in em
  //   {t:"s", n, w}     run of n spaces
  //   {t:"e", id, animated}  custom emoji
  //   {t:"n"}           line break
  static QVariantList inlineTokens(const QString &html);

  // cdn.discordapp.com URL for a custom emoji: ".png" for static, ".gif" for animated.
  static QString cdnUrl(const QString &id, bool animated);
};

#endif /* EmojiUtils_HPP_ */
