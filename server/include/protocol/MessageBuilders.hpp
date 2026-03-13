#ifndef CIG_NEXUS_PROTOCOL_MESSAGE_BUILDERS_HPP
#define CIG_NEXUS_PROTOCOL_MESSAGE_BUILDERS_HPP

#include <string>

#include <nlohmann/json.hpp>

namespace protocol {

inline nlohmann::json make_welcome(const std::string& server_version) {
    return nlohmann::json{
        {"type", "WELCOME"},
        {"server_version", server_version}
    };
}

inline nlohmann::json make_error(const std::string& code,
                                 const std::string& message) {
    return nlohmann::json{
        {"type", "ERROR"},
        {"code", code},
        {"message", message}
    };
}

} // namespace protocol

#endif // CIG_NEXUS_PROTOCOL_MESSAGE_BUILDERS_HPP
