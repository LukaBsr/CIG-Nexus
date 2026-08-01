#ifndef CIG_NEXUS_GUILD_GUILD_MANAGER_HPP
#define CIG_NEXUS_GUILD_GUILD_MANAGER_HPP

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "guild/Channel.hpp"
#include "guild/Guild.hpp"
#include "guild/RoleRank.hpp"

namespace guild {

// GuildManager is a write-through cache over the Postgres-backed catalog
// (design doc §8.1): the fast in-memory structure handlers read from for
// every request, but no longer the source of truth. Ids are never
// generated here — upsertGuild/upsertChannel take the id the internal API
// (Next.js/Postgres) already assigned, either from a mutation response or
// from the full-catalog fetch at server startup.
class GuildManager {
  public:
    Guild& upsertGuild(const std::string& id, const std::string& name, const std::string& owner_id,
                       GuildVisibility visibility = GuildVisibility::OPEN);

    // docs/social-presence-design.md §1.10 (SET_GUILD_VISIBILITY). No-op if
    // the guild doesn't exist (callers already check hasGuild() first).
    void setGuildVisibility(const std::string& guild_id, GuildVisibility visibility);

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

    // docs/social-presence-design.md §2.2/§2.3: a minimal role_rank cache,
    // deliberately NOT the full roster (username, role_label) LIST_MEMBERS
    // needs — that stays a live internal-API read per §2.3's "fetch live"
    // recommendation, a cold UI-driven path. This exists only to keep the
    // predicates below synchronous and in-memory, the same way isOwner
    // already is — every one of them gates a hot, per-message protocol
    // action, exactly the class of thing the write-through cache exists for.
    // Populated at catalog hydration (Catalog::memberships now carries
    // role_rank) and kept current by CREATE_GUILD/JOIN_GUILD/LEAVE_GUILD/
    // SET_MEMBER_ROLE, mirroring how owner_id is already kept current.
    void setMemberRank(const std::string& guild_id, const std::string& user_id, int role_rank);
    void removeMember(const std::string& guild_id, const std::string& user_id);
    std::optional<int> getMemberRank(const std::string& guild_id, const std::string& user_id) const;

    // docs/social-presence-design.md §3.4/§1.10: the Session.guild_ids
    // hydration-on-IDENTIFY fix. user_id -> every guild_id they belong to,
    // kept current by setMemberRank/removeMember/deleteGuild above (the
    // same mutation points that already maintain member_ranks_) — no
    // separate write path to keep in sync. IdentifyHandler calls this once,
    // on successful IDENTIFY, to populate the freshly-created Session's
    // guild_ids via a pure in-memory lookup instead of leaving it empty
    // until the client re-issues JOIN_GUILD for every guild it's already in.
    std::vector<std::string> getGuildIdsForUser(const std::string& user_id) const;

    // Authorization choke points: handlers must call these named predicates
    // rather than comparing a rank/owner_id inline. A future tier only ever
    // means adding a constant to RoleRank.hpp; nothing in ChannelHandler,
    // GuildHandler, or the protocol itself should need to change.
    // See docs/guilds/design.md, "Future Permission Hook", and
    // docs/social-presence-design.md §2.2's predicate table.
    bool isOfficerOrAbove(const std::string& guild_id, const std::string& user_id) const;
    bool canCreateChannel(const std::string& guild_id, const std::string& user_id) const;
    bool canDeleteChannel(const std::string& guild_id, const std::string& user_id) const;
    bool canSetMemberRole(const std::string& guild_id, const std::string& user_id) const;
    bool canCreateInvite(const std::string& guild_id, const std::string& user_id) const;
    bool canApproveJoinRequest(const std::string& guild_id, const std::string& user_id) const;
    bool canSetGuildVisibility(const std::string& guild_id, const std::string& user_id) const;

  private:
    bool hasRankAtLeast(const std::string& guild_id, const std::string& user_id,
                        int min_rank) const;

    std::unordered_map<std::string, Guild> guilds_;
    std::unordered_map<std::string, Channel> channels_;
    // guild_id -> user_id -> role_rank.
    std::unordered_map<std::string, std::unordered_map<std::string, int>> member_ranks_;
    // user_id -> every guild_id they belong to (§3.4/§1.10's index).
    std::unordered_map<std::string, std::vector<std::string>> guild_ids_by_user_;
};

} // namespace guild

#endif // CIG_NEXUS_GUILD_GUILD_MANAGER_HPP
