#ifndef CIG_NEXUS_PROTOCOL_HANDLERS_DM_READ_STATE_HANDLER_HPP
#define CIG_NEXUS_PROTOCOL_HANDLERS_DM_READ_STATE_HANDLER_HPP

#include "protocol/Message.hpp"

namespace session {
class SessionManager;
} // namespace session

namespace http {
class InternalApiClient;
}

namespace util {
class RateLimiter;
}

namespace protocol {

// DM unread counts: MARK_DM_READ, FETCH_UNREAD_COUNTS (see
// docs/social/friends-dms-design.md §3.7). Kept separate from DMHandler
// (which owns DM_SEND/FETCH_HISTORY's DM branch/LIST_DM_CONVERSATIONS) —
// a distinct enough concern (read-state tracking, not message delivery)
// to earn its own file rather than growing DMHandler past the point of
// being one coherent job.
//
// Neither handler here checks Session::session_context_ready the way
// DM_SEND does: that gate exists specifically because canSendDm() reads
// friend_ids/blocked_user_ids, and neither handler here reads either —
// marking a conversation read, or asking how much of it is unread, is
// independent of current send permission (you can still read a backlog
// from someone you can no longer message, or who has blocked you).
class DMReadStateHandler {
  public:
    void setSessionManager(session::SessionManager* session_manager);
    void setInternalApiClient(http::InternalApiClient* internal_api_client);
    void setRateLimiter(util::RateLimiter* rate_limiter);

    Message handleMarkDmRead(const Message& message, int fd) const;
    Message handleFetchUnreadCounts(const Message& message, int fd) const;

  private:
    session::SessionManager* session_manager_ = nullptr;
    http::InternalApiClient* internal_api_client_ = nullptr;
    util::RateLimiter* rate_limiter_ = nullptr;
};

} // namespace protocol

#endif // CIG_NEXUS_PROTOCOL_HANDLERS_DM_READ_STATE_HANDLER_HPP
