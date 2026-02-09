#include "protocol/handlers/HelloHandler.hpp"

#include "protocol/MessageBuilders.hpp"

namespace protocol {

namespace {

bool is_valid_client(const std::string& client) {
    return client == "web" || client == "desktop";
}

} // namespace

Message HelloHandler::handle(const Message& message) const {
    if (message.type != "HELLO") {
        Message response;
        response.type = "ERROR";
        response.payload = make_error("PROTOCOL_VIOLATION", "Expected HELLO message");
        return response;
    }

    if (!message.payload.is_object()) {
        Message response;
        response.type = "ERROR";
        response.payload = make_error("MALFORMED_MESSAGE", "HELLO payload must be an object");
        return response;
    }

    if (!message.payload.contains("type") ||
        !message.payload.contains("version") ||
        !message.payload.contains("client")) {
        Message response;
        response.type = "ERROR";
        response.payload = make_error("MALFORMED_MESSAGE", "HELLO missing required fields");
        return response;
    }

    if (!message.payload["type"].is_string() ||
        !message.payload["version"].is_string() ||
        !message.payload["client"].is_string()) {
        Message response;
        response.type = "ERROR";
        response.payload = make_error("MALFORMED_MESSAGE", "HELLO fields must be strings");
        return response;
    }

    if (message.payload["type"].get<std::string>() != "HELLO") {
        Message response;
        response.type = "ERROR";
        response.payload = make_error("PROTOCOL_VIOLATION", "HELLO type mismatch");
        return response;
    }

    const std::string version = message.payload["version"].get<std::string>();
    if (version != "0.1") {
        Message response;
        response.type = "ERROR";
        response.payload = make_error("UNSUPPORTED_VERSION", "Protocol version not supported");
        return response;
    }

    const std::string client = message.payload["client"].get<std::string>();
    if (!is_valid_client(client)) {
        Message response;
        response.type = "ERROR";
        response.payload = make_error("PROTOCOL_VIOLATION", "Unsupported client type");
        return response;
    }

    Message response;
    response.type = "WELCOME";
    response.payload = make_welcome("", "0.1");
    return response;
}

} // namespace protocol
