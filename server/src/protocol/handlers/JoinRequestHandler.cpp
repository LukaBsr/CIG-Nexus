#include "protocol/handlers/JoinRequestHandler.hpp"

#include "guild/Channel.hpp"
#include "guild/GuildManager.hpp"
#include "http/InternalApiClient.hpp"
#include "protocol/MessageBuilders.hpp"
#include "session/SessionManager.hpp"

namespace protocol {

void JoinRequestHandler::setSessionManager(session::SessionManager* session_manager) {
    session_manager_ = session_manager;
}

void JoinRequestHandler::setGuildManager(guild::GuildManager* guild_manager) {
    guild_manager_ = guild_manager;
}

void JoinRequestHandler::setInternalApiClient(http::InternalApiClient* internal_api_client) {
    internal_api_client_ = internal_api_client;
}

Message JoinRequestHandler::makeError(const std::string& code, const std::string& msg) {
    Message response;
    response.type = "ERROR";
    response.payload = make_error(code, msg);
    return response;
}

const session::Session* JoinRequestHandler::requireIdentified(int fd) const {
    if (!session_manager_) {
        return nullptr;
    }

    const session::Session* session = session_manager_->getSession(fd);
    if (!session || session->username.empty()) {
        return nullptr;
    }

    return session;
}

std::vector<int> JoinRequestHandler::getOfficerFds(const std::string& guild_id) const {
    std::vector<int> officer_fds;
    for (int candidate_fd : session_manager_->getFdsInGuild(guild_id)) {
        const session::Session* candidate = session_manager_->getSession(candidate_fd);
        if (candidate && guild_manager_->isOfficerOrAbove(guild_id, candidate->user_id)) {
            officer_fds.push_back(candidate_fd);
        }
    }
    return officer_fds;
}

std::vector<Message> JoinRequestHandler::handleRequestJoin(const Message& message, int fd) const {
    if (message.type != "REQUEST_JOIN") {
        return {makeError("PROTOCOL_VIOLATION", "Expected REQUEST_JOIN message")};
    }

    if (!message.payload.is_object()) {
        return {makeError("MALFORMED_MESSAGE", "REQUEST_JOIN payload must be an object")};
    }

    const session::Session* session = requireIdentified(fd);
    if (!session) {
        return {makeError("NOT_IDENTIFIED", "Client must IDENTIFY before requesting to join a guild")};
    }

    if (!message.payload.contains("guild_id") || !message.payload["guild_id"].is_string()) {
        return {makeError("MALFORMED_MESSAGE", "REQUEST_JOIN missing required field: guild_id")};
    }

    if (!guild_manager_ || !internal_api_client_) {
        return {makeError("INTERNAL_ERROR", "Guild context unavailable")};
    }

    const std::string guild_id = message.payload["guild_id"].get<std::string>();
    const guild::Guild* target_guild = guild_manager_->getGuild(guild_id);
    if (!target_guild) {
        return {makeError("GUILD_NOT_FOUND", "No guild with that id")};
    }

    if (session_manager_->isMemberOfGuild(fd, guild_id)) {
        return {makeError("PROTOCOL_VIOLATION", "Already a member of this guild")};
    }

    // docs/guilds/social-presence-design.md §1.8: same information-hiding as
    // JOIN_GUILD — a non-member probing a `private` guild's id gets the
    // same GUILD_NOT_FOUND a nonexistent id would.
    if (target_guild->visibility == guild::GuildVisibility::PRIVATE) {
        return {makeError("GUILD_NOT_FOUND", "No guild with that id")};
    }
    if (target_guild->visibility == guild::GuildVisibility::OPEN) {
        return {makeError("PROTOCOL_VIOLATION", "Guild is open — use JOIN_GUILD instead")};
    }

    const http::CreateJoinRequestResult result =
        internal_api_client_->createJoinRequest(guild_id, session->user_id);
    if (result == http::CreateJoinRequestResult::ALREADY_PENDING) {
        return {makeError("JOIN_REQUEST_ALREADY_PENDING", "A join request is already pending for this guild")};
    }
    if (result == http::CreateJoinRequestResult::FAILED) {
        return {makeError("INTERNAL_ERROR", "Failed to create join request")};
    }

    Message requested;
    requested.type = "JOIN_REQUESTED";
    requested.payload = nlohmann::json{{"type", "JOIN_REQUESTED"}, {"guild_id", guild_id}};

    std::vector<Message> responses{requested};
    const std::vector<int> officer_fds = getOfficerFds(guild_id);
    if (!officer_fds.empty()) {
        Message received;
        received.type = "JOIN_REQUEST_RECEIVED";
        received.scope = Scope::TARGETED;
        received.target_fds = officer_fds;
        received.payload = nlohmann::json{{"type", "JOIN_REQUEST_RECEIVED"},
                                          {"guild_id", guild_id},
                                          {"user_id", session->user_id},
                                          {"username", session->username}};
        responses.push_back(received);
    }
    return responses;
}

Message JoinRequestHandler::handleListJoinRequests(const Message& message, int fd) const {
    if (message.type != "LIST_JOIN_REQUESTS") {
        return makeError("PROTOCOL_VIOLATION", "Expected LIST_JOIN_REQUESTS message");
    }

    if (!message.payload.is_object()) {
        return makeError("MALFORMED_MESSAGE", "LIST_JOIN_REQUESTS payload must be an object");
    }

    const session::Session* session = requireIdentified(fd);
    if (!session) {
        return makeError("NOT_IDENTIFIED", "Client must IDENTIFY before listing join requests");
    }

    if (!message.payload.contains("guild_id") || !message.payload["guild_id"].is_string()) {
        return makeError("MALFORMED_MESSAGE", "LIST_JOIN_REQUESTS missing required field: guild_id");
    }

    if (!guild_manager_ || !internal_api_client_) {
        return makeError("INTERNAL_ERROR", "Guild context unavailable");
    }

    const std::string guild_id = message.payload["guild_id"].get<std::string>();
    if (!guild_manager_->hasGuild(guild_id)) {
        return makeError("GUILD_NOT_FOUND", "No guild with that id");
    }

    if (!guild_manager_->canApproveJoinRequest(guild_id, session->user_id)) {
        return makeError("NOT_GUILD_OFFICER", "Must be an officer or above to list join requests");
    }

    const std::optional<std::vector<http::WireJoinRequest>> requests =
        internal_api_client_->fetchJoinRequests(guild_id);
    if (!requests) {
        return makeError("INTERNAL_ERROR", "Failed to fetch join requests");
    }

    nlohmann::json requests_json = nlohmann::json::array();
    for (const auto& r : *requests) {
        requests_json.push_back(nlohmann::json{
            {"user_id", r.user_id}, {"username", r.username}, {"requested_at", r.requested_at}});
    }

    Message response;
    response.type = "JOIN_REQUEST_LIST";
    response.payload = nlohmann::json{
        {"type", "JOIN_REQUEST_LIST"}, {"guild_id", guild_id}, {"requests", requests_json}};
    return response;
}

std::vector<Message> JoinRequestHandler::handleApproveJoinRequest(const Message& message, int fd) const {
    if (message.type != "APPROVE_JOIN_REQUEST") {
        return {makeError("PROTOCOL_VIOLATION", "Expected APPROVE_JOIN_REQUEST message")};
    }

    if (!message.payload.is_object()) {
        return {makeError("MALFORMED_MESSAGE", "APPROVE_JOIN_REQUEST payload must be an object")};
    }

    const session::Session* session = requireIdentified(fd);
    if (!session) {
        return {makeError("NOT_IDENTIFIED", "Client must IDENTIFY before approving a join request")};
    }

    if (!message.payload.contains("guild_id") || !message.payload["guild_id"].is_string()) {
        return {makeError("MALFORMED_MESSAGE", "APPROVE_JOIN_REQUEST missing required field: guild_id")};
    }
    if (!message.payload.contains("user_id") || !message.payload["user_id"].is_string()) {
        return {makeError("MALFORMED_MESSAGE", "APPROVE_JOIN_REQUEST missing required field: user_id")};
    }

    if (!guild_manager_ || !internal_api_client_) {
        return {makeError("INTERNAL_ERROR", "Guild context unavailable")};
    }

    const std::string guild_id = message.payload["guild_id"].get<std::string>();
    const std::string target_user_id = message.payload["user_id"].get<std::string>();
    const guild::Guild* target_guild = guild_manager_->getGuild(guild_id);
    if (!target_guild) {
        return {makeError("GUILD_NOT_FOUND", "No guild with that id")};
    }

    if (!guild_manager_->canApproveJoinRequest(guild_id, session->user_id)) {
        return {makeError("NOT_GUILD_OFFICER", "Must be an officer or above to approve join requests")};
    }

    const std::optional<int> role_rank = internal_api_client_->approveJoinRequest(guild_id, target_user_id);
    if (!role_rank) {
        return {makeError("JOIN_REQUEST_NOT_FOUND", "No pending join request for that user")};
    }

    guild_manager_->setMemberRank(guild_id, target_user_id, *role_rank);
    const std::vector<int> target_fds = session_manager_->getFdsForUser(target_user_id);
    for (int target_fd : target_fds) {
        session_manager_->addGuildMembership(target_fd, guild_id);
    }

    Message approved;
    approved.type = "JOIN_REQUEST_APPROVED";
    approved.payload = nlohmann::json{
        {"type", "JOIN_REQUEST_APPROVED"}, {"guild_id", guild_id}, {"user_id", target_user_id}};

    std::vector<Message> responses{approved};
    if (!target_fds.empty()) {
        nlohmann::json channels = nlohmann::json::array();
        for (const auto& c : guild_manager_->listChannels(guild_id)) {
            channels.push_back(
                {{"channel_id", c.id}, {"name", c.name}, {"channel_type", guild::toString(c.type)}});
        }

        Message joined;
        joined.type = "GUILD_JOINED";
        joined.scope = Scope::TARGETED;
        joined.target_fds = target_fds;
        joined.payload = nlohmann::json{{"type", "GUILD_JOINED"},
                                        {"guild_id", target_guild->id},
                                        {"name", target_guild->name},
                                        {"owner_id", target_guild->owner_id},
                                        {"visibility", guild::toString(target_guild->visibility)},
                                        {"channels", channels}};
        responses.push_back(joined);
    }
    return responses;
}

std::vector<Message> JoinRequestHandler::handleRejectJoinRequest(const Message& message, int fd) const {
    if (message.type != "REJECT_JOIN_REQUEST") {
        return {makeError("PROTOCOL_VIOLATION", "Expected REJECT_JOIN_REQUEST message")};
    }

    if (!message.payload.is_object()) {
        return {makeError("MALFORMED_MESSAGE", "REJECT_JOIN_REQUEST payload must be an object")};
    }

    const session::Session* session = requireIdentified(fd);
    if (!session) {
        return {makeError("NOT_IDENTIFIED", "Client must IDENTIFY before rejecting a join request")};
    }

    if (!message.payload.contains("guild_id") || !message.payload["guild_id"].is_string()) {
        return {makeError("MALFORMED_MESSAGE", "REJECT_JOIN_REQUEST missing required field: guild_id")};
    }
    if (!message.payload.contains("user_id") || !message.payload["user_id"].is_string()) {
        return {makeError("MALFORMED_MESSAGE", "REJECT_JOIN_REQUEST missing required field: user_id")};
    }

    if (!guild_manager_ || !internal_api_client_) {
        return {makeError("INTERNAL_ERROR", "Guild context unavailable")};
    }

    const std::string guild_id = message.payload["guild_id"].get<std::string>();
    const std::string target_user_id = message.payload["user_id"].get<std::string>();
    if (!guild_manager_->hasGuild(guild_id)) {
        return {makeError("GUILD_NOT_FOUND", "No guild with that id")};
    }

    if (!guild_manager_->canApproveJoinRequest(guild_id, session->user_id)) {
        return {makeError("NOT_GUILD_OFFICER", "Must be an officer or above to reject join requests")};
    }

    if (!internal_api_client_->rejectJoinRequest(guild_id, target_user_id)) {
        return {makeError("JOIN_REQUEST_NOT_FOUND", "No pending join request for that user")};
    }

    const nlohmann::json rejected_payload = nlohmann::json{
        {"type", "JOIN_REQUEST_REJECTED"}, {"guild_id", guild_id}, {"user_id", target_user_id}};

    Message rejected;
    rejected.type = "JOIN_REQUEST_REJECTED";
    rejected.payload = rejected_payload;

    std::vector<Message> responses{rejected};
    const std::vector<int> target_fds = session_manager_->getFdsForUser(target_user_id);
    if (!target_fds.empty()) {
        Message notified;
        notified.type = "JOIN_REQUEST_REJECTED";
        notified.scope = Scope::TARGETED;
        notified.target_fds = target_fds;
        notified.payload = rejected_payload;
        responses.push_back(notified);
    }
    return responses;
}

} // namespace protocol
