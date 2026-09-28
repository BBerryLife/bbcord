#include "../RestClient.hpp"

void DiscordRestClient::fetchDmChannels(const QString &token, int limit,
                                        const QString &afterId) {
  Q_UNUSED(limit);
  Q_UNUSED(afterId);

  RestRequest request;
  request.token = token.trimmed();
  if (request.token.isEmpty()) {
    emit requestFailed("Token is empty");
    return;
  }

  request.requestPath = "/api/v9/users/@me/channels";

  request.type = DmChannelsRequest;
  enqueueRequest(request);
}

void DiscordRestClient::fetchGuildChannels(const QString &token,
                                           const QString &guildId, int limit,
                                           const QString &afterId) {
  Q_UNUSED(limit);
  Q_UNUSED(afterId);

  RestRequest request;
  request.token = token.trimmed();
  request.guildId = guildId.trimmed();
  if (request.token.isEmpty() || request.guildId.isEmpty()) {
    emit requestFailed("Guild request is empty");
    return;
  }

  request.requestPath =
      QString("/api/v9/guilds/%1/channels").arg(request.guildId);

  request.type = GuildChannelsRequest;
  enqueueRequest(request);
}

void DiscordRestClient::fetchSelfGuildMember(const QString &token,
                                             const QString &guildId,
                                             const QString &userId) {
  RestRequest request;
  request.token = token.trimmed();
  request.guildId = guildId.trimmed();
  QString safeUserId = userId.trimmed();
  if (request.token.isEmpty() || request.guildId.isEmpty() ||
      safeUserId.isEmpty()) {
    emit requestFailed("Self member request is empty");
    return;
  }

  // Use the caller's numeric user id: "@me" is rejected with a 400
  // (NUMBER_TYPE_COERCE, "not snowflake").
  request.requestPath = QString("/api/v9/guilds/%1/members/%2")
                            .arg(request.guildId)
                            .arg(safeUserId);

  request.type = SelfGuildMemberRequest;
  enqueueRequest(request);
}


void DiscordRestClient::fetchActiveThreads(const QString &token,
                                           const QString &channelId) {
  // Uses the channel-level endpoint (/channels/{id}/threads/active), called per channel:
  // the guild-level one (/guilds/{id}/threads/active) is bot-only and returns 403 for
  // user tokens.
  RestRequest request;
  request.token = token.trimmed();
  request.channelId = channelId.trimmed();
  if (request.token.isEmpty() || request.channelId.isEmpty()) {
    emit requestFailed("Channel request is empty");
    return;
  }

  request.requestPath =
      QString("/api/v9/channels/%1/threads/active").arg(request.channelId);

  request.type = ActiveThreadsRequest;
  enqueueRequest(request);
}

void DiscordRestClient::fetchArchivedThreads(const QString &token,
                                             const QString &channelId,
                                             const QString &beforeCursor) {
  // Auto-archived threads are not in active threads/THREAD_LIST_SYNC; this channel-level
  // endpoint (not bot-only) lists them.
  RestRequest request;
  request.token = token.trimmed();
  request.channelId = channelId.trimmed();
  if (request.token.isEmpty() || request.channelId.isEmpty()) {
    emit requestFailed("Channel request is empty");
    return;
  }

  request.requestPath =
      QString("/api/v9/channels/%1/threads/archived/public")
          .arg(request.channelId);
  QString trimmedCursor = beforeCursor.trimmed();
  if (!trimmedCursor.isEmpty()) {
    request.requestPath += QString("?before=%1").arg(trimmedCursor);
  }

  request.type = ArchivedThreadsRequest;
  enqueueRequest(request);
}

void DiscordRestClient::fetchChannelInfo(const QString &token,
                                         const QString &channelId) {
  RestRequest request;
  request.token = token.trimmed();
  request.channelId = channelId.trimmed();
  if (request.token.isEmpty() || request.channelId.isEmpty()) {
    emit requestFailed("Channel info request is empty");
    return;
  }

  request.requestPath = QString("/api/v9/channels/%1").arg(request.channelId);

  request.type = ChannelInfoRequest;
  enqueueRequest(request);
}
