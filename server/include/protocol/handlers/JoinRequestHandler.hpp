#ifndef CIG_NEXUS_PROTOCOL_HANDLERS_JOIN_REQUEST_HANDLER_HPP
#define CIG_NEXUS_PROTOCOL_HANDLERS_JOIN_REQUEST_HANDLER_HPP

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

// `application`-visibility guild join requests: REQUEST_JOIN,
// LIST_JOIN_REQUESTS, APPROVE_JOIN_REQUEST, REJECT_JOIN_REQUEST. See
// docs/guilds/social-presence-design.md §1.9.
class JoinRequestHandler {
  public:
    void setSessionManager(session::SessionManager* session_manager);
    void setGuildManager(guild::GuildManager* guild_manager);
    void setInternalApiClient(http::InternalApiClient* internal_api_client);

    // JOIN_REQUESTED to the requester, plus JOIN_REQUEST_RECEIVED to every
    // currently-connected officer-or-above member (§1.9) — the first
    // handler in this codebase needing that shape, alongside
    // InviteHandler::handleJoinViaInvite's identical diversion case.
    std::vector<Message> handleRequestJoin(const Message& message, int fd) const;
    Message handleListJoinRequests(const Message& message, int fd) const;
    // JOIN_REQUEST_APPROVED to the approver, plus GUILD_JOINED to every
    // connection currently identified as the approved user (getFdsForUser
    // — they're not yet reflected as a member in anyone's Session state).
    std::vector<Message> handleApproveJoinRequest(const Message& message, int fd) const;
    // JOIN_REQUEST_REJECTED to the rejecter, plus the same to the rejected
    // user's connection(s), if any are currently open.
    std::vector<Message> handleRejectJoinRequest(const Message& message, int fd) const;

  private:
    static Message makeError(const std::string& code, const std::string& msg);
    const session::Session* requireIdentified(int fd) const;
    std::vector<int> getOfficerFds(const std::string& guild_id) const;

    session::SessionManager* session_manager_ = nullptr;
    guild::GuildManager* guild_manager_ = nullptr;
    http::InternalApiClient* internal_api_client_ = nullptr;
};

} // namespace protocol

#endif // CIG_NEXUS_PROTOCOL_HANDLERS_JOIN_REQUEST_HANDLER_HPP
