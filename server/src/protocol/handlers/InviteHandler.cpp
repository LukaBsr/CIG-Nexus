#include "protocol/handlers/InviteHandler.hpp"

#include "guild/Channel.hpp"
#include "guild/GuildManager.hpp"
#include "http/InternalApiClient.hpp"
#include "protocol/MessageBuilders.hpp"
#include "session/SessionManager.hpp"

namespace protocol {

void InviteHandler::setSessionManager(session::SessionManager* session_manager) {
    session_manager_ = session_manager;
}

void InviteHandler::setGuildManager(guild::GuildManager* guild_manager) {
    guild_manager_ = guild_manager;
}

void InviteHandler::setInternalApiClient(http::InternalApiClient* internal_api_client) {
    internal_api_client_ = internal_api_client;
}

Message InviteHandler::makeError(const std::string& code, const std::string& msg) {
    Message response;
    response.type = "ERROR";
    response.payload = make_error(code, msg);
    return response;
}

const session::Session* InviteHandler::requireIdentified(int fd) const {
    if (!session_manager_) {
        return nullptr;
    }

    const session::Session* session = session_manager_->getSession(fd);
    if (!session || session->username.empty()) {
        return nullptr;
    }

    return session;
}

std::vector<int> InviteHandler::getOfficerFds(const std::string& guild_id) const {
    std::vector<int> officer_fds;
    for (int candidate_fd : session_manager_->getFdsInGuild(guild_id)) {
        const session::Session* candidate = session_manager_->getSession(candidate_fd);
        if (candidate && guild_manager_->isOfficerOrAbove(guild_id, candidate->user_id)) {
            officer_fds.push_back(candidate_fd);
        }
    }
    return officer_fds;
}

namespace {

nlohmann::json inviteToJson(const http::WireInvite& invite) {
    return nlohmann::json{
        {"code", invite.code},
        {"max_uses", invite.max_uses.has_value() ? nlohmann::json(*invite.max_uses) : nullptr},
        {"use_count", invite.use_count},
        {"expires_at", invite.expires_at.has_value() ? nlohmann::json(*invite.expires_at) : nullptr},
        {"revoked_at", invite.revoked_at.has_value() ? nlohmann::json(*invite.revoked_at) : nullptr},
        {"created_at", invite.created_at}};
}

} // namespace

Message InviteHandler::handleCreateInvite(const Message& message, int fd) const {
    if (message.type != "CREATE_INVITE") {
        return makeError("PROTOCOL_VIOLATION", "Expected CREATE_INVITE message");
    }

    if (!message.payload.is_object()) {
        return makeError("MALFORMED_MESSAGE", "CREATE_INVITE payload must be an object");
    }

    const session::Session* session = requireIdentified(fd);
    if (!session) {
        return makeError("NOT_IDENTIFIED", "Client must IDENTIFY before creating an invite");
    }

    if (!message.payload.contains("guild_id") || !message.payload["guild_id"].is_string()) {
        return makeError("MALFORMED_MESSAGE", "CREATE_INVITE missing required field: guild_id");
    }

    std::optional<int> max_uses;
    if (message.payload.contains("max_uses") && !message.payload["max_uses"].is_null()) {
        if (!message.payload["max_uses"].is_number_integer() || message.payload["max_uses"].get<int>() <= 0) {
            return makeError("MALFORMED_MESSAGE", "CREATE_INVITE max_uses must be a positive integer");
        }
        max_uses = message.payload["max_uses"].get<int>();
    }

    std::optional<int> expires_in_seconds;
    if (message.payload.contains("expires_in_seconds") && !message.payload["expires_in_seconds"].is_null()) {
        if (!message.payload["expires_in_seconds"].is_number_integer() ||
            message.payload["expires_in_seconds"].get<int>() <= 0) {
            return makeError("MALFORMED_MESSAGE",
                             "CREATE_INVITE expires_in_seconds must be a positive integer");
        }
        expires_in_seconds = message.payload["expires_in_seconds"].get<int>();
    }

    if (!guild_manager_ || !internal_api_client_) {
        return makeError("INTERNAL_ERROR", "Guild context unavailable");
    }

    const std::string guild_id = message.payload["guild_id"].get<std::string>();
    if (!guild_manager_->hasGuild(guild_id)) {
        return makeError("GUILD_NOT_FOUND", "No guild with that id");
    }

    // docs/social-presence-design.md §2.2/§5: officer-or-above, not
    // owner-only — "if you can create invites, you can see the ones that
    // exist" (§1.4) applies the same predicate to LIST_INVITES/REVOKE_INVITE
    // below.
    if (!guild_manager_->canCreateInvite(guild_id, session->user_id)) {
        return makeError("NOT_GUILD_OFFICER", "Must be an officer or above to create invites");
    }

    const std::optional<http::WireInvite> invite =
        internal_api_client_->createInvite(guild_id, session->user_id, max_uses, expires_in_seconds);
    if (!invite) {
        return makeError("INTERNAL_ERROR", "Failed to create invite");
    }

    Message response;
    response.type = "INVITE_CREATED";
    nlohmann::json payload = inviteToJson(*invite);
    payload["type"] = "INVITE_CREATED";
    payload["guild_id"] = guild_id;
    payload.erase("revoked_at"); // §1.4: not part of INVITE_CREATED's shape
    response.payload = payload;
    return response;
}

Message InviteHandler::handleListInvites(const Message& message, int fd) const {
    if (message.type != "LIST_INVITES") {
        return makeError("PROTOCOL_VIOLATION", "Expected LIST_INVITES message");
    }

    if (!message.payload.is_object()) {
        return makeError("MALFORMED_MESSAGE", "LIST_INVITES payload must be an object");
    }

    const session::Session* session = requireIdentified(fd);
    if (!session) {
        return makeError("NOT_IDENTIFIED", "Client must IDENTIFY before listing invites");
    }

    if (!message.payload.contains("guild_id") || !message.payload["guild_id"].is_string()) {
        return makeError("MALFORMED_MESSAGE", "LIST_INVITES missing required field: guild_id");
    }

    if (!guild_manager_ || !internal_api_client_) {
        return makeError("INTERNAL_ERROR", "Guild context unavailable");
    }

    const std::string guild_id = message.payload["guild_id"].get<std::string>();
    if (!guild_manager_->hasGuild(guild_id)) {
        return makeError("GUILD_NOT_FOUND", "No guild with that id");
    }

    if (!guild_manager_->canCreateInvite(guild_id, session->user_id)) {
        return makeError("NOT_GUILD_OFFICER", "Must be an officer or above to list invites");
    }

    const std::optional<std::vector<http::WireInvite>> invites = internal_api_client_->fetchInvites(guild_id);
    if (!invites) {
        return makeError("INTERNAL_ERROR", "Failed to fetch invites");
    }

    nlohmann::json invites_json = nlohmann::json::array();
    for (const auto& invite : *invites) {
        invites_json.push_back(inviteToJson(invite));
    }

    Message response;
    response.type = "INVITE_LIST";
    response.payload =
        nlohmann::json{{"type", "INVITE_LIST"}, {"guild_id", guild_id}, {"invites", invites_json}};
    return response;
}

Message InviteHandler::handleRevokeInvite(const Message& message, int fd) const {
    if (message.type != "REVOKE_INVITE") {
        return makeError("PROTOCOL_VIOLATION", "Expected REVOKE_INVITE message");
    }

    if (!message.payload.is_object()) {
        return makeError("MALFORMED_MESSAGE", "REVOKE_INVITE payload must be an object");
    }

    const session::Session* session = requireIdentified(fd);
    if (!session) {
        return makeError("NOT_IDENTIFIED", "Client must IDENTIFY before revoking an invite");
    }

    if (!message.payload.contains("guild_id") || !message.payload["guild_id"].is_string()) {
        return makeError("MALFORMED_MESSAGE", "REVOKE_INVITE missing required field: guild_id");
    }
    if (!message.payload.contains("code") || !message.payload["code"].is_string()) {
        return makeError("MALFORMED_MESSAGE", "REVOKE_INVITE missing required field: code");
    }

    if (!guild_manager_ || !internal_api_client_) {
        return makeError("INTERNAL_ERROR", "Guild context unavailable");
    }

    const std::string guild_id = message.payload["guild_id"].get<std::string>();
    const std::string code = message.payload["code"].get<std::string>();
    if (!guild_manager_->hasGuild(guild_id)) {
        return makeError("GUILD_NOT_FOUND", "No guild with that id");
    }

    if (!guild_manager_->canCreateInvite(guild_id, session->user_id)) {
        return makeError("NOT_GUILD_OFFICER", "Must be an officer or above to revoke invites");
    }

    if (!internal_api_client_->revokeInvite(guild_id, code)) {
        return makeError("INVITE_NOT_FOUND", "No such invite for this guild");
    }

    Message response;
    response.type = "INVITE_REVOKED";
    response.payload = nlohmann::json{{"type", "INVITE_REVOKED"}, {"guild_id", guild_id}, {"code", code}};
    return response;
}

std::vector<Message> InviteHandler::handleJoinViaInvite(const Message& message, int fd) const {
    if (message.type != "JOIN_VIA_INVITE") {
        return {makeError("PROTOCOL_VIOLATION", "Expected JOIN_VIA_INVITE message")};
    }

    if (!message.payload.is_object()) {
        return {makeError("MALFORMED_MESSAGE", "JOIN_VIA_INVITE payload must be an object")};
    }

    const session::Session* session = requireIdentified(fd);
    if (!session) {
        return {makeError("NOT_IDENTIFIED", "Client must IDENTIFY before joining via invite")};
    }

    if (!message.payload.contains("code") || !message.payload["code"].is_string()) {
        return {makeError("MALFORMED_MESSAGE", "JOIN_VIA_INVITE missing required field: code")};
    }

    if (!guild_manager_ || !internal_api_client_) {
        return {makeError("INTERNAL_ERROR", "Guild context unavailable")};
    }

    const std::string code = message.payload["code"].get<std::string>();
    const http::RedeemInviteResult result = internal_api_client_->redeemInvite(code, session->user_id);

    if (!result.ok) {
        switch (result.error) {
        case http::RedeemInviteError::NOT_FOUND:
            return {makeError("INVITE_NOT_FOUND", "No such invite")};
        case http::RedeemInviteError::REVOKED:
            return {makeError("INVITE_REVOKED", "This invite has been revoked")};
        case http::RedeemInviteError::EXPIRED:
            return {makeError("INVITE_EXPIRED", "This invite has expired")};
        case http::RedeemInviteError::MAX_USES_REACHED:
            return {makeError("INVITE_MAX_USES_REACHED", "This invite has reached its use limit")};
        case http::RedeemInviteError::ALREADY_MEMBER:
            // §1.3: reuses the existing "already a member" convention.
            return {makeError("PROTOCOL_VIOLATION", "Already a member of this guild")};
        case http::RedeemInviteError::FAILED:
        default:
            return {makeError("INTERNAL_ERROR", "Failed to redeem invite")};
        }
    }

    const guild::Guild* target_guild = guild_manager_->getGuild(result.guild_id);
    if (!target_guild) {
        return {makeError("INTERNAL_ERROR", "Joined guild is not in the local cache")};
    }

    if (result.is_join_request) {
        // §1.8's ARBITRATION: invite still requires approval against an
        // `application`-visibility guild — the invite is consumed, but this
        // is a join request, not membership. Mirrors REQUEST_JOIN's own
        // response shapes exactly (JoinRequestHandler), since from the
        // requester's and officers' point of view this is the same event
        // reached via a different door.
        Message requested;
        requested.type = "JOIN_REQUESTED";
        requested.payload = nlohmann::json{{"type", "JOIN_REQUESTED"}, {"guild_id", result.guild_id}};

        std::vector<Message> responses{requested};
        const std::vector<int> officer_fds = getOfficerFds(result.guild_id);
        if (!officer_fds.empty()) {
            Message received;
            received.type = "JOIN_REQUEST_RECEIVED";
            received.scope = Scope::TARGETED;
            received.target_fds = officer_fds;
            received.payload = nlohmann::json{{"type", "JOIN_REQUEST_RECEIVED"},
                                              {"guild_id", result.guild_id},
                                              {"user_id", session->user_id},
                                              {"username", session->username}};
            responses.push_back(received);
        }
        return responses;
    }

    session_manager_->addGuildMembership(fd, result.guild_id);
    guild_manager_->setMemberRank(result.guild_id, session->user_id, result.role_rank.value_or(0));

    nlohmann::json channels = nlohmann::json::array();
    for (const auto& c : guild_manager_->listChannels(result.guild_id)) {
        channels.push_back(
            {{"channel_id", c.id}, {"name", c.name}, {"channel_type", guild::toString(c.type)}});
    }

    Message joined;
    joined.type = "GUILD_JOINED";
    joined.payload = nlohmann::json{{"type", "GUILD_JOINED"},
                                    {"guild_id", target_guild->id},
                                    {"name", target_guild->name},
                                    {"owner_id", target_guild->owner_id},
                                    {"visibility", guild::toString(target_guild->visibility)},
                                    {"channels", channels}};
    return {joined};
}

} // namespace protocol
