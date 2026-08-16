#include "protocol/handlers/BlockHandler.hpp"

#include "http/InternalApiClient.hpp"
#include "protocol/MessageBuilders.hpp"
#include "session/SessionManager.hpp"

namespace protocol {

void BlockHandler::setSessionManager(session::SessionManager* session_manager) {
    session_manager_ = session_manager;
}

void BlockHandler::setInternalApiClient(http::InternalApiClient* internal_api_client) {
    internal_api_client_ = internal_api_client;
}

Message BlockHandler::makeError(const std::string& code, const std::string& msg) {
    Message response;
    response.type = "ERROR";
    response.payload = make_error(code, msg);
    return response;
}

const session::Session* BlockHandler::requireIdentified(int fd) const {
    if (!session_manager_) {
        return nullptr;
    }

    const session::Session* session = session_manager_->getSession(fd);
    if (!session || session->username.empty()) {
        return nullptr;
    }

    return session;
}

Message BlockHandler::handleBlockUser(const Message& message, int fd) const {
    if (message.type != "BLOCK_USER") {
        return makeError("PROTOCOL_VIOLATION", "Expected BLOCK_USER message");
    }
    if (!message.payload.is_object()) {
        return makeError("MALFORMED_MESSAGE", "BLOCK_USER payload must be an object");
    }

    const session::Session* session = requireIdentified(fd);
    if (!session) {
        return makeError("NOT_IDENTIFIED", "Client must IDENTIFY before blocking a user");
    }

    if (!message.payload.contains("user_id") || !message.payload["user_id"].is_string()) {
        return makeError("MALFORMED_MESSAGE", "BLOCK_USER missing required field: user_id");
    }
    if (!internal_api_client_) {
        return makeError("INTERNAL_ERROR", "Block context unavailable");
    }

    const std::string target_user_id = message.payload["user_id"].get<std::string>();
    if (target_user_id == session->user_id) {
        return makeError("PROTOCOL_VIOLATION", "Cannot block yourself");
    }

    if (!internal_api_client_->blockUser(session->user_id, target_user_id)) {
        return makeError("USER_NOT_FOUND", "No such user");
    }

    // docs/social/friends-dms-design.md §2.8: update every connection of
    // the caller's own account, mirroring how APPROVE_JOIN_REQUEST updates
    // every connection of the *target* user for guild_ids — here the
    // account being updated is the caller's own, not the other party's.
    for (int caller_fd : session_manager_->getFdsForUser(session->user_id)) {
        session_manager_->addBlockedUser(caller_fd, target_user_id);
    }

    Message response;
    response.type = "USER_BLOCKED";
    response.payload = nlohmann::json{{"type", "USER_BLOCKED"}, {"user_id", target_user_id}};
    return response;
}

Message BlockHandler::handleUnblockUser(const Message& message, int fd) const {
    if (message.type != "UNBLOCK_USER") {
        return makeError("PROTOCOL_VIOLATION", "Expected UNBLOCK_USER message");
    }
    if (!message.payload.is_object()) {
        return makeError("MALFORMED_MESSAGE", "UNBLOCK_USER payload must be an object");
    }

    const session::Session* session = requireIdentified(fd);
    if (!session) {
        return makeError("NOT_IDENTIFIED", "Client must IDENTIFY before unblocking a user");
    }

    if (!message.payload.contains("user_id") || !message.payload["user_id"].is_string()) {
        return makeError("MALFORMED_MESSAGE", "UNBLOCK_USER missing required field: user_id");
    }
    if (!internal_api_client_) {
        return makeError("INTERNAL_ERROR", "Block context unavailable");
    }

    const std::string target_user_id = message.payload["user_id"].get<std::string>();
    if (!internal_api_client_->unblockUser(session->user_id, target_user_id)) {
        return makeError("USER_NOT_FOUND", "No such block");
    }

    for (int caller_fd : session_manager_->getFdsForUser(session->user_id)) {
        session_manager_->removeBlockedUser(caller_fd, target_user_id);
    }

    Message response;
    response.type = "USER_UNBLOCKED";
    response.payload = nlohmann::json{{"type", "USER_UNBLOCKED"}, {"user_id", target_user_id}};
    return response;
}

Message BlockHandler::handleListBlocks(const Message& message, int fd) const {
    if (message.type != "LIST_BLOCKS") {
        return makeError("PROTOCOL_VIOLATION", "Expected LIST_BLOCKS message");
    }
    if (!message.payload.is_object()) {
        return makeError("MALFORMED_MESSAGE", "LIST_BLOCKS payload must be an object");
    }

    const session::Session* session = requireIdentified(fd);
    if (!session) {
        return makeError("NOT_IDENTIFIED", "Client must IDENTIFY before listing blocks");
    }
    if (!internal_api_client_) {
        return makeError("INTERNAL_ERROR", "Block context unavailable");
    }

    const std::optional<std::vector<http::WireBlock>> blocks =
        internal_api_client_->fetchBlocks(session->user_id);
    if (!blocks) {
        return makeError("INTERNAL_ERROR", "Failed to fetch blocks");
    }

    nlohmann::json blocked_json = nlohmann::json::array();
    for (const auto& b : *blocks) {
        blocked_json.push_back(
            nlohmann::json{{"user_id", b.user_id},
                           {"username", b.username},
                           {"blocked_at", b.blocked_at},
                           {"display_name", make_optional_string(b.display_name)},
                           {"avatar_url", make_optional_string(b.avatar_url)}});
    }

    Message response;
    response.type = "BLOCK_LIST";
    response.payload = nlohmann::json{{"type", "BLOCK_LIST"}, {"blocked", blocked_json}};
    return response;
}

} // namespace protocol
