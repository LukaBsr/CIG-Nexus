#ifndef CIG_NEXUS_PROTOCOL_HANDLERS_CHAT_HANDLER_HPP
#define CIG_NEXUS_PROTOCOL_HANDLERS_CHAT_HANDLER_HPP

#include "protocol/Message.hpp"

namespace protocol {

class ChatHandler {
public:
    Message handle(const Message& message) const;
};

} // namespace protocol

#endif // CIG_NEXUS_PROTOCOL_HANDLERS_CHAT_HANDLER_HPP
