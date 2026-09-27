#ifndef EmojiUtils_HPP_
#define EmojiUtils_HPP_

#include <QList>
#include <QString>

// Discord custom emoji come through message content as raw tokens:
//   <:name:id>    - static
//   <a:name:id>   - animated
// Cascades' Label (TextFormat.Html) has no <img> support, so these can't
// be inlined into the HTML MarkdownParser produces - they have to be
// pulled out as separate segments and rendered as their own ImageView
// next to the surrounding text (see ChatController::prepareMessageForModel()
// and MessageBubble.qml's emoji segment repeater).
//
// CDN note: for STATIC custom emoji, cdn.discordapp.com happily serves
// a real PNG at the ".png" path regardless of what the client actually
// asked for - unlike Zalo, where the server unilaterally picks
// PNG/GIF/WebP with no way to request a specific one. So the common
// custom-emoji case here needs no WebP decoding at all; only animated
// emoji (".gif", already readable by Cascades - see Zalo10's
// saveStickerData() notes) and, later, Discord's sticker CDN (which does
// use WebP) need the vendored libwebpdecoder in third_party/webp/.
class EmojiUtils {
public:
  struct EmojiToken {
    QString name;
    QString id;
    bool animated;
    int start;   // index into the original string
    int length;  // length of the raw "<:name:id>" / "<a:name:id>" token
  };

  // Scans `content` left to right and returns every custom-emoji token
  // found, in order, with byte offsets into `content` unchanged (safe to
  // use for splitting the original string into text/emoji segments).
  static QList<EmojiToken> findTokens(const QString &content);

  // True if `content` is *only* emoji tokens (and surrounding whitespace),
  // no other text - Discord renders these "jumbo" (larger, no bubble
  // text line). Mirrors Discord's own "all emoji" message treatment.
  static bool isEmojiOnly(const QString &content);

  // cdn.discordapp.com URL for a custom emoji. Always requests ".png" for
  // static emoji (see class comment - no WebP round-trip needed for the
  // common case) and ".gif" for animated ones (Cascades reads GIF fine
  // natively, animated or not).
  static QString cdnUrl(const QString &id, bool animated);
};

#endif /* EmojiUtils_HPP_ */
