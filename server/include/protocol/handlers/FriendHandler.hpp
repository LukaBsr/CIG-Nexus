#ifndef CIG_NEXUS_PROTOCOL_HANDLERS_FRIEND_HANDLER_HPP
#define CIG_NEXUS_PROTOCOL_HANDLERS_FRIEND_HANDLER_HPP

#include "http/InternalApiClient.hpp"
#include "protocol/Message.hpp"

#include <vector>

namespace session {
class SessionManager;
struct Session;
} // namespace session

namespace http {
class InternalApiClient;
}

namespace protocol {

// Friend system: SEND_FRIEND_REQUEST, ACCEPT/REJECT/CANCEL_FRIEND_REQUEST,
// REMOVE_FRIEND, LIST_FRIENDS, LIST_FRIEND_REQUESTS, FETCH_FRIEND_CODE,
// REGENERATE_FRIEND_CODE, ADD_FRIEND_BY_CODE. See
// docs/social/friends-dms-design.md §1. No GuildManager dependency —
// friendship isn't guild-scoped.
class FriendHandler {
  public:
    void setSessionManager(session::SessionManager* session_manager);
    void setInternalApiClient(http::InternalApiClient* internal_api_client);

    // Both can produce the FRIEND_REQUEST_SENT/RECEIVED pair or, on the
    // reverse-pending auto-accept path (§1.4 step 5), the FRIEND_ADDED
    // pair instead — same outcome-to-response mapping either way (§1.3:
    // "one shared internal function with two callers").
    std::vector<Message> handleSendFriendRequest(const Message& message, int fd) const;
    std::vector<Message> handleAddFriendByCode(const Message& message, int fd) const;

    std::vector<Message> handleAcceptFriendRequest(const Message& message, int fd) const;
    std::vector<Message> handleRejectFriendRequest(const Message& message, int fd) const;
    std::vector<Message> handleCancelFriendRequest(const Message& message, int fd) const;
    std::vector<Message> handleRemoveFriend(const Message& message, int fd) const;

    Message handleListFriends(const Message& message, int fd) const;
    Message handleListFriendRequests(const Message& message, int fd) const;
    Message handleFetchFriendCode(const Message& message, int fd) const;
    Message handleRegenerateFriendCode(const Message& message, int fd) const;

  private:
    static Message makeError(const std::string& code, const std::string& msg);
    const session::Session* requireIdentified(int fd) const;

    // Shared by handleSendFriendRequest/handleAddFriendByCode — maps a
    // SendFriendRequestResult to the appropriate error, or to the
    // FRIEND_REQUEST_SENT+RECEIVED / FRIEND_ADDED+FRIEND_ADDED response
    // pair, targeted at the other party via getFdsForUser.
    std::vector<Message> buildSendFriendRequestResponses(const http::SendFriendRequestResult& result,
                                                          const session::Session* session) const;

    // docs/social/friends-dms-design.md §3.3: keeps the in-memory
    // friend_ids cache (every connection of both accounts) in sync with a
    // friendship's creation/removal, the same live-update discipline
    // BlockHandler already applies to blocked_user_ids — mirrors
    // APPROVE_JOIN_REQUEST's "loop getFdsForUser, mutate each" shape,
    // just applied to both parties instead of one.
    void updateFriendIdsForBothParties(const std::string& user_id_a, const std::string& user_id_b, bool added) const;

    session::SessionManager* session_manager_ = nullptr;
    http::InternalApiClient* internal_api_client_ = nullptr;
};

} // namespace protocol

#endif // CIG_NEXUS_PROTOCOL_HANDLERS_FRIEND_HANDLER_HPP
