#ifndef CIG_NEXUS_HTTP_WIRE_PARSERS_HPP
#define CIG_NEXUS_HTTP_WIRE_PARSERS_HPP

// Internal to src/http: JSON -> Wire* parsers and small request helpers used
// by CurlInternalApiClient.cpp. Not part of the public include/ tree.

#include "http/InternalApiClient.hpp"

#include <cstddef>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace http {

size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata);
void parseProfileFields(const nlohmann::json& json, std::optional<std::string>& display_name,
                        std::optional<std::string>& avatar_url);
std::optional<WireGuild> parseWireGuild(const nlohmann::json& json);
std::optional<WireChannel> parseWireChannel(const nlohmann::json& json);
std::optional<WireMessage> parseWireMessage(const nlohmann::json& json);
std::optional<WireMember> parseWireMember(const nlohmann::json& json);
std::optional<WireInvite> parseWireInvite(const nlohmann::json& json);
std::optional<WireJoinRequest> parseWireJoinRequest(const nlohmann::json& json);
RedeemInviteResult parseRedeemInviteResult(const nlohmann::json& json);
std::optional<WireFriend> parseWireFriend(const nlohmann::json& json);
std::optional<WireFriendRequest> parseWireFriendRequest(const nlohmann::json& json);
SendFriendRequestResult parseSendFriendRequestResult(const nlohmann::json& json);
std::optional<WireBlock> parseWireBlock(const nlohmann::json& json);
std::string urlEncode(const std::string& value);

} // namespace http

#endif // CIG_NEXUS_HTTP_WIRE_PARSERS_HPP
