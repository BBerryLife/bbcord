#ifndef MarkdownParser_HPP_
#define MarkdownParser_HPP_

#include <QString>

class MarkdownParser {
public:
  // With `emojiMarkers`, custom-emoji tokens become private-use marker sequences
  // (see EmojiUtils::inlineTokens()) instead of the ":name:" text fallback, so the
  // caller can lay text and emoji images out in one line.
  static QString toHtml(const QString &markdown, bool emojiMarkers = false);

  // Discord-style highlighted mention/channel pill ("#general", "@name"): light
  // blurple background with light text. `label` is plain text (escaped here).
  static QString mentionHtml(const QString &label);

  // Text-only ":name:" fallback for a custom emoji (as the desktop client shows when the
  // image fails to load), since Label(TextFormat.Html) cannot display <img>. The real
  // image is a separate ImageView segment (see EmojiUtils.hpp).
  static QString escapeHtml(const QString &text);

private:
  static QString parseInline(const QString &text, bool emojiMarkers);
  static bool isSafeLink(const QString &url);
  static int markdownLinkEndIndex(const QString &text, int urlStart);
  static int linkEndIndex(const QString &text, int start);
  static bool isWordChar(const QChar &character);
  static int closingDelimiterIndex(const QString &text,
                                   const QString &delimiter, int start);
};

#endif /* MarkdownParser_HPP_ */
