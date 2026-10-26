#include "MessageCache.hpp"

namespace {
const int kDefaultPageSize = 50;
const qint64 kMessageGroupWindowMs = 7LL * 60LL * 1000LL;
} // namespace

MessageCache::ChannelState::ChannelState()
    : initialLoaded(false), hasMoreBefore(true), loadingInitial(false),
      loadingBefore(false) {}

MessageCache::MessageCache(int maxMessagesPerChannel)
    : m_maxMessagesPerChannel(maxMessagesPerChannel) {
  if (m_maxMessagesPerChannel < kDefaultPageSize) {
    m_maxMessagesPerChannel = kDefaultPageSize;
  }
}

bool MessageCache::hasChannel(const QString &channelId) const {
  return m_channels.contains(channelId);
}

bool MessageCache::isInitialLoaded(const QString &channelId) const {
  const ChannelState *state = channel(channelId);
  return state != 0 && state->initialLoaded;
}

bool MessageCache::isLoadingInitial(const QString &channelId) const {
  const ChannelState *state = channel(channelId);
  return state != 0 && state->loadingInitial;
}

bool MessageCache::isLoadingBefore(const QString &channelId) const {
  const ChannelState *state = channel(channelId);
  return state != 0 && state->loadingBefore;
}

bool MessageCache::hasMoreBefore(const QString &channelId) const {
  const ChannelState *state = channel(channelId);
  return state == 0 || state->hasMoreBefore;
}

QStringList MessageCache::initialLoadedChannelIds() const {
  QStringList ids;
  QHash<QString, ChannelState>::const_iterator it = m_channels.constBegin();
  for (; it != m_channels.constEnd(); ++it) {
    if (it.value().initialLoaded) {
      ids.append(it.key());
    }
  }
  return ids;
}

QString MessageCache::oldestMessageId(const QString &channelId) const {
  const ChannelState *state = channel(channelId);
  return state == 0 ? QString() : state->oldestMessageId;
}

QString MessageCache::newestMessageId(const QString &channelId) const {
  const ChannelState *state = channel(channelId);
  return state == 0 ? QString() : state->newestMessageId;
}

void MessageCache::setLoadingInitial(const QString &channelId, bool loading) {
  ensureChannel(channelId).loadingInitial = loading;
}

void MessageCache::setLoadingBefore(const QString &channelId, bool loading) {
  ensureChannel(channelId).loadingBefore = loading;
}

void MessageCache::setHasMoreBefore(const QString &channelId, bool hasMore) {
  ensureChannel(channelId).hasMoreBefore = hasMore;
}

void MessageCache::setChannelGuild(const QString &channelId,
                                   const QString &guildId) {
  ensureChannel(channelId).guildId = guildId;
}

QVariantList MessageCache::messagesForChannel(const QString &channelId) const {
  QVariantList result;
  const ChannelState *state = channel(channelId);
  if (state == 0) {
    return result;
  }

  for (int i = 0; i < state->orderedIds.size(); ++i) {
    const QString &id = state->orderedIds.at(i);
    if (state->messagesById.contains(id)) {
      result.append(state->messagesById.value(id).toVariantMap());
    }
  }
  return result;
}

bool MessageCache::containsMessage(const QString &channelId,
                                   const QString &messageId) const {
  const ChannelState *state = channel(channelId);
  return state != 0 && state->messagesById.contains(messageId);
}

DiscordMessage MessageCache::message(const QString &channelId,
                                     const QString &messageId) const {
  const ChannelState *state = channel(channelId);
  if (state == 0) {
    return DiscordMessage();
  }
  return state->messagesById.value(messageId);
}

void MessageCache::setInitialMessages(const QString &channelId,
                                      const QString &guildId,
                                      const QList<DiscordMessage> &messages,
                                      bool hasMore) {
  ChannelState &state = ensureChannel(channelId);
  state.guildId = guildId;
  state.orderedIds.clear();
  state.messagesById.clear();
  state.pendingNonces.clear();

  for (int i = 0; i < messages.size(); ++i) {
    insertSorted(state, messages.at(i));
  }

  state.initialLoaded = true;
  state.hasMoreBefore = hasMore;
  state.loadingInitial = false;
  state.loadingBefore = false;
  trimChannel(state);
  recalculateGrouping(state);
  refreshBounds(state);
}

QVariantList
MessageCache::prependOlderMessages(const QString &channelId,
                                   const QList<DiscordMessage> &messages,
                                   bool hasMore) {
  ChannelState &state = ensureChannel(channelId);
  QVariantList added;
  QStringList addedIds;

  for (int i = 0; i < messages.size(); ++i) {
    const DiscordMessage &message = messages.at(i);
    if (message.id.isEmpty() || state.messagesById.contains(message.id)) {
      continue;
    }
    insertSorted(state, message);
    addedIds.append(message.id);
  }

  state.hasMoreBefore = hasMore;
  state.loadingBefore = false;
  trimChannelFromBack(state);
  recalculateGrouping(state);
  refreshBounds(state);

  for (int i = 0; i < addedIds.size(); ++i) {
    const QString &id = addedIds.at(i);
    if (state.messagesById.contains(id)) {
      added.append(state.messagesById.value(id).toVariantMap());
    }
  }
  return added;
}

QVariantMap MessageCache::addOrReplaceMessage(const DiscordMessage &message) {
  if (message.channelId.isEmpty() || message.id.isEmpty()) {
    return QVariantMap();
  }

  ChannelState &state = ensureChannel(message.channelId);
  if (!message.guildId.isEmpty()) {
    state.guildId = message.guildId;
  }

  QString idToRemove;
  if (!message.nonce.isEmpty() && state.pendingNonces.contains(message.nonce)) {
    idToRemove = message.nonce;
  }

  if (idToRemove.isEmpty() && !message.nonce.isEmpty()) {
    for (int i = 0; i < state.orderedIds.size(); ++i) {
      const QString &id = state.orderedIds.at(i);
      DiscordMessage old = state.messagesById.value(id);
      if (old.pending && old.id == message.nonce) {
        idToRemove = id;
        break;
      }
    }
  }

  if (!idToRemove.isEmpty()) {
    state.messagesById.remove(idToRemove);
    state.orderedIds.removeAll(idToRemove);
    state.pendingNonces.remove(idToRemove);
  }

  insertSorted(state, message);
  trimChannel(state);
  recalculateGrouping(state);
  refreshBounds(state);
  return state.messagesById.value(message.id).toVariantMap();
}

QVariantMap
MessageCache::addOrReplaceMessages(const QList<DiscordMessage> &messages) {
  QVariantMap changedByChannel;
  QHash<QString, QStringList> changedIdsByChannel;

  for (int i = 0; i < messages.size(); ++i) {
    const DiscordMessage &message = messages.at(i);
    if (message.channelId.isEmpty() || message.id.isEmpty()) {
      continue;
    }

    ChannelState &state = ensureChannel(message.channelId);
    if (!message.guildId.isEmpty()) {
      state.guildId = message.guildId;
    }

    QString idToRemove;
    if (!message.nonce.isEmpty() &&
        state.pendingNonces.contains(message.nonce)) {
      idToRemove = message.nonce;
    }

    if (idToRemove.isEmpty() && !message.nonce.isEmpty()) {
      for (int j = 0; j < state.orderedIds.size(); ++j) {
        const QString &id = state.orderedIds.at(j);
        DiscordMessage old = state.messagesById.value(id);
        if (old.pending && old.id == message.nonce) {
          idToRemove = id;
          break;
        }
      }
    }

    if (!idToRemove.isEmpty()) {
      state.messagesById.remove(idToRemove);
      state.orderedIds.removeAll(idToRemove);
      state.pendingNonces.remove(idToRemove);
    }

    insertSorted(state, message, false);
    QStringList ids = changedIdsByChannel.value(message.channelId);
    if (!ids.contains(message.id)) {
      ids.append(message.id);
    }
    changedIdsByChannel.insert(message.channelId, ids);
  }

  QHash<QString, QStringList>::const_iterator it =
      changedIdsByChannel.constBegin();
  for (; it != changedIdsByChannel.constEnd(); ++it) {
    ChannelState &state = ensureChannel(it.key());
    trimChannel(state, false);
    recalculateGrouping(state);
    refreshBounds(state);

    QVariantList changed;
    const QStringList ids = it.value();
    for (int i = 0; i < ids.size(); ++i) {
      const QString &id = ids.at(i);
      if (state.messagesById.contains(id)) {
        changed.append(state.messagesById.value(id).toVariantMap());
      }
    }
    if (!changed.isEmpty()) {
      changedByChannel.insert(it.key(), changed);
    }
  }

  return changedByChannel;
}

QVariantMap MessageCache::updateMessage(const DiscordMessage &message) {
  if (message.channelId.isEmpty() || message.id.isEmpty()) {
    return QVariantMap();
  }

  ChannelState &state = ensureChannel(message.channelId);
  if (state.messagesById.contains(message.id)) {
    DiscordMessage merged = state.messagesById.value(message.id);
    if (!message.content.isNull()) {
      merged.content = message.content;
    }
    if (!message.editedTimestamp.isNull()) {
      merged.editedTimestamp = message.editedTimestamp;
    }
    if (!message.timestamp.isEmpty()) {
      merged.timestamp = message.timestamp;
    }
    if (!message.author.id.isEmpty()) {
      merged.author = message.author;
    }
    if (!message.attachments.isEmpty()) {
      merged.attachments = message.attachments;
    }
    state.messagesById.insert(message.id, merged);
    recalculateGrouping(state);
    return state.messagesById.value(message.id).toVariantMap();
  }

  insertSorted(state, message);
  trimChannel(state);
  recalculateGrouping(state);
  refreshBounds(state);
  return state.messagesById.value(message.id).toVariantMap();
}

bool MessageCache::deleteMessage(const QString &channelId,
                                 const QString &messageId) {
  ChannelState *state = &ensureChannel(channelId);
  if (!state->messagesById.contains(messageId)) {
    return false;
  }

  state->messagesById.remove(messageId);
  state->orderedIds.removeAll(messageId);
  state->pendingNonces.remove(messageId);
  recalculateGrouping(*state);
  refreshBounds(*state);
  return true;
}

QString MessageCache::addPendingMessage(const DiscordMessage &message) {
  if (message.channelId.isEmpty() || message.id.isEmpty()) {
    return QString();
  }

  DiscordMessage pending = message;
  pending.pending = true;
  pending.failed = false;
  ChannelState &state = ensureChannel(pending.channelId);
  state.pendingNonces.insert(pending.id);
  insertSorted(state, pending);
  trimChannel(state);
  recalculateGrouping(state);
  refreshBounds(state);
  return pending.id;
}

void MessageCache::markPendingFailed(const QString &channelId,
                                     const QString &messageId) {
  ChannelState &state = ensureChannel(channelId);
  if (!state.messagesById.contains(messageId)) {
    return;
  }

  DiscordMessage message = state.messagesById.value(messageId);
  message.pending = false;
  message.failed = true;
  state.messagesById.insert(messageId, message);
  state.pendingNonces.remove(messageId);
  recalculateGrouping(state);
}

void MessageCache::clear() { m_channels.clear(); }

MessageCache::ChannelState &
MessageCache::ensureChannel(const QString &channelId) {
  if (!m_channels.contains(channelId)) {
    m_channels.insert(channelId, ChannelState());
  }
  return m_channels[channelId];
}

const MessageCache::ChannelState *
MessageCache::channel(const QString &channelId) const {
  QHash<QString, ChannelState>::const_iterator it = m_channels.find(channelId);
  if (it == m_channels.end()) {
    return 0;
  }
  return &it.value();
}

void MessageCache::insertSorted(ChannelState &state,
                                const DiscordMessage &message,
                                bool updateGrouping) {
  if (message.id.isEmpty()) {
    return;
  }

  if (state.messagesById.contains(message.id)) {
    state.messagesById.insert(message.id, message);
    if (updateGrouping) {
      recalculateGrouping(state);
    }
    return;
  }

  // Pending (not yet confirmed) messages always go last: their id is a locally built
  // snowflake, and a skewed device clock must not slot them into the middle of the
  // list.
  int insertAt = state.orderedIds.size();
  for (int i = 0; i < state.orderedIds.size() && !message.pending; ++i) {
    if (compareMessageIds(message.id, state.orderedIds.at(i)) < 0) {
      insertAt = i;
      break;
    }
  }

  state.orderedIds.insert(insertAt, message.id);
  state.messagesById.insert(message.id, message);
  if (updateGrouping) {
    recalculateGrouping(state);
  }
}

void MessageCache::trimChannel(ChannelState &state, bool updateGrouping) {
  while (state.orderedIds.size() > m_maxMessagesPerChannel) {
    QString removeId = state.orderedIds.first();
    DiscordMessage message = state.messagesById.value(removeId);
    if (message.pending) {
      break;
    }
    state.orderedIds.removeFirst();
    state.messagesById.remove(removeId);
    state.pendingNonces.remove(removeId);
  }
  if (updateGrouping) {
    recalculateGrouping(state);
  }
}

void MessageCache::trimChannelFromBack(ChannelState &state,
                                       bool updateGrouping) {
  while (state.orderedIds.size() > m_maxMessagesPerChannel) {
    int removeIndex = -1;
    for (int i = state.orderedIds.size() - 1; i >= 0; --i) {
      DiscordMessage message = state.messagesById.value(state.orderedIds.at(i));
      if (!message.pending) {
        removeIndex = i;
        break;
      }
    }

    if (removeIndex < 0) {
      break;
    }

    QString removeId = state.orderedIds.at(removeIndex);
    state.orderedIds.removeAt(removeIndex);
    state.messagesById.remove(removeId);
    state.pendingNonces.remove(removeId);
  }
  if (updateGrouping) {
    recalculateGrouping(state);
  }
}

void MessageCache::recalculateGrouping(ChannelState &state) {
  for (int i = 0; i < state.orderedIds.size(); ++i) {
    const QString &id = state.orderedIds.at(i);
    if (!state.messagesById.contains(id)) {
      continue;
    }

    DiscordMessage message = state.messagesById.value(id);
    message.isGroupStart = true;
    message.isGroupEnd = true;
    message.showAvatar = true;
    message.showUsername = true;
    message.showTimestamp = true;
    state.messagesById.insert(id, message);
  }

  for (int i = 1; i < state.orderedIds.size(); ++i) {
    const QString previousId = state.orderedIds.at(i - 1);
    const QString currentId = state.orderedIds.at(i);
    if (!state.messagesById.contains(previousId) ||
        !state.messagesById.contains(currentId)) {
      continue;
    }

    DiscordMessage previous = state.messagesById.value(previousId);
    DiscordMessage current = state.messagesById.value(currentId);
    qint64 previousTimestamp = previous.timestampMs();
    qint64 currentTimestamp = current.timestampMs();
    qint64 timestampDistance = currentTimestamp - previousTimestamp;
    if (timestampDistance < 0) {
      timestampDistance = -timestampDistance;
    }

    if (!previous.author.id.isEmpty() &&
        previous.author.id == current.author.id && previousTimestamp > 0 &&
        currentTimestamp > 0 && timestampDistance < kMessageGroupWindowMs) {
      previous.isGroupEnd = false;
      current.isGroupStart = false;
      current.showAvatar = false;
      current.showUsername = false;
      current.showTimestamp = false;
      state.messagesById.insert(previousId, previous);
      state.messagesById.insert(currentId, current);
    }
  }
}

int MessageCache::compareMessageIds(const QString &left,
                                    const QString &right) const {
  bool leftOk = false;
  bool rightOk = false;
  qulonglong leftValue = left.toULongLong(&leftOk);
  qulonglong rightValue = right.toULongLong(&rightOk);
  if (leftOk && rightOk) {
    if (leftValue < rightValue) {
      return -1;
    }
    if (leftValue > rightValue) {
      return 1;
    }
    return 0;
  }
  return QString::compare(left, right);
}

void MessageCache::refreshBounds(ChannelState &state) {
  if (state.orderedIds.isEmpty()) {
    state.oldestMessageId.clear();
    state.newestMessageId.clear();
    return;
  }

  state.oldestMessageId = state.orderedIds.first();
  state.newestMessageId = state.orderedIds.last();
}
