#ifndef CIG_NEXUS_PROTOCOL_MESSAGE_BUILDERS_HPP
#define CIG_NEXUS_PROTOCOL_MESSAGE_BUILDERS_HPP

#include <string>

#include <nlohmann/json.hpp>

namespace protocol {

inline nlohmann::json make_welcome(const std::string& session_id,
                                   const std::string& server_version) {
    return nlohmann::json{
        {"type", "WELCOME"},
        {"session_id", session_id},
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
