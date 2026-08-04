#include "protocol/handlers/FriendHandler.hpp"

#include "http/InternalApiClient.hpp"
#include "protocol/MessageBuilders.hpp"
#include "session/SessionManager.hpp"

namespace protocol {

void FriendHandler::setSessionManager(session::SessionManager* session_manager) {
    session_manager_ = session_manager;
}

void FriendHandler::setInternalApiClient(http::InternalApiClient* internal_api_client) {
    internal_api_client_ = internal_api_client;
}

Message FriendHandler::makeError(const std::string& code, const std::string& msg) {
    Message response;
    response.type = "ERROR";
    response.payload = make_error(code, msg);
    return response;
}

const session::Session* FriendHandler::requireIdentified(int fd) const {
    if (!session_manager_) {
        return nullptr;
    }

    const session::Session* session = session_manager_->getSession(fd);
    if (!session || session->username.empty()) {
        return nullptr;
    }

    return session;
}

void FriendHandler::updateFriendIdsForBothParties(const std::string& user_id_a, const std::string& user_id_b,
                                                  bool added) const {
    for (int fd_a : session_manager_->getFdsForUser(user_id_a)) {
        if (added) {
            session_manager_->addFriend(fd_a, user_id_b);
        } else {
            session_manager_->removeFriend(fd_a, user_id_b);
        }
    }
    for (int fd_b : session_manager_->getFdsForUser(user_id_b)) {
        if (added) {
            session_manager_->addFriend(fd_b, user_id_a);
        } else {
            session_manager_->removeFriend(fd_b, user_id_a);
        }
    }
}

std::vector<Message>
FriendHandler::buildSendFriendRequestResponses(const http::SendFriendRequestResult& result,
                                               const session::Session* session) const {
    switch (result.outcome) {
    case http::SendFriendRequestOutcome::USER_NOT_FOUND:
        return {makeError("USER_NOT_FOUND", "No such user")};
    case http::SendFriendRequestOutcome::SELF:
        return {makeError("PROTOCOL_VIOLATION", "Cannot send a friend request to yourself")};
    case http::SendFriendRequestOutcome::ALREADY_FRIENDS:
        return {makeError("PROTOCOL_VIOLATION", "Already friends")};
    case http::SendFriendRequestOutcome::CODE_NOT_FOUND:
        return {makeError("FRIEND_CODE_NOT_FOUND", "No such friend code")};
    case http::SendFriendRequestOutcome::FAILED:
    default:
        if (result.outcome != http::SendFriendRequestOutcome::REQUEST_CREATED &&
            result.outcome != http::SendFriendRequestOutcome::FRIENDS_ADDED) {
            return {makeError("INTERNAL_ERROR", "Failed to send friend request")};
        }
        break;
    }

    // docs/social/friends-dms-design.md §1.5: a single client action
    // triggering more than one outgoing message, same "dispatcher returns
    // a list per incoming message" pattern JOIN_VIA_INVITE/REQUEST_JOIN
    // already use.
    const bool is_auto_accept = result.outcome == http::SendFriendRequestOutcome::FRIENDS_ADDED;
    const std::string to_caller_type = is_auto_accept ? "FRIEND_ADDED" : "FRIEND_REQUEST_SENT";
    const std::string to_other_type = is_auto_accept ? "FRIEND_ADDED" : "FRIEND_REQUEST_RECEIVED";

    if (is_auto_accept) {
        updateFriendIdsForBothParties(session->user_id, result.user_id, /*added=*/true);
    }

    Message to_caller;
    to_caller.type = to_caller_type;
    to_caller.payload = nlohmann::json{{"type", to_caller_type}, {"user_id", result.user_id}};

    std::vector<Message> responses{to_caller};
    const std::vector<int> target_fds = session_manager_->getFdsForUser(result.user_id);
    if (!target_fds.empty()) {
        Message to_other;
        to_other.type = to_other_type;
        to_other.scope = Scope::TARGETED;
        to_other.target_fds = target_fds;
        to_other.payload = nlohmann::json{{"type", to_other_type}, {"user_id", session->user_id}};
        responses.push_back(to_other);
    }
    return responses;
}

std::vector<Message> FriendHandler::handleSendFriendRequest(const Message& message, int fd) const {
    if (message.type != "SEND_FRIEND_REQUEST") {
        return {makeError("PROTOCOL_VIOLATION", "Expected SEND_FRIEND_REQUEST message")};
    }
    if (!message.payload.is_object()) {
        return {makeError("MALFORMED_MESSAGE", "SEND_FRIEND_REQUEST payload must be an object")};
    }

    const session::Session* session = requireIdentified(fd);
    if (!session) {
        return {makeError("NOT_IDENTIFIED", "Client must IDENTIFY before sending a friend request")};
    }

    if (!message.payload.contains("user_id") || !message.payload["user_id"].is_string()) {
        return {makeError("MALFORMED_MESSAGE", "SEND_FRIEND_REQUEST missing required field: user_id")};
    }
    if (!internal_api_client_) {
        return {makeError("INTERNAL_ERROR", "Friend context unavailable")};
    }

    const std::string target_user_id = message.payload["user_id"].get<std::string>();
    const http::SendFriendRequestResult result =
        internal_api_client_->sendFriendRequest(session->user_id, target_user_id);
    return buildSendFriendRequestResponses(result, session);
}

std::vector<Message> FriendHandler::handleAddFriendByCode(const Message& message, int fd) const {
    if (message.type != "ADD_FRIEND_BY_CODE") {
        return {makeError("PROTOCOL_VIOLATION", "Expected ADD_FRIEND_BY_CODE message")};
    }
    if (!message.payload.is_object()) {
        return {makeError("MALFORMED_MESSAGE", "ADD_FRIEND_BY_CODE payload must be an object")};
    }

    const session::Session* session = requireIdentified(fd);
    if (!session) {
        return {makeError("NOT_IDENTIFIED", "Client must IDENTIFY before adding a friend by code")};
    }

    if (!message.payload.contains("code") || !message.payload["code"].is_string()) {
        return {makeError("MALFORMED_MESSAGE", "ADD_FRIEND_BY_CODE missing required field: code")};
    }
    if (!internal_api_client_) {
        return {makeError("INTERNAL_ERROR", "Friend context unavailable")};
    }

    const std::string code = message.payload["code"].get<std::string>();
    const http::SendFriendRequestResult result = internal_api_client_->addFriendByCode(session->user_id, code);
    return buildSendFriendRequestResponses(result, session);
}

std::vector<Message> FriendHandler::handleAcceptFriendRequest(const Message& message, int fd) const {
    if (message.type != "ACCEPT_FRIEND_REQUEST") {
        return {makeError("PROTOCOL_VIOLATION", "Expected ACCEPT_FRIEND_REQUEST message")};
    }
    if (!message.payload.is_object()) {
        return {makeError("MALFORMED_MESSAGE", "ACCEPT_FRIEND_REQUEST payload must be an object")};
    }

    const session::Session* session = requireIdentified(fd);
    if (!session) {
        return {makeError("NOT_IDENTIFIED", "Client must IDENTIFY before accepting a friend request")};
    }

    if (!message.payload.contains("user_id") || !message.payload["user_id"].is_string()) {
        return {makeError("MALFORMED_MESSAGE", "ACCEPT_FRIEND_REQUEST missing required field: user_id")};
    }
    if (!internal_api_client_) {
        return {makeError("INTERNAL_ERROR", "Friend context unavailable")};
    }

    const std::string requester_id = message.payload["user_id"].get<std::string>();
    const http::AcceptFriendRequestResult result =
        internal_api_client_->acceptFriendRequest(requester_id, session->user_id);
    if (!result.ok) {
        return {makeError("FRIEND_REQUEST_NOT_FOUND", "No pending friend request from that user")};
    }

    updateFriendIdsForBothParties(session->user_id, requester_id, /*added=*/true);

    Message to_caller;
    to_caller.type = "FRIEND_ADDED";
    to_caller.payload = nlohmann::json{{"type", "FRIEND_ADDED"}, {"user_id", requester_id}};

    std::vector<Message> responses{to_caller};
    const std::vector<int> target_fds = session_manager_->getFdsForUser(requester_id);
    if (!target_fds.empty()) {
        Message to_other;
        to_other.type = "FRIEND_ADDED";
        to_other.scope = Scope::TARGETED;
        to_other.target_fds = target_fds;
        to_other.payload = nlohmann::json{{"type", "FRIEND_ADDED"}, {"user_id", session->user_id}};
        responses.push_back(to_other);
    }
    return responses;
}

namespace {

// Shared by REJECT_FRIEND_REQUEST and CANCEL_FRIEND_REQUEST — both delete
// the same row (docs/social/friends-dms-design.md §1.4); only the wire
// response type and which party plays "requester" vs. "recipient" in the
// internal call differ.
std::vector<Message> buildDeleteFriendRequestResponses(bool deleted, const std::string& response_type,
                                                        const std::string& target_user_id,
                                                        const session::Session* session,
                                                        session::SessionManager* session_manager) {
    if (!deleted) {
        Message error;
        error.type = "ERROR";
        error.payload = make_error("FRIEND_REQUEST_NOT_FOUND", "No pending friend request for that user");
        return {error};
    }

    Message to_caller;
    to_caller.type = response_type;
    to_caller.payload = nlohmann::json{{"type", response_type}, {"user_id", target_user_id}};

    std::vector<Message> responses{to_caller};
    const std::vector<int> target_fds = session_manager->getFdsForUser(target_user_id);
    if (!target_fds.empty()) {
        Message to_other;
        to_other.type = response_type;
        to_other.scope = Scope::TARGETED;
        to_other.target_fds = target_fds;
        to_other.payload = nlohmann::json{{"type", response_type}, {"user_id", session->user_id}};
        responses.push_back(to_other);
    }
    return responses;
}

} // namespace

std::vector<Message> FriendHandler::handleRejectFriendRequest(const Message& message, int fd) const {
    if (message.type != "REJECT_FRIEND_REQUEST") {
        return {makeError("PROTOCOL_VIOLATION", "Expected REJECT_FRIEND_REQUEST message")};
    }
    if (!message.payload.is_object()) {
        return {makeError("MALFORMED_MESSAGE", "REJECT_FRIEND_REQUEST payload must be an object")};
    }

    const session::Session* session = requireIdentified(fd);
    if (!session) {
        return {makeError("NOT_IDENTIFIED", "Client must IDENTIFY before rejecting a friend request")};
    }

    if (!message.payload.contains("user_id") || !message.payload["user_id"].is_string()) {
        return {makeError("MALFORMED_MESSAGE", "REJECT_FRIEND_REQUEST missing required field: user_id")};
    }
    if (!internal_api_client_) {
        return {makeError("INTERNAL_ERROR", "Friend context unavailable")};
    }

    // The caller is the recipient rejecting; the payload's user_id is the
    // original requester.
    const std::string requester_id = message.payload["user_id"].get<std::string>();
    const bool deleted = internal_api_client_->deleteFriendRequest(requester_id, session->user_id);
    return buildDeleteFriendRequestResponses(deleted, "FRIEND_REQUEST_REJECTED", requester_id, session,
                                             session_manager_);
}

std::vector<Message> FriendHandler::handleCancelFriendRequest(const Message& message, int fd) const {
    if (message.type != "CANCEL_FRIEND_REQUEST") {
        return {makeError("PROTOCOL_VIOLATION", "Expected CANCEL_FRIEND_REQUEST message")};
    }
    if (!message.payload.is_object()) {
        return {makeError("MALFORMED_MESSAGE", "CANCEL_FRIEND_REQUEST payload must be an object")};
    }

    const session::Session* session = requireIdentified(fd);
    if (!session) {
        return {makeError("NOT_IDENTIFIED", "Client must IDENTIFY before canceling a friend request")};
    }

    if (!message.payload.contains("user_id") || !message.payload["user_id"].is_string()) {
        return {makeError("MALFORMED_MESSAGE", "CANCEL_FRIEND_REQUEST missing required field: user_id")};
    }
    if (!internal_api_client_) {
        return {makeError("INTERNAL_ERROR", "Friend context unavailable")};
    }

    // The caller is the requester canceling; the payload's user_id is the
    // recipient.
    const std::string recipient_id = message.payload["user_id"].get<std::string>();
    const bool deleted = internal_api_client_->deleteFriendRequest(session->user_id, recipient_id);
    return buildDeleteFriendRequestResponses(deleted, "FRIEND_REQUEST_CANCELED", recipient_id, session,
                                             session_manager_);
}

std::vector<Message> FriendHandler::handleRemoveFriend(const Message& message, int fd) const {
    if (message.type != "REMOVE_FRIEND") {
        return {makeError("PROTOCOL_VIOLATION", "Expected REMOVE_FRIEND message")};
    }
    if (!message.payload.is_object()) {
        return {makeError("MALFORMED_MESSAGE", "REMOVE_FRIEND payload must be an object")};
    }

    const session::Session* session = requireIdentified(fd);
    if (!session) {
        return {makeError("NOT_IDENTIFIED", "Client must IDENTIFY before removing a friend")};
    }

    if (!message.payload.contains("user_id") || !message.payload["user_id"].is_string()) {
        return {makeError("MALFORMED_MESSAGE", "REMOVE_FRIEND missing required field: user_id")};
    }
    if (!internal_api_client_) {
        return {makeError("INTERNAL_ERROR", "Friend context unavailable")};
    }

    const std::string other_user_id = message.payload["user_id"].get<std::string>();
    if (!internal_api_client_->removeFriend(session->user_id, other_user_id)) {
        return {makeError("FRIEND_NOT_FOUND", "Not friends with that user")};
    }

    updateFriendIdsForBothParties(session->user_id, other_user_id, /*added=*/false);

    Message to_caller;
    to_caller.type = "FRIEND_REMOVED";
    to_caller.payload = nlohmann::json{{"type", "FRIEND_REMOVED"}, {"user_id", other_user_id}};

    std::vector<Message> responses{to_caller};
    const std::vector<int> target_fds = session_manager_->getFdsForUser(other_user_id);
    if (!target_fds.empty()) {
        Message to_other;
        to_other.type = "FRIEND_REMOVED";
        to_other.scope = Scope::TARGETED;
        to_other.target_fds = target_fds;
        to_other.payload = nlohmann::json{{"type", "FRIEND_REMOVED"}, {"user_id", session->user_id}};
        responses.push_back(to_other);
    }
    return responses;
}

Message FriendHandler::handleListFriends(const Message& message, int fd) const {
    if (message.type != "LIST_FRIENDS") {
        return makeError("PROTOCOL_VIOLATION", "Expected LIST_FRIENDS message");
    }
    if (!message.payload.is_object()) {
        return makeError("MALFORMED_MESSAGE", "LIST_FRIENDS payload must be an object");
    }

    const session::Session* session = requireIdentified(fd);
    if (!session) {
        return makeError("NOT_IDENTIFIED", "Client must IDENTIFY before listing friends");
    }
    if (!internal_api_client_) {
        return makeError("INTERNAL_ERROR", "Friend context unavailable");
    }

    const std::optional<std::vector<http::WireFriend>> friends = internal_api_client_->fetchFriends(session->user_id);
    if (!friends) {
        return makeError("INTERNAL_ERROR", "Failed to fetch friends");
    }

    nlohmann::json friends_json = nlohmann::json::array();
    for (const auto& f : *friends) {
        friends_json.push_back(nlohmann::json{{"user_id", f.user_id}, {"username", f.username}});
    }

    Message response;
    response.type = "FRIEND_LIST";
    response.payload = nlohmann::json{{"type", "FRIEND_LIST"}, {"friends", friends_json}};
    return response;
}

Message FriendHandler::handleListFriendRequests(const Message& message, int fd) const {
    if (message.type != "LIST_FRIEND_REQUESTS") {
        return makeError("PROTOCOL_VIOLATION", "Expected LIST_FRIEND_REQUESTS message");
    }
    if (!message.payload.is_object()) {
        return makeError("MALFORMED_MESSAGE", "LIST_FRIEND_REQUESTS payload must be an object");
    }

    const session::Session* session = requireIdentified(fd);
    if (!session) {
        return makeError("NOT_IDENTIFIED", "Client must IDENTIFY before listing friend requests");
    }
    if (!internal_api_client_) {
        return makeError("INTERNAL_ERROR", "Friend context unavailable");
    }

    const std::optional<http::FriendRequestList> list = internal_api_client_->fetchFriendRequests(session->user_id);
    if (!list) {
        return makeError("INTERNAL_ERROR", "Failed to fetch friend requests");
    }

    auto toJson = [](const std::vector<http::WireFriendRequest>& requests) {
        nlohmann::json array = nlohmann::json::array();
        for (const auto& r : requests) {
            array.push_back(
                nlohmann::json{{"user_id", r.user_id}, {"username", r.username}, {"created_at", r.created_at}});
        }
        return array;
    };

    Message response;
    response.type = "FRIEND_REQUEST_LIST";
    response.payload = nlohmann::json{
        {"type", "FRIEND_REQUEST_LIST"}, {"incoming", toJson(list->incoming)}, {"outgoing", toJson(list->outgoing)}};
    return response;
}

Message FriendHandler::handleFetchFriendCode(const Message& message, int fd) const {
    if (message.type != "FETCH_FRIEND_CODE") {
        return makeError("PROTOCOL_VIOLATION", "Expected FETCH_FRIEND_CODE message");
    }
    if (!message.payload.is_object()) {
        return makeError("MALFORMED_MESSAGE", "FETCH_FRIEND_CODE payload must be an object");
    }

    const session::Session* session = requireIdentified(fd);
    if (!session) {
        return makeError("NOT_IDENTIFIED", "Client must IDENTIFY before fetching a friend code");
    }
    if (!internal_api_client_) {
        return makeError("INTERNAL_ERROR", "Friend context unavailable");
    }

    const std::optional<std::string> code = internal_api_client_->fetchFriendCode(session->user_id);
    if (!code) {
        return makeError("INTERNAL_ERROR", "Failed to fetch friend code");
    }

    Message response;
    response.type = "FRIEND_CODE";
    response.payload = nlohmann::json{{"type", "FRIEND_CODE"}, {"code", *code}};
    return response;
}

Message FriendHandler::handleRegenerateFriendCode(const Message& message, int fd) const {
    if (message.type != "REGENERATE_FRIEND_CODE") {
        return makeError("PROTOCOL_VIOLATION", "Expected REGENERATE_FRIEND_CODE message");
    }
    if (!message.payload.is_object()) {
        return makeError("MALFORMED_MESSAGE", "REGENERATE_FRIEND_CODE payload must be an object");
    }

    const session::Session* session = requireIdentified(fd);
    if (!session) {
        return makeError("NOT_IDENTIFIED", "Client must IDENTIFY before regenerating a friend code");
    }
    if (!internal_api_client_) {
        return makeError("INTERNAL_ERROR", "Friend context unavailable");
    }

    const std::optional<std::string> code = internal_api_client_->regenerateFriendCode(session->user_id);
    if (!code) {
        return makeError("INTERNAL_ERROR", "Failed to regenerate friend code");
    }

    Message response;
    response.type = "FRIEND_CODE";
    response.payload = nlohmann::json{{"type", "FRIEND_CODE"}, {"code", *code}};
    return response;
}

} // namespace protocol
