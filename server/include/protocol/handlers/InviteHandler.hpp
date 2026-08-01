#ifndef CIG_NEXUS_PROTOCOL_HANDLERS_INVITE_HANDLER_HPP
#define CIG_NEXUS_PROTOCOL_HANDLERS_INVITE_HANDLER_HPP

#include "protocol/Message.hpp"

#include <vector>

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

// Guild invites: CREATE_INVITE, JOIN_VIA_INVITE, LIST_INVITES, REVOKE_INVITE.
// See docs/social-presence-design.md §1.
class InviteHandler {
  public:
    void setSessionManager(session::SessionManager* session_manager);
    void setGuildManager(guild::GuildManager* guild_manager);
    void setInternalApiClient(http::InternalApiClient* internal_api_client);

    Message handleCreateInvite(const Message& message, int fd) const;
    Message handleListInvites(const Message& message, int fd) const;
    Message handleRevokeInvite(const Message& message, int fd) const;

    // Returns more than one Message when redemption diverts to a join
    // request against an `application`-visibility guild (§1.8's
    // ARBITRATION): JOIN_REQUESTED to the requester, plus JOIN_REQUEST_RECEIVED
    // to every currently-connected officer-or-above member. Every other
    // outcome (direct membership, or any error) is a single element.
    std::vector<Message> handleJoinViaInvite(const Message& message, int fd) const;

  private:
    static Message makeError(const std::string& code, const std::string& msg);
    const session::Session* requireIdentified(int fd) const;

    // fds of every currently-connected officer-or-above member of guild_id
    // — the JOIN_REQUEST_RECEIVED audience (§1.9).
    std::vector<int> getOfficerFds(const std::string& guild_id) const;

    session::SessionManager* session_manager_ = nullptr;
    guild::GuildManager* guild_manager_ = nullptr;
    http::InternalApiClient* internal_api_client_ = nullptr;
};

} // namespace protocol

#endif // CIG_NEXUS_PROTOCOL_HANDLERS_INVITE_HANDLER_HPP
