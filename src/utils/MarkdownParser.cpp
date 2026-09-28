#include "MarkdownParser.hpp"

#include "EmojiUtils.hpp"

namespace {
struct MarkdownDelimiter {
  const char *delimiter;
  const char *openTag;
  const char *closeTag;
};

const MarkdownDelimiter kDelimiters[] = {
    {"**", "<b>", "</b>"},
    {"__", "<u>", "</u>"},
    {"~~", "<s>", "</s>"},
    {"||", "<span style=\"background-color: #202225; color: #202225;\">",
     "</span>"},
    {"*", "<i>", "</i>"},
    {"_", "<i>", "</i>"}};

const int kDelimiterCount = sizeof(kDelimiters) / sizeof(kDelimiters[0]);

QString highlightedLink(const QString &html) {
  return "<font color=\"#00A8FC\"><u>" + html + "</u></font>";
}
} // namespace

QString MarkdownParser::toHtml(const QString &markdown) {
  QString html;
  int index = 0;

  while (index < markdown.length()) {
    if (markdown.mid(index, 3) == "```") {
      int blockEnd = markdown.indexOf("```", index + 3);
      if (blockEnd >= 0) {
        QString code = markdown.mid(index + 3, blockEnd - index - 3);
        if (code.startsWith('\n')) {
          code.remove(0, 1);
        }
        html += "<span style=\"font-family: monospace; color: #F2F3F5;\">";
        html += escapeHtml(code);
        html += "</span>";
        index = blockEnd + 3;
        continue;
      }

      html += escapeHtml(markdown.mid(index, 3));
      index += 3;
      continue;
    }

    int nextBlock = markdown.indexOf("```", index);
    if (nextBlock < 0) {
      html += parseInline(markdown.mid(index));
      break;
    }

    html += parseInline(markdown.mid(index, nextBlock - index));
    index = nextBlock;
  }

  return html;
}

QString MarkdownParser::escapeHtml(const QString &text) {
  QString escaped;
  escaped.reserve(text.length());

  for (int i = 0; i < text.length(); ++i) {
    const QChar character = text.at(i);
    if (character == '&') {
      escaped += "&amp;";
    } else if (character == '<') {
      escaped += "&lt;";
    } else if (character == '>') {
      escaped += "&gt;";
    } else if (character == '"') {
      escaped += "&quot;";
    } else if (character == '\n') {
      escaped += "<br/>";
    } else {
      escaped += character;
    }
  }

  return escaped;
}

QString MarkdownParser::parseInline(const QString &text) {
  QString html;
  int index = 0;

  while (index < text.length()) {
    // Raw custom-emoji tokens would otherwise be escaped as literal text; emit only the
    // ":name:" fallback (the real image is an ImageView segment, see EmojiUtils.hpp).
    // Checked at every scan position, matching parseInline()'s one-token-at-a-time loop.
    if (text.at(index) == '<') {
      // Max token length: "<a:" + 32-char name + ":" + 19-digit snowflake + ">" = 56 chars.
      // A smaller window would silently miss long emoji names (QRegExp only sees this slice).
      QList<EmojiUtils::EmojiToken> tokens =
          EmojiUtils::findTokens(text.mid(index, 56));
      if (!tokens.isEmpty() && tokens.first().start == 0) {
        const EmojiUtils::EmojiToken &token = tokens.first();
        html += "<span style=\"color: #949BA4;\">:";
        html += escapeHtml(token.name);
        html += ":</span>";
        index += token.length;
        continue;
      }
    }

    if (text.at(index) == '`') {
      int codeEnd = text.indexOf('`', index + 1);
      if (codeEnd >= 0) {
        html += "<span style=\"font-family: monospace; color: #F2F3F5;\">";
        html += escapeHtml(text.mid(index + 1, codeEnd - index - 1));
        html += "</span>";
        index = codeEnd + 1;
        continue;
      }
    }

    if (text.at(index) == '[') {
      int labelEnd = text.indexOf("](", index + 1);
      if (labelEnd >= 0) {
        int urlStart = labelEnd + 2;
        int urlEnd = markdownLinkEndIndex(text, urlStart);
        QString url =
            urlEnd >= 0 ? text.mid(urlStart, urlEnd - urlStart) : QString();
        if (url.length() >= 2 && url.startsWith('<') && url.endsWith('>')) {
          url = url.mid(1, url.length() - 2);
        }
        if (urlEnd >= 0 && isSafeLink(url)) {
          html += "<a href=\"";
          html += escapeHtml(url);
          html += "\">";
          html += highlightedLink(
              parseInline(text.mid(index + 1, labelEnd - index - 1)));
          html += "</a>";
          index = urlEnd + 1;
          continue;
        }
      }
    }

    if (text.mid(index, 7).toLower() == "http://" ||
        text.mid(index, 8).toLower() == "https://") {
      int urlEnd = linkEndIndex(text, index);
      QString url = text.mid(index, urlEnd - index);
      if (isSafeLink(url)) {
        QString escapedUrl = escapeHtml(url);
        html += "<a href=\"";
        html += escapedUrl;
        html += "\">";
        html += highlightedLink(escapedUrl);
        html += "</a>";
        index = urlEnd;
        continue;
      }
    }

    bool matched = false;
    for (int i = 0; i < kDelimiterCount; ++i) {
      const QString delimiter = QString::fromLatin1(kDelimiters[i].delimiter);
      if (text.mid(index, delimiter.length()) != delimiter) {
        continue;
      }

      if (delimiter.contains('_')) {
        QChar before = index > 0 ? text.at(index - 1) : QChar();
        QChar afterOpen = index + delimiter.length() < text.length()
                              ? text.at(index + delimiter.length())
                              : QChar();
        if (!before.isNull() && isWordChar(before)) {
          continue;
        }
        if (delimiter == "_" && !afterOpen.isNull() && isWordChar(afterOpen) &&
            !before.isNull() && isWordChar(before)) {
          continue;
        }
      }

      int end =
          closingDelimiterIndex(text, delimiter, index + delimiter.length());
      if (end >= 0) {
        html += QString::fromLatin1(kDelimiters[i].openTag);
        html += parseInline(text.mid(index + delimiter.length(),
                                     end - index - delimiter.length()));
        html += QString::fromLatin1(kDelimiters[i].closeTag);
        index = end + delimiter.length();
        matched = true;
        break;
      }
    }

    if (!matched) {
      html += escapeHtml(text.mid(index, 1));
      ++index;
    }
  }

  return html;
}

bool MarkdownParser::isSafeLink(const QString &url) {
  QString lowerUrl = url.toLower();
  return lowerUrl.startsWith("http://") || lowerUrl.startsWith("https://");
}

int MarkdownParser::markdownLinkEndIndex(const QString &text, int urlStart) {
  int index = urlStart;
  bool angledUrl = urlStart < text.length() && text.at(urlStart) == '<';

  while (index < text.length()) {
    QChar character = text.at(index);
    if (angledUrl) {
      if (character == '>') {
        return index + 1 < text.length() && text.at(index + 1) == ')'
                   ? index + 1
                   : -1;
      }
      if (character == '"') {
        return -1;
      }
      ++index;
      continue;
    }

    if (character == ')') {
      return index;
    }
    if (character.isSpace() || character == '<' || character == '>' ||
        character == '"') {
      return -1;
    }
    ++index;
  }

  return -1;
}

int MarkdownParser::linkEndIndex(const QString &text, int start) {
  int end = start;

  while (end < text.length()) {
    QChar character = text.at(end);
    if (character.isSpace() || character == '<' || character == '>' ||
        character == '"') {
      break;
    }
    ++end;
  }

  while (end > start) {
    QChar character = text.at(end - 1);
    if (character == '.' || character == ',' || character == '!' ||
        character == '?' || character == ':' || character == ';') {
      --end;
      continue;
    }
    break;
  }

  return end;
}

bool MarkdownParser::isWordChar(const QChar &character) {
  return (character >= '0' && character <= '9') ||
         (character >= 'A' && character <= 'Z') ||
         (character >= 'a' && character <= 'z');
}

int MarkdownParser::closingDelimiterIndex(const QString &text,
                                          const QString &delimiter, int start) {
  int index = text.indexOf(delimiter, start);

  while (index >= 0) {
    if (delimiter.contains('_')) {
      QChar after = index + delimiter.length() < text.length()
                        ? text.at(index + delimiter.length())
                        : QChar();
      if (!after.isNull() && isWordChar(after)) {
        index = text.indexOf(delimiter, index + delimiter.length());
        continue;
      }
    }

    return index;
  }

  return -1;
}
