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

namespace protocol {

// Guild lifecycle: CREATE_GUILD, LIST_GUILDS, JOIN_GUILD, LEAVE_GUILD,
// DELETE_GUILD. See docs/rooms-spec.md for the full protocol shapes.
class GuildHandler {
  public:
    void setSessionManager(session::SessionManager* session_manager);
    void setGuildManager(guild::GuildManager* guild_manager);

    Message handleCreateGuild(const Message& message, int fd) const;
    Message handleListGuilds(const Message& message, int fd) const;
    Message handleJoinGuild(const Message& message, int fd) const;
    Message handleLeaveGuild(const Message& message, int fd) const;
    Message handleDeleteGuild(const Message& message, int fd) const;

  private:
    static Message makeError(const std::string& code, const std::string& msg);

    // Returns nullptr (caller returns NOT_IDENTIFIED) if fd has no session
    // or hasn't completed IDENTIFY yet.
    const session::Session* requireIdentified(int fd) const;

    session::SessionManager* session_manager_ = nullptr;
    guild::GuildManager* guild_manager_ = nullptr;
};

} // namespace protocol

#endif // CIG_NEXUS_PROTOCOL_HANDLERS_GUILD_HANDLER_HPP
