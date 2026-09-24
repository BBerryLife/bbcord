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

  // Fix: "@me" is NOT accepted here - confirmed via a real 400 response
  // ({"errors":{"user_id":{"_errors":[{"code":"NUMBER_TYPE_COERCE",
  // "message":"Value \"@me\" is not snowflake."}]}}}) - this route
  // needs the caller's actual numeric user id.
  request.requestPath = QString("/api/v9/guilds/%1/members/%2")
                            .arg(request.guildId)
                            .arg(safeUserId);

  request.type = SelfGuildMemberRequest;
  enqueueRequest(request);
}


void DiscordRestClient::fetchActiveThreads(const QString &token,
                                           const QString &channelId) {
  // Fix: GET /guilds/{id}/threads/active (used previously) always
  // returns 403 "Invalid Discord token" with a USER token (email/
  // password login), no matter how correct the token/permissions are -
  // confirmed through repeated real-world testing (log: every guild
  // returns 403, including guilds with admin rights). This is NOT a
  // code bug: this guild-level endpoint only works with BOT tokens by
  // Discord's own design (see community discussion in
  // discord.js-selfbot-v13 issue #1137 - "only bot accounts can fetch
  // active threads [at guild level]"). The CHANNEL-level endpoint
  // (GET /channels/{channel.id}/threads/active) has no such
  // restriction and works fine with user tokens - switched to calling
  // this instead, per channel that needs its threads, rather than once
  // for the whole guild.
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
  // Threads auto-archived by Discord (inactive past
  // auto_archive_duration) are NO LONGER in active threads/
  // THREAD_LIST_SYNC - need this separate endpoint to see them. Unlike
  // active threads (only a channel-level version exists, and both
  // active variants are confirmed working with user tokens) - this
  // archived endpoint is also in the /channels/{id}/... group
  // (channel-level, not guild-level), same group as the channel-level
  // active threads endpoint confirmed NOT blocked as bot-only, so it's
  // safe to assume it works with user tokens too.
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
