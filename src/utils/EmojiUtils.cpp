#include "EmojiUtils.hpp"

#include <QRegExp>

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
