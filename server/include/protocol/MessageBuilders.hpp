#ifndef CIG_NEXUS_PROTOCOL_MESSAGE_BUILDERS_HPP
#define CIG_NEXUS_PROTOCOL_MESSAGE_BUILDERS_HPP

#include "http/InternalApiClient.hpp"

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace protocol {

inline nlohmann::json make_welcome(const std::string& server_version) {
    return nlohmann::json{{"type", "WELCOME"}, {"server_version", server_version}};
}

inline nlohmann::json make_error(const std::string& code, const std::string& message) {
    return nlohmann::json{{"type", "ERROR"}, {"code", code}, {"message", message}};
}

// docs/social/friends-dms-design.md §4.5: display_name/avatar_url are
// present-but-possibly-null on the wire (matching the max_uses/expires_at
// precedent in InviteHandler.cpp), not omitted when unset — a client can
// then always destructure the key rather than checking for its presence.
inline nlohmann::json make_optional_string(const std::optional<std::string>& value) {
    return value.has_value() ? nlohmann::json(*value) : nullptr;
}

// §4.5: the FETCH_HISTORY message-loop JSON was previously duplicated
// verbatim between ChannelHandler.cpp (lobby/channel scope) and
// DMHandler.cpp (DM scope) — extracted here so the two new fields (and any
// future WireMessage field) are added once, not twice.
inline nlohmann::json make_history_message(const http::WireMessage& message) {
    return nlohmann::json{{"message_id", message.message_id},
                          {"timestamp", message.timestamp},
                          {"user_id", message.user_id},
                          {"username", message.username},
                          {"content", message.content},
                          {"display_name", make_optional_string(message.display_name)},
                          {"avatar_url", make_optional_string(message.avatar_url)}};
}

} // namespace protocol

#endif // CIG_NEXUS_PROTOCOL_MESSAGE_BUILDERS_HPP
