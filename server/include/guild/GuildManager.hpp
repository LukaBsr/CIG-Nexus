#ifndef CIG_NEXUS_GUILD_GUILD_MANAGER_HPP
#define CIG_NEXUS_GUILD_GUILD_MANAGER_HPP

#include <string>
#include <unordered_map>
#include <vector>

#include "guild/Channel.hpp"
#include "guild/Guild.hpp"

namespace guild {

// GuildManager is a write-through cache over the Postgres-backed catalog
// (design doc §8.1): the fast in-memory structure handlers read from for
// every request, but no longer the source of truth. Ids are never
// generated here — upsertGuild/upsertChannel take the id the internal API
// (Next.js/Postgres) already assigned, either from a mutation response or
// from the full-catalog fetch at server startup.
class GuildManager {
  public:
    Guild& upsertGuild(const std::string& id, const std::string& name, const std::string& owner_id);

    // Deletes the guild and cascades to delete all of its channels. Callers
    // needing to clean up per-connection membership/active-channel state
    // (session::SessionManager::purgeGuildMembership) must capture
    // listChannels(guild_id) *before* calling this, since the channel list
    // won't be queryable afterward.
    bool deleteGuild(const std::string& guild_id);

    Channel& upsertChannel(const std::string& id, const std::string& guild_id,
                           const std::string& name, ChannelType type);
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
    // See docs/guilds/design.md, "Future Permission Hook".
    bool canCreateChannel(const std::string& guild_id, const std::string& user_id) const;
    bool canDeleteChannel(const std::string& guild_id, const std::string& user_id) const;

  private:
    std::unordered_map<std::string, Guild> guilds_;
    std::unordered_map<std::string, Channel> channels_;
};

} // namespace guild

#endif // CIG_NEXUS_GUILD_GUILD_MANAGER_HPP
