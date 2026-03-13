#include "protocol/handlers/IdentifyHandler.hpp"

#include "protocol/MessageBuilders.hpp"
#include "session/SessionManager.hpp"

#include <string>

namespace protocol {

void IdentifyHandler::setSessionManager(session::SessionManager* session_manager) {
    session_manager_ = session_manager;
}

Message IdentifyHandler::handle(const Message& message, int fd) {
    auto makeError = [](const std::string& code, const std::string& msg) {
        Message response;
        response.type = "ERROR";
        response.payload = make_error(code, msg);
        return response;
    };

    if (message.type != "IDENTIFY") {
        return makeError("PROTOCOL_VIOLATION", "Expected IDENTIFY message");
    }

    if (!message.payload.is_object()) {
        return makeError("MALFORMED_MESSAGE", "IDENTIFY payload must be an object");
    }

    if (!message.payload.contains("username")) {
        return makeError("MALFORMED_MESSAGE", "IDENTIFY missing required field: username");
    }

    if (!message.payload["username"].is_string()) {
        return makeError("MALFORMED_MESSAGE", "IDENTIFY username must be a string");
    }

    const std::string username = message.payload["username"].get<std::string>();
    if (username.empty()) {
        return makeError("MALFORMED_MESSAGE", "IDENTIFY username must not be empty");
    }

    if (username.length() > 32) {
        return makeError("MALFORMED_MESSAGE", "IDENTIFY username must be <= 32 characters");
    }

    if (!session_manager_) {
        return makeError("INTERNAL_ERROR", "Session context unavailable");
    }

    if (session_manager_->hasSession(fd)) {
        return makeError("PROTOCOL_VIOLATION", "Connection is already identified");
    }

    session::Session& session = session_manager_->createSession(fd);
    session.username = username;

    Message response;
    response.type = "IDENTIFIED";
    response.payload = nlohmann::json{
        {"user_id", session.user_id},
        {"username", session.username}
    };
    return response;
}

} // namespace protocol
