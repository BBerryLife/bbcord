#include "DiscordUtils.hpp"

#include <bb/data/JsonDataAccess>

namespace {
const int kProtoFieldGuildFolders = 14;
const int kProtoFieldGuildFolderItems = 1;
const int kProtoFieldGuildFolderGuildIds = 1;
const int kProtoFieldGuildFolderId = 2;
const int kProtoFieldGuildFolderName = 3;

bool snowflakeNewerThan(const QString &left, const QString &right) {
  if (left.isEmpty()) {
    return false;
  }

  if (right.isEmpty()) {
    return true;
  }

  if (left.size() != right.size()) {
    return left.size() > right.size();
  }

  return left > right;
}

bool readProtoVarint(const QByteArray &bytes, int *offset, quint64 *value) {
  if (offset == 0 || value == 0 || *offset < 0 || *offset >= bytes.size()) {
    return false;
  }

  quint64 result = 0;
  int shift = 0;
  while (*offset < bytes.size() && shift < 64) {
    unsigned char ch = static_cast<unsigned char>(bytes.at(*offset));
    ++(*offset);
    result |= (static_cast<quint64>(ch & 0x7f) << shift);
    if ((ch & 0x80) == 0) {
      *value = result;
      return true;
    }
    shift += 7;
  }

  return false;
}

bool readProtoLengthDelimited(const QByteArray &bytes, int *offset,
                              QByteArray *value) {
  quint64 length = 0;
  if (offset == 0 || value == 0 || !readProtoVarint(bytes, offset, &length) ||
      length > static_cast<quint64>(bytes.size() - *offset)) {
    return false;
  }

  *value = bytes.mid(*offset, static_cast<int>(length));
  *offset += static_cast<int>(length);
  return true;
}

bool skipProtoField(const QByteArray &bytes, int *offset, int wireType) {
  if (offset == 0) {
    return false;
  }

  switch (wireType) {
  case 0: {
    quint64 ignored = 0;
    return readProtoVarint(bytes, offset, &ignored);
  }
  case 1:
    if (*offset + 8 > bytes.size()) {
      return false;
    }
    *offset += 8;
    return true;
  case 2: {
    QByteArray ignored;
    return readProtoLengthDelimited(bytes, offset, &ignored);
  }
  case 5:
    if (*offset + 4 > bytes.size()) {
      return false;
    }
    *offset += 4;
    return true;
  default:
    return false;
  }
}

void appendGuildIdsFromProtoBytes(QStringList *orderedGuildIds,
                                  const QByteArray &guildBytes) {
  for (int i = 0; i + 7 < guildBytes.size(); i += 8) {
    quint64 guildId = 0;
    for (int j = 0; j < 8; ++j) {
      guildId |= (static_cast<quint64>(
                      static_cast<unsigned char>(guildBytes.at(i + j)))
                  << (j * 8));
    }
    DiscordUtils::appendUniqueGuildId(orderedGuildIds,
                                      QString::number(guildId));
  }
}

QString snowflakeFromProtoBytes(const QByteArray &guildBytes, int index) {
  if (index < 0 || index + 7 >= guildBytes.size()) {
    return QString();
  }

  quint64 guildId = 0;
  for (int j = 0; j < 8; ++j) {
    guildId |= (static_cast<quint64>(
                    static_cast<unsigned char>(guildBytes.at(index + j)))
                << (j * 8));
  }
  return QString::number(guildId);
}

QString wrappedVarintValue(const QByteArray &bytes) {
  int offset = 0;
  while (offset < bytes.size()) {
    quint64 tag = 0;
    if (!readProtoVarint(bytes, &offset, &tag)) {
      return QString();
    }

    int fieldNumber = static_cast<int>(tag >> 3);
    int wireType = static_cast<int>(tag & 0x07);
    if (fieldNumber == 1 && wireType == 0) {
      quint64 value = 0;
      if (!readProtoVarint(bytes, &offset, &value)) {
        return QString();
      }
      return QString::number(value);
    }

    if (!skipProtoField(bytes, &offset, wireType)) {
      return QString();
    }
  }
  return QString();
}

QString wrappedStringValue(const QByteArray &bytes) {
  int offset = 0;
  while (offset < bytes.size()) {
    quint64 tag = 0;
    if (!readProtoVarint(bytes, &offset, &tag)) {
      return QString();
    }

    int fieldNumber = static_cast<int>(tag >> 3);
    int wireType = static_cast<int>(tag & 0x07);
    if (fieldNumber == 1 && wireType == 2) {
      QByteArray value;
      if (!readProtoLengthDelimited(bytes, &offset, &value)) {
        return QString();
      }
      return QString::fromUtf8(value);
    }

    if (!skipProtoField(bytes, &offset, wireType)) {
      return QString();
    }
  }
  return QString();
}

QVariantMap guildFolderFromProtoItem(const QByteArray &itemBytes) {
  QVariantMap folder;
  QVariantList guildIds;

  int offset = 0;
  while (offset < itemBytes.size()) {
    quint64 tag = 0;
    if (!readProtoVarint(itemBytes, &offset, &tag)) {
      return folder;
    }

    int fieldNumber = static_cast<int>(tag >> 3);
    int wireType = static_cast<int>(tag & 0x07);

    if (fieldNumber == kProtoFieldGuildFolderGuildIds && wireType == 2) {
      QByteArray guildBytes;
      if (!readProtoLengthDelimited(itemBytes, &offset, &guildBytes)) {
        return folder;
      }
      for (int i = 0; i + 7 < guildBytes.size(); i += 8) {
        QString guildId = snowflakeFromProtoBytes(guildBytes, i);
        if (!guildId.isEmpty()) {
          guildIds.append(guildId);
        }
      }
    } else if (fieldNumber == kProtoFieldGuildFolderId && wireType == 2) {
      QByteArray idBytes;
      if (!readProtoLengthDelimited(itemBytes, &offset, &idBytes)) {
        return folder;
      }
      folder["id"] = wrappedVarintValue(idBytes);
    } else if (fieldNumber == kProtoFieldGuildFolderName && wireType == 2) {
      QByteArray nameBytes;
      if (!readProtoLengthDelimited(itemBytes, &offset, &nameBytes)) {
        return folder;
      }
      folder["name"] = wrappedStringValue(nameBytes);
    } else if (!skipProtoField(itemBytes, &offset, wireType)) {
      return folder;
    }
  }

  folder["guild_ids"] = guildIds;
  return folder;
}

void appendGuildIdsFromProtoFolderItem(QStringList *orderedGuildIds,
                                       const QByteArray &itemBytes) {
  int offset = 0;
  while (offset < itemBytes.size()) {
    quint64 tag = 0;
    if (!readProtoVarint(itemBytes, &offset, &tag)) {
      return;
    }

    int fieldNumber = static_cast<int>(tag >> 3);
    int wireType = static_cast<int>(tag & 0x07);

    if (fieldNumber == kProtoFieldGuildFolderGuildIds && wireType == 2) {
      QByteArray guildBytes;
      if (!readProtoLengthDelimited(itemBytes, &offset, &guildBytes)) {
        return;
      }
      appendGuildIdsFromProtoBytes(orderedGuildIds, guildBytes);
    } else if (!skipProtoField(itemBytes, &offset, wireType)) {
      return;
    }
  }
}
} // namespace

QByteArray DiscordUtils::desktopUserAgent() {
  return QByteArray("Mozilla/5.0 (Windows NT 10.0; Win64; x64) ") +
         QByteArray("AppleWebKit/537.36 (KHTML, like Gecko) ") +
         QByteArray("Chrome/149.0.0.0 Safari/537.36");
}

QByteArray DiscordUtils::desktopUserAgentHeader() {
  return QByteArray("User-Agent: ") + desktopUserAgent() + QByteArray("\r\n");
}

QByteArray DiscordUtils::superPropertiesHeader() {
  // Mirrors the User-Agent string above (Chrome 149 on Windows 10/11) so
  // the two headers agree - a mismatched pair (e.g. claiming Chrome in one
  // header and a different browser/version in the other) is itself a
  // signal Discord's risk scoring can key on. Values are otherwise not
  // required to be exact/current; see the header comment in
  // DiscordUtils.hpp.
  //
  // Deliberately omits client_event_source (and any other optional field
  // whose "empty" value isn't a plain string/number/bool) - it was
  // previously set via QVariant() (null), and this old bb::data JSON
  // serializer's handling of a null QVariant is undocumented/untested here.
  // If it produced anything other than a literal JSON "null" (e.g. an
  // empty object, or dropping the key with a trailing comma), the embedded
  // JSON inside this base64 header would be malformed, and Discord
  // rejecting an unparseable X-Super-Properties value as a blanket "Invalid
  // Form Body" on the whole request - not specifically about this field -
  // would look exactly like the regression seen after adding this header.
  // Every field below is a plain string/int, which this serializer is
  // already known to handle correctly (it's the same pattern used
  // elsewhere in this file for years).
  QVariantMap properties;
  properties["os"] = QString("Windows");
  properties["browser"] = QString("Chrome");
  properties["device"] = QString("");
  properties["system_locale"] = QString("en-US");
  properties["browser_user_agent"] = QString::fromUtf8(desktopUserAgent());
  properties["browser_version"] = QString("149.0.0.0");
  properties["os_version"] = QString("10");
  properties["referrer"] = QString("");
  properties["referring_domain"] = QString("");
  properties["referrer_current"] = QString("");
  properties["referring_domain_current"] = QString("");
  properties["release_channel"] = QString("stable");
  properties["client_build_number"] = 400000;

  bb::data::JsonDataAccess json;
  QByteArray propertiesJson;
  json.saveToBuffer(properties, &propertiesJson);

  QByteArray encoded = propertiesJson.toBase64();
  return QByteArray("X-Super-Properties: ") + encoded + QByteArray("\r\n");
}

QString DiscordUtils::firstLetter(const QString &text) {
  if (text.isEmpty()) {
    return "?";
  }

  return text.left(1).toUpper();
}

QString DiscordUtils::firstTwoWordLetters(const QString &text) {
  QStringList parts = text.split(' ', QString::SkipEmptyParts);
  QString initials;
  for (int i = 0; i < parts.size() && initials.size() < 2; ++i) {
    initials += parts.at(i).left(1).toUpper();
  }

  if (initials.size() < 2 && parts.size() == 1 && parts.first().size() > 1) {
    initials = parts.first().left(2).toUpper();
  }

  if (initials.isEmpty()) {
    return firstLetter(text);
  }

  return initials;
}

QString DiscordUtils::cleanSnowflake(const QVariant &value) {
  return value.toString().trimmed();
}

void DiscordUtils::appendUniqueGuildId(QStringList *guildIds,
                                       const QString &guildId) {
  QString cleanGuildId = guildId.trimmed();
  if (guildIds == 0 || cleanGuildId.isEmpty() ||
      guildIds->contains(cleanGuildId)) {
    return;
  }

  guildIds->append(cleanGuildId);
}

void DiscordUtils::appendGuildIdsFromUserSettingsProto(
    QStringList *orderedGuildIds, const QString &base64Proto) {
  QByteArray settings = QByteArray::fromBase64(base64Proto.toLatin1());
  if (orderedGuildIds == 0 || settings.isEmpty()) {
    return;
  }

  int offset = 0;
  while (offset < settings.size()) {
    quint64 tag = 0;
    if (!readProtoVarint(settings, &offset, &tag)) {
      return;
    }

    int fieldNumber = static_cast<int>(tag >> 3);
    int wireType = static_cast<int>(tag & 0x07);

    if (fieldNumber == kProtoFieldGuildFolders && wireType == 2) {
      QByteArray guildFolders;
      if (!readProtoLengthDelimited(settings, &offset, &guildFolders)) {
        return;
      }

      int foldersOffset = 0;
      while (foldersOffset < guildFolders.size()) {
        quint64 folderTag = 0;
        if (!readProtoVarint(guildFolders, &foldersOffset, &folderTag)) {
          return;
        }

        int folderFieldNumber = static_cast<int>(folderTag >> 3);
        int folderWireType = static_cast<int>(folderTag & 0x07);

        if (folderFieldNumber == kProtoFieldGuildFolderItems &&
            folderWireType == 2) {
          QByteArray itemBytes;
          if (!readProtoLengthDelimited(guildFolders, &foldersOffset,
                                        &itemBytes)) {
            return;
          }
          appendGuildIdsFromProtoFolderItem(orderedGuildIds, itemBytes);
        } else if (!skipProtoField(guildFolders, &foldersOffset,
                                   folderWireType)) {
          return;
        }
      }
    } else if (!skipProtoField(settings, &offset, wireType)) {
      return;
    }
  }
}

QVariantList
DiscordUtils::guildFoldersFromUserSettingsProto(const QString &base64Proto) {
  QVariantList folders;
  QByteArray settings = QByteArray::fromBase64(base64Proto.toLatin1());
  if (settings.isEmpty()) {
    return folders;
  }

  int offset = 0;
  while (offset < settings.size()) {
    quint64 tag = 0;
    if (!readProtoVarint(settings, &offset, &tag)) {
      return folders;
    }

    int fieldNumber = static_cast<int>(tag >> 3);
    int wireType = static_cast<int>(tag & 0x07);

    if (fieldNumber == kProtoFieldGuildFolders && wireType == 2) {
      QByteArray guildFolders;
      if (!readProtoLengthDelimited(settings, &offset, &guildFolders)) {
        return folders;
      }

      int foldersOffset = 0;
      while (foldersOffset < guildFolders.size()) {
        quint64 folderTag = 0;
        if (!readProtoVarint(guildFolders, &foldersOffset, &folderTag)) {
          return folders;
        }

        int folderFieldNumber = static_cast<int>(folderTag >> 3);
        int folderWireType = static_cast<int>(folderTag & 0x07);
        if (folderFieldNumber == kProtoFieldGuildFolderItems &&
            folderWireType == 2) {
          QByteArray itemBytes;
          if (!readProtoLengthDelimited(guildFolders, &foldersOffset,
                                        &itemBytes)) {
            return folders;
          }
          QVariantMap folder = guildFolderFromProtoItem(itemBytes);
          if (!folder.value("guild_ids").toList().isEmpty()) {
            folders.append(folder);
          }
        } else if (!skipProtoField(guildFolders, &foldersOffset,
                                   folderWireType)) {
          return folders;
        }
      }
    } else if (!skipProtoField(settings, &offset, wireType)) {
      return folders;
    }
  }

  return folders;
}

bool DiscordUtils::positionShouldMoveBefore(const QVariantMap &left,
                                            const QVariantMap &right) {
  if (!left.contains("position") || !right.contains("position")) {
    return false;
  }

  int leftPosition = left.value("position").toInt();
  int rightPosition = right.value("position").toInt();
  return leftPosition < rightPosition;
}

bool DiscordUtils::dmShouldMoveBefore(const QVariantMap &left,
                                      const QVariantMap &right) {
  return snowflakeNewerThan(cleanSnowflake(left.value("lastMessageId")),
                            cleanSnowflake(right.value("lastMessageId")));
}

void DiscordUtils::stableSortItems(
    QVariantList *items, bool (*shouldMoveBefore)(const QVariantMap &left,
                                                  const QVariantMap &right)) {
  if (items == 0 || items->size() < 2 || shouldMoveBefore == 0) {
    return;
  }

  for (int i = 1; i < items->size(); ++i) {
    QVariant current = items->at(i);
    QVariantMap currentMap = current.toMap();
    int j = i - 1;
    while (j >= 0 && shouldMoveBefore(currentMap, items->at(j).toMap())) {
      items->replace(j + 1, items->at(j));
      --j;
    }
    items->replace(j + 1, current);
  }
}
