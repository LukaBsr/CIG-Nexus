#ifndef CIG_NEXUS_PROTOCOL_HANDLERS_GUILD_HANDLER_HPP
#define CIG_NEXUS_PROTOCOL_HANDLERS_GUILD_HANDLER_HPP

#include "protocol/Message.hpp"

namespace session {
class SessionManager;
struct Session;
} // namespace session

namespace guild {
class GuildManager;
}

namespace http {
class InternalApiClient;
}

namespace util {
class RateLimiter;
}

namespace protocol {

// Guild lifecycle: CREATE_GUILD, LIST_GUILDS, JOIN_GUILD, LEAVE_GUILD,
// DELETE_GUILD, LIST_MEMBERS, SET_MEMBER_ROLE. See docs/guilds/design.md for
// the full protocol shapes, docs/auth/discord-design.md §8.1 for the
// write-through-cache/internal-API pattern every mutation below follows
// (call InternalApiClient first, only touch GuildManager/SessionManager if
// that call succeeds), and docs/guilds/social-presence-design.md §2 for the
// roster/rank-based-role protocol messages.
class GuildHandler {
  public:
    void setSessionManager(session::SessionManager* session_manager);
    void setGuildManager(guild::GuildManager* guild_manager);
    void setInternalApiClient(http::InternalApiClient* internal_api_client);
    void setRateLimiter(util::RateLimiter* rate_limiter);

    Message handleCreateGuild(const Message& message, int fd) const;
    Message handleListGuilds(const Message& message, int fd) const;
    Message handleJoinGuild(const Message& message, int fd) const;
    Message handleLeaveGuild(const Message& message, int fd) const;
    Message handleDeleteGuild(const Message& message, int fd) const;
    Message handleListMembers(const Message& message, int fd) const;
    Message handleSetMemberRole(const Message& message, int fd) const;
    Message handleSetGuildVisibility(const Message& message, int fd) const;

  private:
    session::SessionManager* session_manager_ = nullptr;
    guild::GuildManager* guild_manager_ = nullptr;
    http::InternalApiClient* internal_api_client_ = nullptr;
    util::RateLimiter* rate_limiter_ = nullptr;
};

} // namespace protocol

#endif // CIG_NEXUS_PROTOCOL_HANDLERS_GUILD_HANDLER_HPP
