#ifndef CIG_NEXUS_PROTOCOL_HANDLERS_HELLO_HANDLER_HPP
#define CIG_NEXUS_PROTOCOL_HANDLERS_HELLO_HANDLER_HPP

#include <string>

#include "protocol/Message.hpp"

namespace protocol {

class HelloHandler {
public:
    Message handle(const Message& message) const;
};

} // namespace protocol

#endif // CIG_NEXUS_PROTOCOL_HANDLERS_HELLO_HANDLER_HPP
