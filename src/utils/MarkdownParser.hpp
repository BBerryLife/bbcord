#ifndef MarkdownParser_HPP_
#define MarkdownParser_HPP_

#include <QString>

class MarkdownParser {
public:
  static QString toHtml(const QString &markdown);

  // Text-only fallback rendering for a custom-emoji token: Discord's own
  // desktop client shows ":name:" (in a muted color) if the emoji image
  // itself fails to load - same fallback used here, since Cascades'
  // Label(TextFormat.Html) can't display an <img> at all. The real emoji
  // image, when it loads, is drawn as a separate ImageView segment laid
  // over/instead of this span - see EmojiUtils.hpp and
  // ChatController::prepareMessageForModel()'s "emojiSegments" field.
  static QString escapeHtml(const QString &text);

private:
  static QString parseInline(const QString &text);
  static bool isSafeLink(const QString &url);
  static int markdownLinkEndIndex(const QString &text, int urlStart);
  static int linkEndIndex(const QString &text, int start);
  static bool isWordChar(const QChar &character);
  static int closingDelimiterIndex(const QString &text,
                                   const QString &delimiter, int start);
};

#endif /* MarkdownParser_HPP_ */
