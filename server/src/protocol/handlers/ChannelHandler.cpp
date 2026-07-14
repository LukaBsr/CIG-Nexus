#include "protocol/handlers/ChannelHandler.hpp"

#include "guild/Channel.hpp"
#include "guild/GuildManager.hpp"
#include "protocol/MessageBuilders.hpp"
#include "session/SessionManager.hpp"

#include <ctime>

namespace protocol {

void ChannelHandler::setSessionManager(session::SessionManager* session_manager) {
    session_manager_ = session_manager;
}

void ChannelHandler::setGuildManager(guild::GuildManager* guild_manager) {
    guild_manager_ = guild_manager;
}

Message ChannelHandler::makeError(const std::string& code, const std::string& msg) {
    Message response;
    response.type = "ERROR";
    response.payload = make_error(code, msg);
    return response;
}

const session::Session* ChannelHandler::requireIdentified(int fd) const {
    if (!session_manager_) {
        return nullptr;
    }

    const session::Session* session = session_manager_->getSession(fd);
    if (!session || session->username.empty()) {
        return nullptr;
    }

    return session;
}

Message ChannelHandler::handleListChannels(const Message& message, int fd) const {
    if (message.type != "LIST_CHANNELS") {
        return makeError("PROTOCOL_VIOLATION", "Expected LIST_CHANNELS message");
    }

    if (!message.payload.is_object()) {
        return makeError("MALFORMED_MESSAGE", "LIST_CHANNELS payload must be an object");
    }

    if (!requireIdentified(fd)) {
        return makeError("NOT_IDENTIFIED", "Client must IDENTIFY before listing channels");
    }

    if (!message.payload.contains("guild_id") || !message.payload["guild_id"].is_string()) {
        return makeError("MALFORMED_MESSAGE", "LIST_CHANNELS missing required field: guild_id");
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

    nlohmann::json channels = nlohmann::json::array();
    for (const auto& c : guild_manager_->listChannels(guild_id)) {
        channels.push_back(
            {{"channel_id", c.id}, {"name", c.name}, {"channel_type", guild::toString(c.type)}});
    }

    Message response;
    response.type = "CHANNEL_LIST";
    response.payload =
        nlohmann::json{{"type", "CHANNEL_LIST"}, {"guild_id", guild_id}, {"channels", channels}};
    return response;
}

Message ChannelHandler::handleCreateChannel(const Message& message, int fd) const {
    if (message.type != "CREATE_CHANNEL") {
        return makeError("PROTOCOL_VIOLATION", "Expected CREATE_CHANNEL message");
    }

    if (!message.payload.is_object()) {
        return makeError("MALFORMED_MESSAGE", "CREATE_CHANNEL payload must be an object");
    }

    const session::Session* session = requireIdentified(fd);
    if (!session) {
        return makeError("NOT_IDENTIFIED", "Client must IDENTIFY before creating a channel");
    }

    if (!message.payload.contains("guild_id") || !message.payload["guild_id"].is_string()) {
        return makeError("MALFORMED_MESSAGE", "CREATE_CHANNEL missing required field: guild_id");
    }

    if (!message.payload.contains("name") || !message.payload["name"].is_string()) {
        return makeError("MALFORMED_MESSAGE", "CREATE_CHANNEL missing required field: name");
    }

    const std::string name = message.payload["name"].get<std::string>();
    if (name.empty()) {
        return makeError("MALFORMED_MESSAGE", "CREATE_CHANNEL name must not be empty");
    }

    if (name.length() > 64) {
        return makeError("MALFORMED_MESSAGE", "CREATE_CHANNEL name must be <= 64 characters");
    }

    if (!message.payload.contains("channel_type") || !message.payload["channel_type"].is_string()) {
        return makeError("MALFORMED_MESSAGE", "CREATE_CHANNEL missing required field: channel_type");
    }

    const std::string channel_type_str = message.payload["channel_type"].get<std::string>();
    guild::ChannelType channel_type;
    if (channel_type_str == "TEXT") {
        channel_type = guild::ChannelType::TEXT;
    } else if (channel_type_str == "VOICE") {
        channel_type = guild::ChannelType::VOICE;
    } else {
        return makeError("MALFORMED_MESSAGE", "CREATE_CHANNEL channel_type must be TEXT or VOICE");
    }

    if (!guild_manager_) {
        return makeError("INTERNAL_ERROR", "Guild context unavailable");
    }

    const std::string guild_id = message.payload["guild_id"].get<std::string>();
    if (!guild_manager_->hasGuild(guild_id)) {
        return makeError("GUILD_NOT_FOUND", "No guild with that id");
    }

    if (!guild_manager_->canCreateChannel(guild_id, session->user_id)) {
        return makeError("NOT_GUILD_OWNER", "Only the guild owner can create channels");
    }

    guild::Channel& new_channel = guild_manager_->createChannel(guild_id, name, channel_type);

    Message response;
    response.type = "CHANNEL_CREATED";
    response.scope = Scope::TARGETED;
    response.target_fds = session_manager_->getFdsInGuild(guild_id);
    response.payload = nlohmann::json{{"type", "CHANNEL_CREATED"},
                                      {"guild_id", guild_id},
                                      {"channel_id", new_channel.id},
                                      {"name", new_channel.name},
                                      {"channel_type", guild::toString(new_channel.type)}};
    return response;
}

Message ChannelHandler::handleDeleteChannel(const Message& message, int fd) const {
    if (message.type != "DELETE_CHANNEL") {
        return makeError("PROTOCOL_VIOLATION", "Expected DELETE_CHANNEL message");
    }

    if (!message.payload.is_object()) {
        return makeError("MALFORMED_MESSAGE", "DELETE_CHANNEL payload must be an object");
    }

    const session::Session* session = requireIdentified(fd);
    if (!session) {
        return makeError("NOT_IDENTIFIED", "Client must IDENTIFY before deleting a channel");
    }

    if (!message.payload.contains("guild_id") || !message.payload["guild_id"].is_string()) {
        return makeError("MALFORMED_MESSAGE", "DELETE_CHANNEL missing required field: guild_id");
    }

    if (!message.payload.contains("channel_id") || !message.payload["channel_id"].is_string()) {
        return makeError("MALFORMED_MESSAGE", "DELETE_CHANNEL missing required field: channel_id");
    }

    if (!guild_manager_) {
        return makeError("INTERNAL_ERROR", "Guild context unavailable");
    }

    const std::string guild_id = message.payload["guild_id"].get<std::string>();
    if (!guild_manager_->hasGuild(guild_id)) {
        return makeError("GUILD_NOT_FOUND", "No guild with that id");
    }

    const std::string channel_id = message.payload["channel_id"].get<std::string>();
    const guild::Channel* channel = guild_manager_->getChannel(channel_id);
    if (!channel || channel->guild_id != guild_id) {
        return makeError("CHANNEL_NOT_FOUND", "No channel with that id in this guild");
    }

    if (!guild_manager_->canDeleteChannel(guild_id, session->user_id)) {
        return makeError("NOT_GUILD_OWNER", "Only the guild owner can delete channels");
    }

    const std::vector<int> target_fds = session_manager_->getFdsInGuild(guild_id);

    guild_manager_->deleteChannel(channel_id);
    session_manager_->clearActiveChannelEverywhere(channel_id);

    Message response;
    response.type = "CHANNEL_DELETED";
    response.scope = Scope::TARGETED;
    response.target_fds = target_fds;
    response.payload =
        nlohmann::json{{"type", "CHANNEL_DELETED"}, {"guild_id", guild_id}, {"channel_id", channel_id}};
    return response;
}

Message ChannelHandler::handleJoinChannel(const Message& message, int fd) const {
    if (message.type != "JOIN_CHANNEL") {
        return makeError("PROTOCOL_VIOLATION", "Expected JOIN_CHANNEL message");
    }

    if (!message.payload.is_object()) {
        return makeError("MALFORMED_MESSAGE", "JOIN_CHANNEL payload must be an object");
    }

    if (!requireIdentified(fd)) {
        return makeError("NOT_IDENTIFIED", "Client must IDENTIFY before joining a channel");
    }

    if (!message.payload.contains("channel_id") || !message.payload["channel_id"].is_string()) {
        return makeError("MALFORMED_MESSAGE", "JOIN_CHANNEL missing required field: channel_id");
    }

    if (!guild_manager_) {
        return makeError("INTERNAL_ERROR", "Guild context unavailable");
    }

    const std::string channel_id = message.payload["channel_id"].get<std::string>();
    const guild::Channel* channel = guild_manager_->getChannel(channel_id);
    if (!channel) {
        return makeError("CHANNEL_NOT_FOUND", "No channel with that id");
    }

    if (!session_manager_->isMemberOfGuild(fd, channel->guild_id)) {
        return makeError("NOT_GUILD_MEMBER", "Not a member of this channel's guild");
    }

    if (channel->type != guild::ChannelType::TEXT) {
        return makeError("PROTOCOL_VIOLATION", "Voice channels are not joinable this iteration");
    }

    // Implicitly leaves any previously active channel: a connection has at
    // most one active channel, so this is a plain overwrite.
    session_manager_->setActiveChannel(fd, channel_id);

    Message response;
    response.type = "CHANNEL_JOINED";
    response.payload = nlohmann::json{
        {"type", "CHANNEL_JOINED"}, {"guild_id", channel->guild_id}, {"channel_id", channel_id}};
    return response;
}

Message ChannelHandler::handleLeaveChannel(const Message& message, int fd) const {
    if (message.type != "LEAVE_CHANNEL") {
        return makeError("PROTOCOL_VIOLATION", "Expected LEAVE_CHANNEL message");
    }

    const session::Session* session = requireIdentified(fd);
    if (!session) {
        return makeError("NOT_IDENTIFIED", "Client must IDENTIFY before leaving a channel");
    }

    if (session->active_channel_id.empty()) {
        return makeError("NOT_IN_CHANNEL", "Not currently in a channel");
    }

    const std::string channel_id = session->active_channel_id;
    session_manager_->clearActiveChannel(fd);

    Message response;
    response.type = "CHANNEL_LEFT";
    response.payload = nlohmann::json{{"type", "CHANNEL_LEFT"}, {"channel_id", channel_id}};
    return response;
}

Message ChannelHandler::handleChannelMessage(const Message& message, int fd) const {
    if (message.type != "CHANNEL_MESSAGE") {
        return makeError("PROTOCOL_VIOLATION", "Expected CHANNEL_MESSAGE message");
    }

    if (!message.payload.is_object()) {
        return makeError("MALFORMED_MESSAGE", "CHANNEL_MESSAGE payload must be an object");
    }

    const session::Session* session = requireIdentified(fd);
    if (!session) {
        return makeError("NOT_IDENTIFIED", "Client must IDENTIFY before sending channel messages");
    }

    if (session->active_channel_id.empty()) {
        return makeError("NOT_IN_CHANNEL", "Not currently in a channel");
    }

    if (!message.payload.contains("content") || !message.payload["content"].is_string()) {
        return makeError("MALFORMED_MESSAGE", "CHANNEL_MESSAGE missing required field: content");
    }

    const std::string content = message.payload["content"].get<std::string>();
    if (content.empty()) {
        return makeError("MALFORMED_MESSAGE", "CHANNEL_MESSAGE content must not be empty");
    }

    if (content.length() > 500) {
        return makeError("MALFORMED_MESSAGE", "CHANNEL_MESSAGE content must be <= 500 characters");
    }

    if (!guild_manager_) {
        return makeError("INTERNAL_ERROR", "Guild context unavailable");
    }

    const std::string channel_id = session->active_channel_id;
    const guild::Channel* channel = guild_manager_->getChannel(channel_id);
    if (!channel) {
        return makeError("CHANNEL_NOT_FOUND", "Active channel no longer exists");
    }

    const int message_id = ++message_counter_;
    const std::time_t timestamp = std::time(nullptr);

    Message response;
    response.type = "CHANNEL_MESSAGE";
    response.scope = Scope::TARGETED;
    response.target_fds = session_manager_->getFdsWithActiveChannel(channel_id);
    response.payload = nlohmann::json{{"type", "CHANNEL_MESSAGE"},
                                      {"channel_id", channel_id},
                                      {"guild_id", channel->guild_id},
                                      {"message_id", message_id},
                                      {"timestamp", static_cast<long>(timestamp)},
                                      {"user_id", session->user_id},
                                      {"username", session->username},
                                      {"content", content}};
    return response;
}

} // namespace protocol
