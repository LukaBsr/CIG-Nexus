#include "protocol/handlers/GuildHandler.hpp"

#include "guild/Channel.hpp"
#include "guild/GuildManager.hpp"
#include "guild/RoleRank.hpp"
#include "http/InternalApiClient.hpp"
#include "protocol/MessageBuilders.hpp"
#include "session/SessionManager.hpp"

namespace protocol {

void GuildHandler::setSessionManager(session::SessionManager* session_manager) {
    session_manager_ = session_manager;
}

void GuildHandler::setGuildManager(guild::GuildManager* guild_manager) {
    guild_manager_ = guild_manager;
}

void GuildHandler::setInternalApiClient(http::InternalApiClient* internal_api_client) {
    internal_api_client_ = internal_api_client;
}

Message GuildHandler::makeError(const std::string& code, const std::string& msg) {
    Message response;
    response.type = "ERROR";
    response.payload = make_error(code, msg);
    return response;
}

const session::Session* GuildHandler::requireIdentified(int fd) const {
    if (!session_manager_) {
        return nullptr;
    }

    const session::Session* session = session_manager_->getSession(fd);
    if (!session || session->username.empty()) {
        return nullptr;
    }

    return session;
}

Message GuildHandler::handleCreateGuild(const Message& message, int fd) const {
    if (message.type != "CREATE_GUILD") {
        return makeError("PROTOCOL_VIOLATION", "Expected CREATE_GUILD message");
    }

    if (!message.payload.is_object()) {
        return makeError("MALFORMED_MESSAGE", "CREATE_GUILD payload must be an object");
    }

    const session::Session* session = requireIdentified(fd);
    if (!session) {
        return makeError("NOT_IDENTIFIED", "Client must IDENTIFY before creating a guild");
    }

    if (!message.payload.contains("name")) {
        return makeError("MALFORMED_MESSAGE", "CREATE_GUILD missing required field: name");
    }

    if (!message.payload["name"].is_string()) {
        return makeError("MALFORMED_MESSAGE", "CREATE_GUILD name must be a string");
    }

    const std::string name = message.payload["name"].get<std::string>();
    if (name.empty()) {
        return makeError("MALFORMED_MESSAGE", "CREATE_GUILD name must not be empty");
    }

    if (name.length() > 64) {
        return makeError("MALFORMED_MESSAGE", "CREATE_GUILD name must be <= 64 characters");
    }

    // docs/guilds/social-presence-design.md §1.7: optional, defaults to 'open'.
    guild::GuildVisibility visibility = guild::GuildVisibility::OPEN;
    if (message.payload.contains("visibility") && !message.payload["visibility"].is_null()) {
        if (!message.payload["visibility"].is_string()) {
            return makeError("MALFORMED_MESSAGE", "CREATE_GUILD visibility must be a string");
        }
        const auto parsed = guild::guildVisibilityFromString(message.payload["visibility"].get<std::string>());
        if (!parsed) {
            return makeError("MALFORMED_MESSAGE",
                             "CREATE_GUILD visibility must be open, application, or private");
        }
        visibility = *parsed;
    }

    if (!guild_manager_ || !internal_api_client_) {
        return makeError("INTERNAL_ERROR", "Guild context unavailable");
    }

    // Write-through: persist first (design doc §8.1), only touch the local
    // cache/session state if that succeeds. The Postgres-assigned guild_id
    // and owner-membership row (created transactionally on the Next.js
    // side, docs/guilds/design.md decision #2) come back in the response.
    const std::optional<http::WireGuild> created =
        internal_api_client_->createGuild(name, session->user_id, guild::toString(visibility));
    if (!created) {
        return makeError("INTERNAL_ERROR", "Failed to persist new guild");
    }

    const auto created_visibility = guild::guildVisibilityFromString(created->visibility);
    guild::Guild& new_guild = guild_manager_->upsertGuild(
        created->guild_id, created->name, created->owner_id, created_visibility.value_or(visibility));
    session_manager_->addGuildMembership(fd, new_guild.id);
    // Maintained by construction, not by trusting a role_rank the internal
    // API would otherwise have to echo back (docs/guilds/social-presence-design.md
    // §2.2): CREATE_GUILD always creates the owner's row at kOwnerRank.
    guild_manager_->setMemberRank(new_guild.id, new_guild.owner_id, guild::kOwnerRank);

    Message response;
    response.type = "GUILD_CREATED";
    response.payload = nlohmann::json{{"type", "GUILD_CREATED"},
                                      {"guild_id", new_guild.id},
                                      {"name", new_guild.name},
                                      {"owner_id", new_guild.owner_id},
                                      {"visibility", guild::toString(new_guild.visibility)}};
    return response;
}

Message GuildHandler::handleListGuilds(const Message& message, int fd) const {
    if (message.type != "LIST_GUILDS") {
        return makeError("PROTOCOL_VIOLATION", "Expected LIST_GUILDS message");
    }

    if (!requireIdentified(fd)) {
        return makeError("NOT_IDENTIFIED", "Client must IDENTIFY before listing guilds");
    }

    if (!guild_manager_) {
        return makeError("INTERNAL_ERROR", "Guild context unavailable");
    }

    // Served entirely from the local write-through cache — no internal API
    // call per read (design doc §8.1).
    // docs/guilds/social-presence-design.md §1.8: a `private` guild is filtered
    // out of LIST_GUILDS for everyone except its own members — "private
    // means private, not private except to whichever error code you
    // trigger" applies here too, so this is a silent omission, not an
    // error.
    nlohmann::json guilds = nlohmann::json::array();
    for (const auto& g : guild_manager_->listGuilds()) {
        if (g.visibility == guild::GuildVisibility::PRIVATE && !session_manager_->isMemberOfGuild(fd, g.id)) {
            continue;
        }
        guilds.push_back({{"guild_id", g.id},
                          {"name", g.name},
                          {"owner_id", g.owner_id},
                          {"visibility", guild::toString(g.visibility)}});
    }

    Message response;
    response.type = "GUILD_LIST";
    response.payload = nlohmann::json{{"type", "GUILD_LIST"}, {"guilds", guilds}};
    return response;
}

Message GuildHandler::handleJoinGuild(const Message& message, int fd) const {
    if (message.type != "JOIN_GUILD") {
        return makeError("PROTOCOL_VIOLATION", "Expected JOIN_GUILD message");
    }

    if (!message.payload.is_object()) {
        return makeError("MALFORMED_MESSAGE", "JOIN_GUILD payload must be an object");
    }

    if (!requireIdentified(fd)) {
        return makeError("NOT_IDENTIFIED", "Client must IDENTIFY before joining a guild");
    }

    if (!message.payload.contains("guild_id") || !message.payload["guild_id"].is_string()) {
        return makeError("MALFORMED_MESSAGE", "JOIN_GUILD missing required field: guild_id");
    }

    if (!guild_manager_ || !internal_api_client_) {
        return makeError("INTERNAL_ERROR", "Guild context unavailable");
    }

    const std::string guild_id = message.payload["guild_id"].get<std::string>();
    const guild::Guild* target_guild = guild_manager_->getGuild(guild_id);
    if (!target_guild) {
        return makeError("GUILD_NOT_FOUND", "No guild with that id");
    }

    if (session_manager_->isMemberOfGuild(fd, guild_id)) {
        return makeError("PROTOCOL_VIOLATION", "Already a member of this guild");
    }

    // docs/guilds/social-presence-design.md §1.8: `private` returns the same
    // GUILD_NOT_FOUND a genuinely nonexistent id would — indistinguishable
    // to a non-member, by design (the ARBITRATION there). `application`
    // tells the client to use REQUEST_JOIN instead.
    if (target_guild->visibility == guild::GuildVisibility::PRIVATE) {
        return makeError("GUILD_NOT_FOUND", "No guild with that id");
    }
    if (target_guild->visibility == guild::GuildVisibility::APPLICATION) {
        return makeError("GUILD_REQUIRES_APPROVAL",
                         "This guild requires approval to join — use REQUEST_JOIN instead");
    }

    // Note: createMembership is idempotent server-side for a membership
    // that already exists in Postgres from a prior session (a returning
    // connection's SessionManager state starts empty every reconnect, see
    // web/lib/internal/catalog.ts) — so this call succeeding here doesn't
    // imply a *new* row was created, only that one now exists. The
    // returned rank is the row's *actual* current rank, not necessarily
    // kMemberRank: an idempotent rejoin must not clobber a previously-
    // promoted officer's cached rank down to kMemberRank.
    const session::Session* session = requireIdentified(fd);
    const std::optional<int> resulting_rank =
        internal_api_client_->createMembership(guild_id, session->user_id, guild::kMemberRank);
    if (!resulting_rank) {
        return makeError("INTERNAL_ERROR", "Failed to persist guild membership");
    }

    session_manager_->addGuildMembership(fd, guild_id);
    guild_manager_->setMemberRank(guild_id, session->user_id, *resulting_rank);

    nlohmann::json channels = nlohmann::json::array();
    for (const auto& c : guild_manager_->listChannels(guild_id)) {
        channels.push_back(
            {{"channel_id", c.id}, {"name", c.name}, {"channel_type", guild::toString(c.type)}});
    }

    Message response;
    response.type = "GUILD_JOINED";
    response.payload = nlohmann::json{{"type", "GUILD_JOINED"},
                                      {"guild_id", target_guild->id},
                                      {"name", target_guild->name},
                                      {"owner_id", target_guild->owner_id},
                                      {"visibility", guild::toString(target_guild->visibility)},
                                      {"channels", channels}};
    return response;
}

Message GuildHandler::handleLeaveGuild(const Message& message, int fd) const {
    if (message.type != "LEAVE_GUILD") {
        return makeError("PROTOCOL_VIOLATION", "Expected LEAVE_GUILD message");
    }

    if (!message.payload.is_object()) {
        return makeError("MALFORMED_MESSAGE", "LEAVE_GUILD payload must be an object");
    }

    const session::Session* session = requireIdentified(fd);
    if (!session) {
        return makeError("NOT_IDENTIFIED", "Client must IDENTIFY before leaving a guild");
    }

    if (!message.payload.contains("guild_id") || !message.payload["guild_id"].is_string()) {
        return makeError("MALFORMED_MESSAGE", "LEAVE_GUILD missing required field: guild_id");
    }

    if (!guild_manager_ || !internal_api_client_) {
        return makeError("INTERNAL_ERROR", "Guild context unavailable");
    }

    const std::string guild_id = message.payload["guild_id"].get<std::string>();
    if (!guild_manager_->hasGuild(guild_id)) {
        return makeError("GUILD_NOT_FOUND", "No guild with that id");
    }

    if (!session_manager_->isMemberOfGuild(fd, guild_id)) {
        return makeError("NOT_GUILD_MEMBER", "Not a member of this guild");
    }

    if (guild_manager_->isOwner(guild_id, session->user_id)) {
        return makeError("PROTOCOL_VIOLATION",
                         "Guild owner cannot leave; use DELETE_GUILD instead");
    }

    if (!internal_api_client_->deleteMembership(guild_id, session->user_id)) {
        return makeError("INTERNAL_ERROR", "Failed to remove guild membership");
    }

    // Snapshot before mutating membership so the leaver is included in the
    // notification, mirroring how CHAT_MESSAGE's BROADCAST already includes
    // the sender today.
    const std::vector<int> target_fds = session_manager_->getFdsInGuild(guild_id);
    const std::string user_id = session->user_id;

    const guild::Channel* active_channel =
        session->active_channel_id.empty() ? nullptr
                                           : guild_manager_->getChannel(session->active_channel_id);
    if (active_channel && active_channel->guild_id == guild_id) {
        session_manager_->clearActiveChannel(fd);
    }

    session_manager_->removeGuildMembership(fd, guild_id);
    guild_manager_->removeMember(guild_id, user_id);

    Message response;
    response.type = "MEMBER_LEFT";
    response.scope = Scope::TARGETED;
    response.target_fds = target_fds;
    response.payload =
        nlohmann::json{{"type", "MEMBER_LEFT"}, {"guild_id", guild_id}, {"user_id", user_id}};
    return response;
}

Message GuildHandler::handleDeleteGuild(const Message& message, int fd) const {
    if (message.type != "DELETE_GUILD") {
        return makeError("PROTOCOL_VIOLATION", "Expected DELETE_GUILD message");
    }

    if (!message.payload.is_object()) {
        return makeError("MALFORMED_MESSAGE", "DELETE_GUILD payload must be an object");
    }

    const session::Session* session = requireIdentified(fd);
    if (!session) {
        return makeError("NOT_IDENTIFIED", "Client must IDENTIFY before deleting a guild");
    }

    if (!message.payload.contains("guild_id") || !message.payload["guild_id"].is_string()) {
        return makeError("MALFORMED_MESSAGE", "DELETE_GUILD missing required field: guild_id");
    }

    if (!guild_manager_ || !internal_api_client_) {
        return makeError("INTERNAL_ERROR", "Guild context unavailable");
    }

    const std::string guild_id = message.payload["guild_id"].get<std::string>();
    if (!guild_manager_->hasGuild(guild_id)) {
        return makeError("GUILD_NOT_FOUND", "No guild with that id");
    }

    if (!guild_manager_->isOwner(guild_id, session->user_id)) {
        return makeError("NOT_GUILD_OWNER", "Only the guild owner can delete it");
    }

    if (!internal_api_client_->deleteGuild(guild_id)) {
        return makeError("INTERNAL_ERROR", "Failed to delete guild");
    }

    // Snapshot fds and channel ids before tearing anything down: deleteGuild
    // makes listChannels(guild_id) unqueryable afterward, and the fd list
    // must include everyone who was a member (including the owner) so they
    // all get the notification.
    const std::vector<int> target_fds = session_manager_->getFdsInGuild(guild_id);
    std::vector<std::string> channel_ids;
    for (const auto& c : guild_manager_->listChannels(guild_id)) {
        channel_ids.push_back(c.id);
    }

    guild_manager_->deleteGuild(guild_id);
    session_manager_->purgeGuildMembership(guild_id, channel_ids);

    Message response;
    response.type = "GUILD_DELETED";
    response.scope = Scope::TARGETED;
    response.target_fds = target_fds;
    response.payload = nlohmann::json{{"type", "GUILD_DELETED"}, {"guild_id", guild_id}};
    return response;
}

Message GuildHandler::handleListMembers(const Message& message, int fd) const {
    if (message.type != "LIST_MEMBERS") {
        return makeError("PROTOCOL_VIOLATION", "Expected LIST_MEMBERS message");
    }

    if (!message.payload.is_object()) {
        return makeError("MALFORMED_MESSAGE", "LIST_MEMBERS payload must be an object");
    }

    if (!requireIdentified(fd)) {
        return makeError("NOT_IDENTIFIED", "Client must IDENTIFY before listing members");
    }

    if (!message.payload.contains("guild_id") || !message.payload["guild_id"].is_string()) {
        return makeError("MALFORMED_MESSAGE", "LIST_MEMBERS missing required field: guild_id");
    }

    if (!guild_manager_ || !internal_api_client_) {
        return makeError("INTERNAL_ERROR", "Guild context unavailable");
    }

    const std::string guild_id = message.payload["guild_id"].get<std::string>();
    if (!guild_manager_->hasGuild(guild_id)) {
        return makeError("GUILD_NOT_FOUND", "No guild with that id");
    }

    // docs/guilds/social-presence-design.md §2.1: same precedent as LIST_CHANNELS —
    // roster is gated behind membership, one step further than bare
    // existence (already visible via LIST_GUILDS).
    if (!session_manager_->isMemberOfGuild(fd, guild_id)) {
        return makeError("NOT_GUILD_MEMBER", "Not a member of this guild");
    }

    // §2.3: a live read, never cached in GuildManager.
    const std::optional<std::vector<http::WireMember>> members =
        internal_api_client_->fetchGuildMembers(guild_id);
    if (!members) {
        return makeError("INTERNAL_ERROR", "Failed to fetch guild members");
    }

    nlohmann::json members_json = nlohmann::json::array();
    for (const auto& m : *members) {
        members_json.push_back(nlohmann::json{{"user_id", m.user_id},
                                              {"username", m.username},
                                              {"role_rank", m.role_rank},
                                              {"role_label", m.role_label},
                                              {"joined_at", m.joined_at}});
    }

    Message response;
    response.type = "MEMBER_LIST";
    response.payload = nlohmann::json{
        {"type", "MEMBER_LIST"}, {"guild_id", guild_id}, {"members", members_json}};
    return response;
}

Message GuildHandler::handleSetMemberRole(const Message& message, int fd) const {
    if (message.type != "SET_MEMBER_ROLE") {
        return makeError("PROTOCOL_VIOLATION", "Expected SET_MEMBER_ROLE message");
    }

    if (!message.payload.is_object()) {
        return makeError("MALFORMED_MESSAGE", "SET_MEMBER_ROLE payload must be an object");
    }

    const session::Session* session = requireIdentified(fd);
    if (!session) {
        return makeError("NOT_IDENTIFIED", "Client must IDENTIFY before setting a member's role");
    }

    if (!message.payload.contains("guild_id") || !message.payload["guild_id"].is_string()) {
        return makeError("MALFORMED_MESSAGE", "SET_MEMBER_ROLE missing required field: guild_id");
    }
    if (!message.payload.contains("user_id") || !message.payload["user_id"].is_string()) {
        return makeError("MALFORMED_MESSAGE", "SET_MEMBER_ROLE missing required field: user_id");
    }
    if (!message.payload.contains("role_rank") || !message.payload["role_rank"].is_number_integer()) {
        return makeError("MALFORMED_MESSAGE", "SET_MEMBER_ROLE missing required field: role_rank");
    }

    if (!guild_manager_ || !internal_api_client_) {
        return makeError("INTERNAL_ERROR", "Guild context unavailable");
    }

    const std::string guild_id = message.payload["guild_id"].get<std::string>();
    const std::string target_user_id = message.payload["user_id"].get<std::string>();
    const int role_rank = message.payload["role_rank"].get<int>();

    if (!guild_manager_->hasGuild(guild_id)) {
        return makeError("GUILD_NOT_FOUND", "No guild with that id");
    }

    // docs/guilds/social-presence-design.md §2.2/§2.4: canSetMemberRole is
    // owner-tier — NOT_GUILD_OWNER stays the right code for it, same as
    // canDeleteChannel/canDeleteGuild's owner-tier failures, unlike
    // canCreateChannel's officer-tier widening below.
    if (!guild_manager_->canSetMemberRole(guild_id, session->user_id)) {
        return makeError("NOT_GUILD_OWNER", "Only the guild owner can change member roles");
    }

    if (role_rank < 0 || role_rank >= guild::kOwnerRank) {
        return makeError("MALFORMED_MESSAGE",
                         "role_rank must be >= 0 and less than the owner rank");
    }

    // Structural guarantee that role_rank/isOwner never disagree (§2.2): the
    // owner's row is never a valid SET_MEMBER_ROLE target, regardless of
    // the requested rank.
    if (guild_manager_->isOwner(guild_id, target_user_id)) {
        return makeError("PROTOCOL_VIOLATION", "Cannot change the guild owner's role");
    }

    if (!guild_manager_->getMemberRank(guild_id, target_user_id).has_value()) {
        return makeError("NOT_GUILD_MEMBER", "Target user is not a member of this guild");
    }

    const std::optional<std::string> role_label =
        internal_api_client_->setMemberRole(guild_id, target_user_id, role_rank);
    if (!role_label) {
        return makeError("INTERNAL_ERROR", "Failed to persist member role");
    }

    guild_manager_->setMemberRank(guild_id, target_user_id, role_rank);

    Message response;
    response.type = "MEMBER_ROLE_UPDATED";
    response.scope = Scope::TARGETED;
    response.target_fds = session_manager_->getFdsInGuild(guild_id);
    response.payload = nlohmann::json{{"type", "MEMBER_ROLE_UPDATED"},
                                      {"guild_id", guild_id},
                                      {"user_id", target_user_id},
                                      {"role_rank", role_rank},
                                      {"role_label", *role_label}};
    return response;
}

Message GuildHandler::handleSetGuildVisibility(const Message& message, int fd) const {
    if (message.type != "SET_GUILD_VISIBILITY") {
        return makeError("PROTOCOL_VIOLATION", "Expected SET_GUILD_VISIBILITY message");
    }

    if (!message.payload.is_object()) {
        return makeError("MALFORMED_MESSAGE", "SET_GUILD_VISIBILITY payload must be an object");
    }

    const session::Session* session = requireIdentified(fd);
    if (!session) {
        return makeError("NOT_IDENTIFIED", "Client must IDENTIFY before changing guild visibility");
    }

    if (!message.payload.contains("guild_id") || !message.payload["guild_id"].is_string()) {
        return makeError("MALFORMED_MESSAGE", "SET_GUILD_VISIBILITY missing required field: guild_id");
    }
    if (!message.payload.contains("visibility") || !message.payload["visibility"].is_string()) {
        return makeError("MALFORMED_MESSAGE", "SET_GUILD_VISIBILITY missing required field: visibility");
    }

    const auto visibility = guild::guildVisibilityFromString(message.payload["visibility"].get<std::string>());
    if (!visibility) {
        return makeError("MALFORMED_MESSAGE",
                         "SET_GUILD_VISIBILITY visibility must be open, application, or private");
    }

    if (!guild_manager_ || !internal_api_client_) {
        return makeError("INTERNAL_ERROR", "Guild context unavailable");
    }

    const std::string guild_id = message.payload["guild_id"].get<std::string>();
    if (!guild_manager_->hasGuild(guild_id)) {
        return makeError("GUILD_NOT_FOUND", "No guild with that id");
    }

    // docs/guilds/social-presence-design.md §1.10/§5: captain-only, deliberately
    // not widened to officer-or-above — a guild-wide structural decision,
    // same reasoning DELETE_GUILD already uses.
    if (!guild_manager_->canSetGuildVisibility(guild_id, session->user_id)) {
        return makeError("NOT_GUILD_OWNER", "Only the guild owner can change guild visibility");
    }

    // Deleting pending join requests when leaving `application` mode
    // (§1.10's ARBITRATION) happens atomically inside this call, on the
    // Next.js/Postgres side — nothing further needed here.
    const std::optional<std::string> updated_visibility =
        internal_api_client_->setGuildVisibility(guild_id, guild::toString(*visibility));
    if (!updated_visibility) {
        return makeError("INTERNAL_ERROR", "Failed to persist guild visibility");
    }

    guild_manager_->setGuildVisibility(guild_id, *visibility);

    Message response;
    response.type = "GUILD_VISIBILITY_CHANGED";
    response.scope = Scope::TARGETED;
    response.target_fds = session_manager_->getFdsInGuild(guild_id);
    response.payload = nlohmann::json{
        {"type", "GUILD_VISIBILITY_CHANGED"}, {"guild_id", guild_id}, {"visibility", *updated_visibility}};
    return response;
}

} // namespace protocol
