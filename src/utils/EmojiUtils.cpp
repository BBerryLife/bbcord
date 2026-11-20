#include "EmojiUtils.hpp"

#include <QRegExp>
#include <QStringList>
#include <QVariantMap>

namespace {
const ushort kMarkerStart = 0xE002;
const ushort kMarkerEnd = 0xE003;

// Rough glyph width in em for a proportional sans font. Only used to decide where to
// wrap, so it errs on the wide side: wrapping early is harmless, overflowing is not.
double charWidthEm(const QChar &c) {
  ushort u = c.unicode();
  if (c.isHighSurrogate()) {
    return 1.3; // a Unicode emoji / astral symbol; the low surrogate adds nothing
  }
  if (c.isLowSurrogate()) {
    return 0.0;
  }
  if (u == ' ') {
    return 0.30;
  }
  if (u >= 0x2E80) {
    return 1.05; // CJK and other full-width scripts
  }
  if (u > 0x7F) {
    return 0.60; // accented Latin (Vietnamese) and the rest
  }
  if (QString::fromLatin1("iljI.,;:!'|`").contains(c)) {
    return 0.32;
  }
  if (QString::fromLatin1("mwMW@").contains(c)) {
    return 0.90;
  }
  if (c.isUpper()) {
    return 0.68;
  }
  if (c.isDigit()) {
    return 0.58;
  }
  return 0.55;
}

QString tagName(const QString &tag) {
  int i = 1;
  if (i < tag.length() && tag.at(i) == '/') {
    ++i;
  }
  int start = i;
  while (i < tag.length() && tag.at(i).isLetterOrNumber()) {
    ++i;
  }
  return tag.mid(start, i - start).toLower();
}

struct TokenBuilder {
  QVariantList tokens;
  QStringList openTags;   // opening tags currently in effect, outermost first
  QStringList openNames;
  QStringList startTags;  // snapshot of openTags when the current word began
  QString body;
  double width;
  int spaces;

  TokenBuilder() : width(0.0), spaces(0) {}

  void flushWord() {
    if (body.isEmpty()) {
      return;
    }
    QString html = startTags.join(QString());
    html += body;
    for (int i = openNames.size() - 1; i >= 0; --i) {
      html += "</" + openNames.at(i) + ">";
    }
    QVariantMap token;
    token["t"] = "w";
    token["html"] = html;
    token["w"] = width;
    tokens.append(token);
    body.clear();
    width = 0.0;
  }

  void flushSpaces() {
    if (spaces <= 0) {
      return;
    }
    QVariantMap token;
    token["t"] = "s";
    token["n"] = spaces;
    token["w"] = spaces * 0.30;
    tokens.append(token);
    spaces = 0;
  }

  void beginWordIfNeeded() {
    if (body.isEmpty()) {
      startTags = openTags;
    }
  }
};
} // namespace

QList<EmojiUtils::EmojiToken>
EmojiUtils::findTokens(const QString &content) {
  QList<EmojiToken> tokens;

  // <a:name:id> or <:name:id>: name is letters/digits/underscore, id is the snowflake.
  // QRegExp is used (not QRegularExpression) because of the Qt4/Cascades 10 toolchain.
  QRegExp pattern("<(a?):(\\w+):(\\d+)>");
  int searchIndex = 0;

  while ((searchIndex = pattern.indexIn(content, searchIndex)) != -1) {
    EmojiToken token;
    token.animated = pattern.cap(1) == "a";
    token.name = pattern.cap(2);
    token.id = pattern.cap(3);
    token.start = searchIndex;
    token.length = pattern.matchedLength();
    tokens.append(token);

    searchIndex += token.length;
  }

  return tokens;
}

bool EmojiUtils::isEmojiOnly(const QString &content) {
  QList<EmojiToken> tokens = findTokens(content);
  if (tokens.isEmpty()) {
    return false;
  }

  // Discord caps "jumbo" rendering at a handful of emoji - mirror that
  // rather than jumbo-ing an arbitrarily long run of them.
  if (tokens.size() > 27) {
    return false;
  }

  QString remainder = content;
  // Strip tokens back-to-front so earlier offsets stay valid as we go.
  for (int i = tokens.size() - 1; i >= 0; --i) {
    remainder.remove(tokens.at(i).start, tokens.at(i).length);
  }

  return remainder.trimmed().isEmpty();
}

QString EmojiUtils::cdnUrl(const QString &id, bool animated) {
  return QString("https://cdn.discordapp.com/emojis/%1.%2")
      .arg(id, animated ? "gif" : "png");
}

QString EmojiUtils::marker(const QString &id, bool animated) {
  QString result;
  result += QChar(kMarkerStart);
  result += animated ? "a" : "s";
  result += ":";
  result += id;
  result += QChar(kMarkerEnd);
  return result;
}

QVariantList EmojiUtils::inlineTokens(const QString &html) {
  TokenBuilder b;
  int i = 0;

  while (i < html.length()) {
    QChar c = html.at(i);

    if (c.unicode() == kMarkerStart) {
      int end = html.indexOf(QChar(kMarkerEnd), i + 1);
      if (end < 0) {
        break;
      }
      QString spec = html.mid(i + 1, end - i - 1); // "a:123" / "s:123"
      b.flushWord();
      b.flushSpaces();
      QVariantMap token;
      token["t"] = "e";
      token["animated"] = spec.startsWith("a");
      token["id"] = spec.mid(2);
      b.tokens.append(token);
      i = end + 1;
      continue;
    }

    if (c == '<') {
      int end = html.indexOf('>', i);
      if (end < 0) {
        break;
      }
      QString tag = html.mid(i, end - i + 1);
      QString name = tagName(tag);
      i = end + 1;

      if (name == "br") {
        b.flushWord();
        b.flushSpaces();
        QVariantMap token;
        token["t"] = "n";
        b.tokens.append(token);
        continue;
      }

      bool closing = tag.startsWith("</");
      if (!closing) {
        b.flushSpaces();
        b.beginWordIfNeeded();
        b.openTags.append(tag);
        b.openNames.append(name);
        if (!b.body.isEmpty()) {
          b.body += tag;
        }
        // With an empty body the tag is re-opened from startTags at the next char.
        else {
          b.startTags = b.openTags;
        }
      } else {
        if (!b.openNames.isEmpty()) {
          b.openNames.removeLast();
          b.openTags.removeLast();
        }
        if (!b.body.isEmpty()) {
          b.body += tag;
        }
      }
      continue;
    }

    if (c == ' ') {
      b.flushWord();
      ++b.spaces;
      ++i;
      continue;
    }

    // Anything else is visible text; an HTML entity counts as one character.
    QString piece = c;
    if (c == '&') {
      int semi = html.indexOf(';', i);
      if (semi > i && semi - i <= 8) {
        piece = html.mid(i, semi - i + 1);
      }
    }
    b.flushSpaces();
    b.beginWordIfNeeded();
    b.body += piece;
    b.width += piece.length() > 1 && piece.startsWith('&') ? 0.60
                                                           : charWidthEm(c);
    i += piece.length();
  }

  b.flushWord();
  b.flushSpaces();
  return b.tokens;
}
