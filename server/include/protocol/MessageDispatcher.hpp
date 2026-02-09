#ifndef CIG_NEXUS_PROTOCOL_MESSAGE_DISPATCHER_HPP
#define CIG_NEXUS_PROTOCOL_MESSAGE_DISPATCHER_HPP

#include <functional>
#include <string>
#include <unordered_map>

#include "Message.hpp"

namespace protocol {

class MessageDispatcher {
public:
    using Handler = std::function<Message(const Message&)>;

    void registerHandler(const std::string& type, Handler handler);
    Message dispatch(const Message& message) const;

private:
    std::unordered_map<std::string, Handler> handlers_;
};

} // namespace protocol

#endif // CIG_NEXUS_PROTOCOL_MESSAGE_DISPATCHER_HPP
