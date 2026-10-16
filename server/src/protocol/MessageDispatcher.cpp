#include "protocol/MessageDispatcher.hpp"

#include "protocol/MessageBuilders.hpp"

#include <stdexcept>

namespace protocol {

void MessageDispatcher::registerHandler(const std::string& type, Handler handler) {
    handlers_[type] = std::move(handler);
}

std::vector<Message> MessageDispatcher::dispatch(const Message& message, int fd) const {
    const auto it = handlers_.find(message.type);
    if (it == handlers_.end()) {
        throw std::runtime_error("Unknown message type: " + message.type);
    }

    std::vector<Message> responses = it->second(message, fd);
    for (Message& response : responses) {
        tag_error_with_request_type(response, message.type);
    }
    return responses;
}

} // namespace protocol
