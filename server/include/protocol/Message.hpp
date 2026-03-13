#ifndef CIG_NEXUS_PROTOCOL_MESSAGE_HPP
#define CIG_NEXUS_PROTOCOL_MESSAGE_HPP

#include <string>

#include <nlohmann/json.hpp>

namespace protocol {

enum class Scope {
    DIRECT,
    BROADCAST
};

struct Message {
    std::string type;
    nlohmann::json payload;
    Scope scope = Scope::DIRECT;
};

} // namespace protocol

#endif // CIG_NEXUS_PROTOCOL_MESSAGE_HPP
