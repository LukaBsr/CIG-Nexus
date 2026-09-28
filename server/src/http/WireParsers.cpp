#include "WireParsers.hpp"

#include <curl/curl.h>

namespace http {

size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

// docs/social/friends-dms-design.md §4.5: display_name/avatar_url are
// optional on every roster/message/list entry that carries them — absent
// (not just null) is tolerated the same way last_message_at/dm_seq already
// are elsewhere in this file, since not every internal API response is
// guaranteed to populate them.
void parseProfileFields(const nlohmann::json& json, std::optional<std::string>& display_name,
                        std::optional<std::string>& avatar_url) {
    if (json.contains("display_name") && json["display_name"].is_string()) {
        display_name = json["display_name"].get<std::string>();
    }
    if (json.contains("avatar_url") && json["avatar_url"].is_string()) {
        avatar_url = json["avatar_url"].get<std::string>();
    }
}

std::optional<WireGuild> parseWireGuild(const nlohmann::json& json) {
    if (!json.is_object() || !json.contains("guild_id") || !json.contains("name") ||
        !json.contains("owner_id") || !json.contains("visibility")) {
        return std::nullopt;
    }
    if (!json["guild_id"].is_string() || !json["name"].is_string() ||
        !json["owner_id"].is_string() || !json["visibility"].is_string()) {
        return std::nullopt;
    }
    return WireGuild{json["guild_id"].get<std::string>(), json["name"].get<std::string>(),
                     json["owner_id"].get<std::string>(), json["visibility"].get<std::string>()};
}

std::optional<WireChannel> parseWireChannel(const nlohmann::json& json) {
    if (!json.is_object() || !json.contains("channel_id") || !json.contains("guild_id") ||
        !json.contains("name") || !json.contains("channel_type")) {
        return std::nullopt;
    }
    if (!json["channel_id"].is_string() || !json["guild_id"].is_string() ||
        !json["name"].is_string() || !json["channel_type"].is_string()) {
        return std::nullopt;
    }
    return WireChannel{json["channel_id"].get<std::string>(), json["guild_id"].get<std::string>(),
                       json["name"].get<std::string>(), json["channel_type"].get<std::string>()};
}

std::optional<WireMessage> parseWireMessage(const nlohmann::json& json) {
    if (!json.is_object() || !json.contains("message_id") || !json.contains("channel_id") ||
        !json.contains("timestamp") || !json.contains("user_id") || !json.contains("username") ||
        !json.contains("content")) {
        return std::nullopt;
    }
    if (!json["message_id"].is_number_integer() ||
        !(json["channel_id"].is_string() || json["channel_id"].is_null()) ||
        !json["timestamp"].is_number_integer() || !json["user_id"].is_string() ||
        !json["username"].is_string() || !json["content"].is_string()) {
        return std::nullopt;
    }

    WireMessage message;
    message.message_id = json["message_id"].get<int>();
    message.channel_id = json["channel_id"].is_string()
                             ? std::make_optional(json["channel_id"].get<std::string>())
                             : std::nullopt;
    message.timestamp = json["timestamp"].get<long>();
    message.user_id = json["user_id"].get<std::string>();
    message.username = json["username"].get<std::string>();
    message.content = json["content"].get<std::string>();
    parseProfileFields(json, message.display_name, message.avatar_url);
    return message;
}

std::optional<WireMember> parseWireMember(const nlohmann::json& json) {
    if (!json.is_object() || !json.contains("user_id") || !json.contains("username") ||
        !json.contains("role_rank") || !json.contains("role_label") ||
        !json.contains("joined_at")) {
        return std::nullopt;
    }
    if (!json["user_id"].is_string() || !json["username"].is_string() ||
        !json["role_rank"].is_number_integer() || !json["role_label"].is_string() ||
        !json["joined_at"].is_string()) {
        return std::nullopt;
    }
    WireMember member{json["user_id"].get<std::string>(),
                      json["username"].get<std::string>(),
                      json["role_rank"].get<int>(),
                      json["role_label"].get<std::string>(),
                      json["joined_at"].get<std::string>(),
                      std::nullopt,
                      std::nullopt};
    parseProfileFields(json, member.display_name, member.avatar_url);
    return member;
}

std::optional<WireInvite> parseWireInvite(const nlohmann::json& json) {
    if (!json.is_object() || !json.contains("code") || !json.contains("max_uses") ||
        !json.contains("use_count") || !json.contains("expires_at") ||
        !json.contains("revoked_at") || !json.contains("created_at")) {
        return std::nullopt;
    }
    if (!json["code"].is_string() || !json["use_count"].is_number_integer() ||
        !(json["max_uses"].is_number_integer() || json["max_uses"].is_null()) ||
        !(json["expires_at"].is_string() || json["expires_at"].is_null()) ||
        !(json["revoked_at"].is_string() || json["revoked_at"].is_null()) ||
        !json["created_at"].is_string()) {
        return std::nullopt;
    }

    WireInvite invite;
    invite.code = json["code"].get<std::string>();
    invite.max_uses = json["max_uses"].is_number_integer()
                          ? std::make_optional(json["max_uses"].get<int>())
                          : std::nullopt;
    invite.use_count = json["use_count"].get<int>();
    invite.expires_at = json["expires_at"].is_string()
                            ? std::make_optional(json["expires_at"].get<std::string>())
                            : std::nullopt;
    invite.revoked_at = json["revoked_at"].is_string()
                            ? std::make_optional(json["revoked_at"].get<std::string>())
                            : std::nullopt;
    invite.created_at = json["created_at"].get<std::string>();
    return invite;
}

std::optional<WireJoinRequest> parseWireJoinRequest(const nlohmann::json& json) {
    if (!json.is_object() || !json.contains("user_id") || !json.contains("username") ||
        !json.contains("requested_at")) {
        return std::nullopt;
    }
    if (!json["user_id"].is_string() || !json["username"].is_string() ||
        !json["requested_at"].is_string()) {
        return std::nullopt;
    }
    WireJoinRequest request{json["user_id"].get<std::string>(), json["username"].get<std::string>(),
                            json["requested_at"].get<std::string>(), std::nullopt, std::nullopt};
    parseProfileFields(json, request.display_name, request.avatar_url);
    return request;
}

// §1.3/§1.5's RedeemInviteResult discriminated union, mirrored from
// web/lib/internal/invites.ts's shape.
RedeemInviteResult parseRedeemInviteResult(const nlohmann::json& json) {
    RedeemInviteResult result;
    if (!json.is_object() || !json.contains("ok") || !json["ok"].is_boolean()) {
        return result; // ok=false, error=FAILED (default-constructed)
    }
    result.ok = json["ok"].get<bool>();

    if (result.ok) {
        if (!json.contains("kind") || !json["kind"].is_string() || !json.contains("guild_id") ||
            !json["guild_id"].is_string()) {
            return RedeemInviteResult{}; // malformed success body — treat as failure
        }
        result.guild_id = json["guild_id"].get<std::string>();
        result.is_join_request = json["kind"].get<std::string>() == "join_request";
        if (!result.is_join_request && json.contains("role_rank") &&
            json["role_rank"].is_number_integer()) {
            result.role_rank = json["role_rank"].get<int>();
        }
        return result;
    }

    const std::string error =
        json.contains("error") && json["error"].is_string() ? json["error"].get<std::string>() : "";
    if (error == "not_found") {
        result.error = RedeemInviteError::NOT_FOUND;
    } else if (error == "revoked") {
        result.error = RedeemInviteError::REVOKED;
    } else if (error == "expired") {
        result.error = RedeemInviteError::EXPIRED;
    } else if (error == "max_uses_reached") {
        result.error = RedeemInviteError::MAX_USES_REACHED;
    } else if (error == "already_member") {
        result.error = RedeemInviteError::ALREADY_MEMBER;
    } else {
        result.error = RedeemInviteError::FAILED;
    }
    return result;
}

std::optional<WireFriend> parseWireFriend(const nlohmann::json& json) {
    if (!json.is_object() || !json.contains("user_id") || !json.contains("username")) {
        return std::nullopt;
    }
    if (!json["user_id"].is_string() || !json["username"].is_string()) {
        return std::nullopt;
    }
    WireFriend friend_{json["user_id"].get<std::string>(), json["username"].get<std::string>(),
                       std::nullopt, std::nullopt};
    parseProfileFields(json, friend_.display_name, friend_.avatar_url);
    return friend_;
}

std::optional<WireFriendRequest> parseWireFriendRequest(const nlohmann::json& json) {
    if (!json.is_object() || !json.contains("user_id") || !json.contains("username") ||
        !json.contains("created_at")) {
        return std::nullopt;
    }
    if (!json["user_id"].is_string() || !json["username"].is_string() ||
        !json["created_at"].is_string()) {
        return std::nullopt;
    }
    WireFriendRequest request{json["user_id"].get<std::string>(),
                              json["username"].get<std::string>(),
                              json["created_at"].get<std::string>(), std::nullopt, std::nullopt};
    parseProfileFields(json, request.display_name, request.avatar_url);
    return request;
}

// docs/social/friends-dms-design.md §1.6's discriminated result, mirrored
// from web/lib/internal/friends.ts's SendFriendRequestResult /
// AddFriendByCodeResult shape.
SendFriendRequestResult parseSendFriendRequestResult(const nlohmann::json& json) {
    SendFriendRequestResult result;
    if (!json.is_object() || !json.contains("ok") || !json["ok"].is_boolean()) {
        return result; // FAILED (default-constructed)
    }

    if (json["ok"].get<bool>()) {
        if (!json.contains("kind") || !json["kind"].is_string() || !json.contains("user_id") ||
            !json["user_id"].is_string() || !json.contains("username") ||
            !json["username"].is_string()) {
            return SendFriendRequestResult{}; // malformed success body — treat as failure
        }
        result.user_id = json["user_id"].get<std::string>();
        result.username = json["username"].get<std::string>();
        result.outcome = json["kind"].get<std::string>() == "friends"
                             ? SendFriendRequestOutcome::FRIENDS_ADDED
                             : SendFriendRequestOutcome::REQUEST_CREATED;
        return result;
    }

    const std::string error =
        json.contains("error") && json["error"].is_string() ? json["error"].get<std::string>() : "";
    if (error == "user_not_found") {
        result.outcome = SendFriendRequestOutcome::USER_NOT_FOUND;
    } else if (error == "self") {
        result.outcome = SendFriendRequestOutcome::SELF;
    } else if (error == "already_friends") {
        result.outcome = SendFriendRequestOutcome::ALREADY_FRIENDS;
    } else if (error == "code_not_found") {
        result.outcome = SendFriendRequestOutcome::CODE_NOT_FOUND;
    } else {
        result.outcome = SendFriendRequestOutcome::FAILED;
    }
    return result;
}

std::optional<WireBlock> parseWireBlock(const nlohmann::json& json) {
    if (!json.is_object() || !json.contains("user_id") || !json.contains("username") ||
        !json.contains("blocked_at")) {
        return std::nullopt;
    }
    if (!json["user_id"].is_string() || !json["username"].is_string() ||
        !json["blocked_at"].is_string()) {
        return std::nullopt;
    }
    WireBlock block{json["user_id"].get<std::string>(), json["username"].get<std::string>(),
                    json["blocked_at"].get<std::string>(), std::nullopt, std::nullopt};
    parseProfileFields(json, block.display_name, block.avatar_url);
    return block;
}

// URL-encodes a single query parameter value. curl_easy_escape needs a
// live handle only to reuse its allocator; it doesn't need to be the same
// handle the request is later performed on.
std::string urlEncode(const std::string& value) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        return value;
    }
    char* escaped = curl_easy_escape(curl, value.c_str(), static_cast<int>(value.size()));
    std::string result = escaped ? std::string(escaped) : value;
    if (escaped) {
        curl_free(escaped);
    }
    curl_easy_cleanup(curl);
    return result;
}

} // namespace http
