#ifndef CIG_NEXUS_PROTOCOL_MESSAGE_HPP
#define CIG_NEXUS_PROTOCOL_MESSAGE_HPP

#include <string>

#include <nlohmann/json.hpp>

namespace protocol {

struct Message {
    std::string type;
    nlohmann::json payload;
};

} // namespace protocol

#endif // CIG_NEXUS_PROTOCOL_MESSAGE_HPP
