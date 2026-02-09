#ifndef CIG_NEXUS_PROTOCOL_MESSAGE_PARSER_HPP
#define CIG_NEXUS_PROTOCOL_MESSAGE_PARSER_HPP

#include <cstdint>
#include <vector>

#include "protocol/Message.hpp"

namespace protocol {

Message parse_message(const std::vector<uint8_t>& frame);

} // namespace protocol

#endif // CIG_NEXUS_PROTOCOL_MESSAGE_PARSER_HPP
