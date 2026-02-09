#include "protocol/MessageParser.hpp"

#include <stdexcept>
#include <string>

namespace protocol {

Message parse_message(const std::vector<uint8_t>& frame) {
    const std::string json_text(frame.begin(), frame.end());

    nlohmann::json payload;
    try {
        payload = nlohmann::json::parse(json_text);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Invalid JSON payload: ") + e.what());
    }

    if (!payload.is_object()) {
        throw std::runtime_error("JSON payload must be an object");
    }

    if (!payload.contains("type") || !payload["type"].is_string()) {
        throw std::runtime_error("Missing or invalid message type");
    }

    Message message;
    message.type = payload["type"].get<std::string>();
    message.payload = std::move(payload);
    return message;
}

} // namespace protocol
