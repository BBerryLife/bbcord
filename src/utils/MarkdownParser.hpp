#ifndef MarkdownParser_HPP_
#define MarkdownParser_HPP_

#include <QString>

class MarkdownParser {
public:
  static QString toHtml(const QString &markdown);

  // Text-only ":name:" fallback for a custom emoji (as the desktop client shows when the
  // image fails to load), since Label(TextFormat.Html) cannot display <img>. The real
  // image is a separate ImageView segment (see EmojiUtils.hpp).
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
