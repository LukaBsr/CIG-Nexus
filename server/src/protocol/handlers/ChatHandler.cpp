#include "protocol/handlers/ChatHandler.hpp"

#include "protocol/MessageBuilders.hpp"
#include "session/SessionManager.hpp"

#include <ctime>
#include <string>

namespace protocol {

void ChatHandler::setSessionManager(session::SessionManager* session_manager) {
    session_manager_ = session_manager;
}

Message ChatHandler::handle(const Message& message, int fd) const {
    auto makeError = [](const std::string& code, const std::string& msg) {
        Message response;
        response.type = "ERROR";
        response.payload = make_error(code, msg);
        return response;
    };

    if (message.type != "CHAT_MESSAGE") {
        return makeError("PROTOCOL_VIOLATION", "Expected CHAT_MESSAGE message");
    }

    if (!message.payload.is_object()) {
        return makeError("MALFORMED_MESSAGE", "CHAT_MESSAGE payload must be an object");
    }

    if (!message.payload.contains("type")) {
        return makeError("PROTOCOL_VIOLATION", "CHAT_MESSAGE missing field: type");
    }

    if (!message.payload["type"].is_string()) {
        return makeError("PROTOCOL_VIOLATION", "CHAT_MESSAGE type must be a string");
    }

    if (message.payload["type"].get<std::string>() != "CHAT_MESSAGE") {
        return makeError("PROTOCOL_VIOLATION", "CHAT_MESSAGE type mismatch");
    }

    if (!message.payload.contains("content")) {
        return makeError("MALFORMED_MESSAGE", "CHAT_MESSAGE missing required field: content");
    }

    if (!message.payload["content"].is_string()) {
        return makeError("MALFORMED_MESSAGE", "CHAT_MESSAGE content must be a string");
    }

    const std::string content = message.payload["content"].get<std::string>();

    if (content.empty()) {
        return makeError("MALFORMED_MESSAGE", "CHAT_MESSAGE content must not be empty");
    }

    if (content.length() > 500) {
        return makeError("MALFORMED_MESSAGE", "CHAT_MESSAGE content must be <= 500 characters");
    }

    static int message_counter = 0;
    const int message_id = ++message_counter;
    const std::time_t timestamp = std::time(nullptr);

    if (!session_manager_) {
        Message response;
        response.type = "CHAT_MESSAGE";
        response.scope = Scope::BROADCAST;
        response.payload = nlohmann::json{
            {"type", "CHAT_MESSAGE"},
            {"message_id", message_id},
            {"timestamp", static_cast<long>(timestamp)},
            {"from", "anonymous"},
            {"content", content}
        };
        return response;
    }

    const session::Session* session = session_manager_->getSession(fd);
    if (!session) {
        return makeError("NOT_IDENTIFIED", "Client must IDENTIFY before sending chat messages");
    }

    if (session->username.empty()) {
        return makeError("NOT_IDENTIFIED", "Client must IDENTIFY before sending chat messages");
    }

    Message response;
    response.type = "CHAT_MESSAGE";
    response.scope = Scope::BROADCAST;
    response.payload = nlohmann::json{
        {"type", "CHAT_MESSAGE"},
        {"message_id", message_id},
        {"timestamp", static_cast<long>(timestamp)},
        {"user_id", session->user_id},
        {"username", session->username},
        {"content", content}
    };
    return response;
}

} // namespace protocol
