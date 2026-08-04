#ifndef CIG_NEXUS_PROTOCOL_HANDLERS_BLOCK_HANDLER_HPP
#define CIG_NEXUS_PROTOCOL_HANDLERS_BLOCK_HANDLER_HPP

#include "protocol/Message.hpp"

namespace session {
class SessionManager;
struct Session;
} // namespace session

namespace http {
class InternalApiClient;
}

namespace protocol {

// Blocking: BLOCK_USER, UNBLOCK_USER, LIST_BLOCKS. See
// docs/social/friends-dms-design.md §2. All three are Scope::DIRECT
// only — silent to the target, per §2.6/§2.7's recommendation.
class BlockHandler {
  public:
    void setSessionManager(session::SessionManager* session_manager);
    void setInternalApiClient(http::InternalApiClient* internal_api_client);

    Message handleBlockUser(const Message& message, int fd) const;
    Message handleUnblockUser(const Message& message, int fd) const;
    Message handleListBlocks(const Message& message, int fd) const;

  private:
    static Message makeError(const std::string& code, const std::string& msg);
    const session::Session* requireIdentified(int fd) const;

    session::SessionManager* session_manager_ = nullptr;
    http::InternalApiClient* internal_api_client_ = nullptr;
};

} // namespace protocol

#endif // CIG_NEXUS_PROTOCOL_HANDLERS_BLOCK_HANDLER_HPP
