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

namespace protocol {

// Guild lifecycle: CREATE_GUILD, LIST_GUILDS, JOIN_GUILD, LEAVE_GUILD,
// DELETE_GUILD, LIST_MEMBERS, SET_MEMBER_ROLE. See docs/guilds/design.md for
// the full protocol shapes, docs/auth/discord-design.md §8.1 for the
// write-through-cache/internal-API pattern every mutation below follows
// (call InternalApiClient first, only touch GuildManager/SessionManager if
// that call succeeds), and docs/social-presence-design.md §2 for the
// roster/rank-based-role protocol messages.
class GuildHandler {
  public:
    void setSessionManager(session::SessionManager* session_manager);
    void setGuildManager(guild::GuildManager* guild_manager);
    void setInternalApiClient(http::InternalApiClient* internal_api_client);

    Message handleCreateGuild(const Message& message, int fd) const;
    Message handleListGuilds(const Message& message, int fd) const;
    Message handleJoinGuild(const Message& message, int fd) const;
    Message handleLeaveGuild(const Message& message, int fd) const;
    Message handleDeleteGuild(const Message& message, int fd) const;
    Message handleListMembers(const Message& message, int fd) const;
    Message handleSetMemberRole(const Message& message, int fd) const;
    Message handleSetGuildVisibility(const Message& message, int fd) const;

  private:
    static Message makeError(const std::string& code, const std::string& msg);

    // Returns nullptr (caller returns NOT_IDENTIFIED) if fd has no session
    // or hasn't completed IDENTIFY yet.
    const session::Session* requireIdentified(int fd) const;

    session::SessionManager* session_manager_ = nullptr;
    guild::GuildManager* guild_manager_ = nullptr;
    http::InternalApiClient* internal_api_client_ = nullptr;
};

} // namespace protocol

#endif // CIG_NEXUS_PROTOCOL_HANDLERS_GUILD_HANDLER_HPP
