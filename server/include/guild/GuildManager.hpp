#ifndef CIG_NEXUS_GUILD_GUILD_MANAGER_HPP
#define CIG_NEXUS_GUILD_GUILD_MANAGER_HPP

#include <atomic>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "guild/Channel.hpp"
#include "guild/Guild.hpp"

namespace guild {

// GuildManager owns the guild/channel catalog (existence, ownership, names).
// It does not track which connection is a member of what — that's
// per-connection state and lives on session::Session, the same way
// SessionManager already owns username instead of a separate manager.
class GuildManager {
  public:
    Guild& createGuild(const std::string& name, const std::string& owner_id);

    // Deletes the guild and cascades to delete all of its channels. Callers
    // needing to clean up per-connection membership/active-channel state
    // (session::SessionManager::purgeGuildMembership) must capture
    // listChannels(guild_id) *before* calling this, since the channel list
    // won't be queryable afterward.
    bool deleteGuild(const std::string& guild_id);

    Channel& createChannel(const std::string& guild_id, const std::string& name, ChannelType type);
    bool deleteChannel(const std::string& channel_id);

    bool hasGuild(const std::string& guild_id) const;
    Guild* getGuild(const std::string& guild_id);
    const Guild* getGuild(const std::string& guild_id) const;
    std::vector<Guild> listGuilds() const;

    bool hasChannel(const std::string& channel_id) const;
    Channel* getChannel(const std::string& channel_id);
    const Channel* getChannel(const std::string& channel_id) const;
    std::vector<Channel> listChannels(const std::string& guild_id) const;

    bool isOwner(const std::string& guild_id, const std::string& user_id) const;

    // Authorization choke points: today both are just isOwner, but handlers
    // must call these named predicates rather than isOwner directly. The
    // future permission system (delegated channel-creation rights) only
    // needs to change these two methods' implementation; nothing in
    // ChannelHandler or the protocol itself should need to change.
    // See docs/rooms-spec.md, "Future Permission Hook".
    bool canCreateChannel(const std::string& guild_id, const std::string& user_id) const;
    bool canDeleteChannel(const std::string& guild_id, const std::string& user_id) const;

  private:
    std::unordered_map<std::string, Guild> guilds_;
    std::unordered_map<std::string, Channel> channels_;

    // Same reasoning as ChatHandler::message_counter_: the server is
    // single-threaded today (one poll loop in Server::start()), so a plain
    // uint64_t would be safe right now. Using std::atomic anyway removes a
    // landmine for whenever that stops being true, at effectively zero cost
    // in the single-threaded case.
    std::atomic<std::uint64_t> next_guild_id_{1};
    std::atomic<std::uint64_t> next_channel_id_{1};
};

} // namespace guild

#endif // CIG_NEXUS_GUILD_GUILD_MANAGER_HPP
