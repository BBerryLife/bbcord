#include "ClientState.hpp"

ClientState::ClientState() { resetSession(); }

void ClientState::resetSession() {
  dmChannelsById.clear();
  dmChannelIdsByRecipientId.clear();
  allDmChannelIndexById.clear();
  visibleDmChannelIndexById.clear();
  orderedGuildIds.clear();
  guildFolders.clear();
  dmPresenceByUserId.clear();
  pendingDmPresenceUserIds.clear();
  guilds.clear();
  resetDmChannels();
  resetGuildChannels();
  resetGatewayPendingUpdates();
  bootstrapCacheLoaded = false;
  lastGuildId.clear();
  lastDmChannelId.clear();
  selectedGuildId.clear();
  visibleDmChannelCount = 0;
  loadingGuilds = false;
  loadingDmChannels = false;
  loadingGuildChannels = false;
  guildsHasMore = true;
  dmChannelsHasMore = true;
  guildChannelsHasMore = false;
}

void ClientState::resetGuildChannels() {
  allGuildChannels.clear();
  visibleGuildChannels.clear();
  channelThreadsByParentId.clear();
  activeThreadChannelId.clear();
  activeThreadParentId.clear();
  visibleGuildChannelCount = 0;
  guildChannelsHasMore = false;
}

void ClientState::resetDmChannels() {
  allDmChannels.clear();
  dmChannels.clear();
  visibleDmChannelCount = 0;
  dmChannelsHasMore = true;
}

void ClientState::resetGatewayPendingUpdates() {
  pendingUnreadGuildIds.clear();
  pendingMentionCountsByGuildId.clear();
  pendingMentionCountsByChannelId.clear();
  pendingUnreadChannelIds.clear();
  gatewayUiUpdateQueued = false;
  pendingDmUiUpdate = false;
  guildsCacheSaveQueued = false;
  dmCacheSaveQueued = false;
}
