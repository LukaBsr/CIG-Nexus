#include "guild/GuildManager.hpp"

#include <algorithm>
#include <chrono>

namespace guild {

namespace {

uint64_t now_seconds() {
    const auto now = std::chrono::system_clock::now();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());
}

} // namespace

Guild& GuildManager::upsertGuild(const std::string& id, const std::string& name,
                                 const std::string& owner_id, GuildVisibility visibility) {
    Guild guild{id, name, owner_id, visibility, now_seconds()};
    auto [it, inserted] = guilds_.insert_or_assign(id, guild);

    return it->second;
}

void GuildManager::setGuildVisibility(const std::string& guild_id, GuildVisibility visibility) {
    Guild* guild = getGuild(guild_id);
    if (!guild) {
        return;
    }
    guild->visibility = visibility;
}

bool GuildManager::deleteGuild(const std::string& guild_id) {
    if (guilds_.erase(guild_id) == 0) {
        return false;
    }

    for (auto it = channels_.begin(); it != channels_.end();) {
        if (it->second.guild_id == guild_id) {
            it = channels_.erase(it);
        } else {
            ++it;
        }
    }

    // guild_ids_by_user_ is indexed by user_id, not guild_id — the members
    // to clean up have to be read out of member_ranks_[guild_id] before
    // it's erased below, since that's the only place that still knows who
    // they were.
    const auto ranks_it = member_ranks_.find(guild_id);
    if (ranks_it != member_ranks_.end()) {
        for (const auto& [user_id, rank] : ranks_it->second) {
            auto& ids = guild_ids_by_user_[user_id];
            ids.erase(std::remove(ids.begin(), ids.end(), guild_id), ids.end());
        }
    }
    member_ranks_.erase(guild_id);

    return true;
}

Channel& GuildManager::upsertChannel(const std::string& id, const std::string& guild_id,
                                     const std::string& name, ChannelType type) {
    Channel channel{id, guild_id, name, type, now_seconds()};
    auto [it, inserted] = channels_.insert_or_assign(id, channel);

    return it->second;
}

bool GuildManager::deleteChannel(const std::string& channel_id) {
    return channels_.erase(channel_id) > 0;
}

bool GuildManager::hasGuild(const std::string& guild_id) const {
    return guilds_.find(guild_id) != guilds_.end();
}

Guild* GuildManager::getGuild(const std::string& guild_id) {
    const auto it = guilds_.find(guild_id);
    if (it == guilds_.end()) {
        return nullptr;
    }
    return &it->second;
}

const Guild* GuildManager::getGuild(const std::string& guild_id) const {
    const auto it = guilds_.find(guild_id);
    if (it == guilds_.end()) {
        return nullptr;
    }
    return &it->second;
}

std::vector<Guild> GuildManager::listGuilds() const {
    std::vector<Guild> guilds;
    guilds.reserve(guilds_.size());
    for (const auto& [id, guild] : guilds_) {
        guilds.push_back(guild);
    }
    return guilds;
}

bool GuildManager::hasChannel(const std::string& channel_id) const {
    return channels_.find(channel_id) != channels_.end();
}

Channel* GuildManager::getChannel(const std::string& channel_id) {
    const auto it = channels_.find(channel_id);
    if (it == channels_.end()) {
        return nullptr;
    }
    return &it->second;
}

const Channel* GuildManager::getChannel(const std::string& channel_id) const {
    const auto it = channels_.find(channel_id);
    if (it == channels_.end()) {
        return nullptr;
    }
    return &it->second;
}

std::vector<Channel> GuildManager::listChannels(const std::string& guild_id) const {
    std::vector<Channel> channels;
    for (const auto& [id, channel] : channels_) {
        if (channel.guild_id == guild_id) {
            channels.push_back(channel);
        }
    }
    return channels;
}

bool GuildManager::isOwner(const std::string& guild_id, const std::string& user_id) const {
    const Guild* guild = getGuild(guild_id);
    if (!guild) {
        return false;
    }
    return guild->owner_id == user_id;
}

void GuildManager::setMemberRank(const std::string& guild_id, const std::string& user_id,
                                 int role_rank) {
    member_ranks_[guild_id][user_id] = role_rank;

    auto& ids = guild_ids_by_user_[user_id];
    if (std::find(ids.begin(), ids.end(), guild_id) == ids.end()) {
        ids.push_back(guild_id);
    }
}

void GuildManager::removeMember(const std::string& guild_id, const std::string& user_id) {
    const auto guild_it = member_ranks_.find(guild_id);
    if (guild_it != member_ranks_.end()) {
        guild_it->second.erase(user_id);
    }

    const auto user_it = guild_ids_by_user_.find(user_id);
    if (user_it != guild_ids_by_user_.end()) {
        auto& ids = user_it->second;
        ids.erase(std::remove(ids.begin(), ids.end(), guild_id), ids.end());
    }
}

std::optional<int> GuildManager::getMemberRank(const std::string& guild_id,
                                               const std::string& user_id) const {
    const auto guild_it = member_ranks_.find(guild_id);
    if (guild_it == member_ranks_.end()) {
        return std::nullopt;
    }
    const auto member_it = guild_it->second.find(user_id);
    if (member_it == guild_it->second.end()) {
        return std::nullopt;
    }
    return member_it->second;
}

std::vector<std::string> GuildManager::getGuildIdsForUser(const std::string& user_id) const {
    const auto it = guild_ids_by_user_.find(user_id);
    if (it == guild_ids_by_user_.end()) {
        return {};
    }
    return it->second;
}

bool GuildManager::hasRankAtLeast(const std::string& guild_id, const std::string& user_id,
                                  int min_rank) const {
    const std::optional<int> rank = getMemberRank(guild_id, user_id);
    return rank.has_value() && *rank >= min_rank;
}

bool GuildManager::isOfficerOrAbove(const std::string& guild_id, const std::string& user_id) const {
    return hasRankAtLeast(guild_id, user_id, kOfficerRank);
}

bool GuildManager::canCreateChannel(const std::string& guild_id, const std::string& user_id) const {
    return isOfficerOrAbove(guild_id, user_id);
}

bool GuildManager::canDeleteChannel(const std::string& guild_id, const std::string& user_id) const {
    return hasRankAtLeast(guild_id, user_id, kOwnerRank);
}

bool GuildManager::canSetMemberRole(const std::string& guild_id, const std::string& user_id) const {
    return hasRankAtLeast(guild_id, user_id, kOwnerRank);
}

bool GuildManager::canCreateInvite(const std::string& guild_id, const std::string& user_id) const {
    return isOfficerOrAbove(guild_id, user_id);
}

bool GuildManager::canApproveJoinRequest(const std::string& guild_id,
                                         const std::string& user_id) const {
    return isOfficerOrAbove(guild_id, user_id);
}

bool GuildManager::canSetGuildVisibility(const std::string& guild_id,
                                         const std::string& user_id) const {
    return hasRankAtLeast(guild_id, user_id, kOwnerRank);
}

} // namespace guild
