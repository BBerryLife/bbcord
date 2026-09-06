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

void DiscordRestClient::fetchActiveThreads(const QString &token,
                                           const QString &channelId) {
  // Fix: GET /guilds/{id}/threads/active (dùng trước đây) luôn trả 403
  // "Invalid Discord token" với USER token (login qua email/password),
  // bất kể token/permission đúng đến đâu - đã xác nhận qua nhiều lần
  // test thực tế (log: mọi guild đều 403, kể cả guild có quyền admin).
  // Đây KHÔNG phải bug code: endpoint cấp-guild này chỉ hoạt động với
  // BOT token theo chính thiết kế của Discord (xem thảo luận cộng đồng
  // discord.js-selfbot-v13 issue #1137 - "chỉ bot account mới dùng được
  // fetch active threads [cấp guild]"). Endpoint cấp-CHANNEL
  // (GET /channels/{channel.id}/threads/active) không có giới hạn này,
  // hoạt động với user token bình thường - đổi sang gọi endpoint này,
  // theo TỪNG channel cần xem threads, thay vì 1 lần cho cả guild.
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
  // Thread bị Discord tự động archive (không hoạt động quá
  // auto_archive_duration) KHÔNG còn nằm trong active threads/
  // THREAD_LIST_SYNC nữa - cần gọi riêng endpoint này để xem lại. Khác
  // active threads (chỉ có bản cấp-channel, và cả 2 phiên bản active
  // đều đã xác nhận hoạt động với user token) - endpoint archived này
  // cũng thuộc nhóm /channels/{id}/... (cấp-channel, không phải cấp-
  // guild), cùng nhóm với endpoint active threads cấp-channel đã xác
  // nhận KHÔNG bị chặn bot-only, nên tin cậy dùng được với user token.
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
