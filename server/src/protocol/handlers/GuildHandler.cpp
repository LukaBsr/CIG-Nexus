#include "protocol/handlers/GuildHandler.hpp"

#include "guild/Channel.hpp"
#include "guild/GuildManager.hpp"
#include "protocol/MessageBuilders.hpp"
#include "session/SessionManager.hpp"

namespace protocol {

void GuildHandler::setSessionManager(session::SessionManager* session_manager) {
    session_manager_ = session_manager;
}

void GuildHandler::setGuildManager(guild::GuildManager* guild_manager) {
    guild_manager_ = guild_manager;
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

    if (!guild_manager_) {
        return makeError("INTERNAL_ERROR", "Guild context unavailable");
    }

    guild::Guild& new_guild = guild_manager_->createGuild(name, session->user_id);
    session_manager_->addGuildMembership(fd, new_guild.id);

    Message response;
    response.type = "GUILD_CREATED";
    response.payload = nlohmann::json{{"type", "GUILD_CREATED"},
                                      {"guild_id", new_guild.id},
                                      {"name", new_guild.name},
                                      {"owner_id", new_guild.owner_id}};
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

    nlohmann::json guilds = nlohmann::json::array();
    for (const auto& g : guild_manager_->listGuilds()) {
        guilds.push_back({{"guild_id", g.id}, {"name", g.name}, {"owner_id", g.owner_id}});
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

    if (!guild_manager_) {
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

    session_manager_->addGuildMembership(fd, guild_id);

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

    if (!guild_manager_) {
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

    if (!guild_manager_) {
        return makeError("INTERNAL_ERROR", "Guild context unavailable");
    }

    const std::string guild_id = message.payload["guild_id"].get<std::string>();
    if (!guild_manager_->hasGuild(guild_id)) {
        return makeError("GUILD_NOT_FOUND", "No guild with that id");
    }

    if (!guild_manager_->isOwner(guild_id, session->user_id)) {
        return makeError("NOT_GUILD_OWNER", "Only the guild owner can delete it");
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

} // namespace protocol
