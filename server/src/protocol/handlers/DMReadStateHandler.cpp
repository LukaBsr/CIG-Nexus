#include "protocol/handlers/DMReadStateHandler.hpp"

#include "http/InternalApiClient.hpp"
#include "protocol/handlers/HandlerSupport.hpp"
#include "session/SessionManager.hpp"
#include "util/RateLimiter.hpp"

#include <chrono>

namespace protocol {

void DMReadStateHandler::setSessionManager(session::SessionManager* session_manager) {
    session_manager_ = session_manager;
}

void DMReadStateHandler::setInternalApiClient(http::InternalApiClient* internal_api_client) {
    internal_api_client_ = internal_api_client;
}

void DMReadStateHandler::setRateLimiter(util::RateLimiter* rate_limiter) {
    rate_limiter_ = rate_limiter;
}

// docs/social/friends-dms-design.md §3.7. On success, pushes DM_READ to
// every connection identified as the caller — self-inclusive, same
// pattern DM_SEND's own broadcast already uses for the sender's other
// open tabs — and ONLY to the caller. The peer is never a recipient: this
// is read-state sync across one person's own sessions/devices, not a
// read receipt the other party would see.
Message DMReadStateHandler::handleMarkDmRead(const Message& message, int fd) const {
    if (message.type != "MARK_DM_READ") {
        return makeError("PROTOCOL_VIOLATION", "Expected MARK_DM_READ message");
    }
    if (!message.payload.is_object()) {
        return makeError("MALFORMED_MESSAGE", "MARK_DM_READ payload must be an object");
    }

    const session::Session* session = requireIdentified(session_manager_, fd);
    if (!session) {
        return makeError("NOT_IDENTIFIED", "Client must IDENTIFY before marking a DM read");
    }

    if (!message.payload.contains("user_id") || !message.payload["user_id"].is_string()) {
        return makeError("MALFORMED_MESSAGE", "MARK_DM_READ missing required field: user_id");
    }
    if (!message.payload.contains("up_to_message_id") ||
        !message.payload["up_to_message_id"].is_number_integer()) {
        return makeError("MALFORMED_MESSAGE",
                         "MARK_DM_READ missing required integer field: up_to_message_id");
    }

    const std::string peer_id = message.payload["user_id"].get<std::string>();
    const int up_to_message_id = message.payload["up_to_message_id"].get<int>();

    // shared/protocol/README.md's Rate Limits table: 10 / 10 seconds — a
    // backstop against a misbehaving client; the web client is expected to
    // debounce its own sends well below this.
    if (rate_limiter_ &&
        !rate_limiter_->allow("mark-dm-read", session->user_id, std::chrono::seconds(10), 10)) {
        return makeError("RATE_LIMITED", "Too many read-state updates, please slow down");
    }

    if (!internal_api_client_) {
        return makeError("INTERNAL_ERROR", "DM read-state unavailable");
    }

    // !ok covers both "no conversation at all between these two users" and
    // "up_to_message_id doesn't belong to the resolved conversation" —
    // deliberately indistinguishable, same posture DM_NOT_PERMITTED
    // already uses for DM_SEND, and the same single-error-code collapse
    // AcceptFriendRequestResult's own !ok already gets in FriendHandler.
    const http::MarkDmReadResult result =
        internal_api_client_->markDmRead(session->user_id, peer_id, up_to_message_id);
    if (!result.ok) {
        return makeError("MALFORMED_MESSAGE",
                         "No such DM conversation, or up_to_message_id does not belong to it");
    }

    Message response;
    response.type = "DM_READ";
    response.scope = Scope::TARGETED;
    response.target_fds = session_manager_->getFdsForUser(session->user_id);
    response.payload = nlohmann::json{{"type", "DM_READ"},
                                      {"user_id", peer_id},
                                      {"up_to_message_id", result.marker_seq},
                                      {"unread", result.unread}};
    return response;
}

Message DMReadStateHandler::handleFetchUnreadCounts(const Message& message, int fd) const {
    if (message.type != "FETCH_UNREAD_COUNTS") {
        return makeError("PROTOCOL_VIOLATION", "Expected FETCH_UNREAD_COUNTS message");
    }

    const session::Session* session = requireIdentified(session_manager_, fd);
    if (!session) {
        return makeError("NOT_IDENTIFIED", "Client must IDENTIFY before fetching unread counts");
    }

    if (!internal_api_client_) {
        return makeError("INTERNAL_ERROR", "DM read-state unavailable");
    }

    const std::optional<std::vector<http::WireUnreadCount>> counts =
        internal_api_client_->fetchUnreadCounts(session->user_id);
    if (!counts) {
        return makeError("INTERNAL_ERROR", "Failed to fetch unread counts");
    }

    // Object keyed by peer user_id, not an array — the client looks up one
    // peer's count at a time, and an absent key already means "no unread"
    // (fetchUnreadCounts only returns entries with count > 0), so there's
    // no reason to make it scan an array first.
    nlohmann::json counts_json = nlohmann::json::object();
    for (const auto& c : *counts) {
        counts_json[c.peer_id] = c.count;
    }

    Message response;
    response.type = "UNREAD_COUNTS";
    response.payload = nlohmann::json{{"type", "UNREAD_COUNTS"}, {"counts", counts_json}};
    return response;
}

} // namespace protocol
