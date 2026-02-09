#include "protocol/MessageDispatcher.hpp"

#include <stdexcept>

namespace protocol {

void MessageDispatcher::registerHandler(const std::string& type, Handler handler) {
    handlers_[type] = std::move(handler);
}

Message MessageDispatcher::dispatch(const Message& message) const {
    const auto it = handlers_.find(message.type);
    if (it == handlers_.end()) {
        throw std::runtime_error("Unknown message type: " + message.type);
    }

    return it->second(message);
}

} // namespace protocol
