#include "guild/GuildManager.hpp"

#include <chrono>

namespace guild {

namespace {

uint64_t now_seconds() {
    const auto now = std::chrono::system_clock::now();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());
}

} // namespace

Guild& GuildManager::createGuild(const std::string& name, const std::string& owner_id) {
    const std::string id = "g_" + std::to_string(next_guild_id_++);

    Guild guild{id, name, owner_id, now_seconds()};
    auto [it, inserted] = guilds_.insert_or_assign(id, guild);

    return it->second;
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

    return true;
}

Channel& GuildManager::createChannel(const std::string& guild_id, const std::string& name,
                                     ChannelType type) {
    const std::string id = "c_" + std::to_string(next_channel_id_++);

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

bool GuildManager::canCreateChannel(const std::string& guild_id, const std::string& user_id) const {
    return isOwner(guild_id, user_id);
}

bool GuildManager::canDeleteChannel(const std::string& guild_id, const std::string& user_id) const {
    return isOwner(guild_id, user_id);
}

} // namespace guild
