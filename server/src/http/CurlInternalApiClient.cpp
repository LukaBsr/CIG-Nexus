#include "http/CurlInternalApiClient.hpp"

#include <nlohmann/json.hpp>

#include <curl/curl.h>

namespace http {

namespace {

// The internal API is called synchronously from the single-threaded
// Server::start() loop (Server.cpp) — a stalled call must not hang the
// server indefinitely.
constexpr long kTimeoutSeconds = 5;

size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
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
    return message;
}

std::optional<WireMember> parseWireMember(const nlohmann::json& json) {
    if (!json.is_object() || !json.contains("user_id") || !json.contains("username") ||
        !json.contains("role_rank") || !json.contains("role_label") || !json.contains("joined_at")) {
        return std::nullopt;
    }
    if (!json["user_id"].is_string() || !json["username"].is_string() ||
        !json["role_rank"].is_number_integer() || !json["role_label"].is_string() ||
        !json["joined_at"].is_string()) {
        return std::nullopt;
    }
    return WireMember{json["user_id"].get<std::string>(), json["username"].get<std::string>(),
                      json["role_rank"].get<int>(), json["role_label"].get<std::string>(),
                      json["joined_at"].get<std::string>()};
}

std::optional<WireInvite> parseWireInvite(const nlohmann::json& json) {
    if (!json.is_object() || !json.contains("code") || !json.contains("max_uses") ||
        !json.contains("use_count") || !json.contains("expires_at") || !json.contains("revoked_at") ||
        !json.contains("created_at")) {
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
    invite.max_uses =
        json["max_uses"].is_number_integer() ? std::make_optional(json["max_uses"].get<int>()) : std::nullopt;
    invite.use_count = json["use_count"].get<int>();
    invite.expires_at = json["expires_at"].is_string() ? std::make_optional(json["expires_at"].get<std::string>())
                                                        : std::nullopt;
    invite.revoked_at = json["revoked_at"].is_string() ? std::make_optional(json["revoked_at"].get<std::string>())
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
    return WireJoinRequest{json["user_id"].get<std::string>(), json["username"].get<std::string>(),
                           json["requested_at"].get<std::string>()};
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
        if (!result.is_join_request && json.contains("role_rank") && json["role_rank"].is_number_integer()) {
            result.role_rank = json["role_rank"].get<int>();
        }
        return result;
    }

    const std::string error = json.contains("error") && json["error"].is_string()
                                  ? json["error"].get<std::string>()
                                  : "";
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

} // namespace

CurlInternalApiClient::CurlInternalApiClient(std::string base_url, std::string shared_secret)
    : base_url_(std::move(base_url)), shared_secret_(std::move(shared_secret)) {}

std::optional<CurlInternalApiClient::HttpResponse>
CurlInternalApiClient::request(const std::string& method, const std::string& path,
                               const std::string& body) const {
    CURL* curl = curl_easy_init();
    if (!curl) {
        return std::nullopt;
    }

    const std::string url = base_url_ + path;
    HttpResponse response;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, kTimeoutSeconds);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);

    struct curl_slist* headers = nullptr;
    const std::string secret_header = "X-Internal-Secret: " + shared_secret_;
    headers = curl_slist_append(headers, secret_header.c_str());

    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
        headers = curl_slist_append(headers, "Content-Type: application/json");
    } else if (method == "PATCH") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
        headers = curl_slist_append(headers, "Content-Type: application/json");
    } else if (method == "DELETE") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    }
    // "GET" needs no extra option — it's curl's default.

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    const CURLcode result = curl_easy_perform(curl);
    if (result == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK) {
        return std::nullopt;
    }
    return response;
}

std::optional<Catalog> CurlInternalApiClient::fetchCatalog() {
    const auto response = request("GET", "/internal/catalog", "");
    if (!response || response->status != 200) {
        return std::nullopt;
    }

    Catalog catalog;
    try {
        const auto json = nlohmann::json::parse(response->body);
        if (!json.is_object() || !json.contains("guilds") || !json.contains("memberships") ||
            !json.contains("channels")) {
            return std::nullopt;
        }

        for (const auto& g : json["guilds"]) {
            if (auto guild = parseWireGuild(g)) {
                catalog.guilds.push_back(*guild);
            }
        }
        for (const auto& m : json["memberships"]) {
            if (!m.is_object() || !m.contains("guild_id") || !m.contains("user_id") ||
                !m.contains("role_rank") || !m["role_rank"].is_number_integer()) {
                continue;
            }
            catalog.memberships.push_back({m["guild_id"].get<std::string>(),
                                          m["user_id"].get<std::string>(),
                                          m["role_rank"].get<int>()});
        }
        for (const auto& c : json["channels"]) {
            if (auto channel = parseWireChannel(c)) {
                catalog.channels.push_back(*channel);
            }
        }
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }

    return catalog;
}

std::optional<WireGuild> CurlInternalApiClient::createGuild(const std::string& name,
                                                            const std::string& owner_id,
                                                            const std::string& visibility) {
    const nlohmann::json body{{"name", name}, {"owner_id", owner_id}, {"visibility", visibility}};
    const auto response = request("POST", "/internal/guilds", body.dump());
    if (!response || response->status != 201) {
        return std::nullopt;
    }

    try {
        return parseWireGuild(nlohmann::json::parse(response->body));
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

bool CurlInternalApiClient::deleteGuild(const std::string& guild_id) {
    const auto response = request("DELETE", "/internal/guilds/" + guild_id, "");
    return response && response->status == 200;
}

std::optional<std::string> CurlInternalApiClient::setGuildVisibility(const std::string& guild_id,
                                                                      const std::string& visibility) {
    const nlohmann::json body{{"visibility", visibility}};
    const auto response = request("POST", "/internal/guilds/" + guild_id + "/visibility", body.dump());
    if (!response || response->status != 200) {
        return std::nullopt;
    }

    try {
        const auto json = nlohmann::json::parse(response->body);
        if (!json.is_object() || !json.contains("visibility") || !json["visibility"].is_string()) {
            return std::nullopt;
        }
        return json["visibility"].get<std::string>();
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

std::optional<int> CurlInternalApiClient::createMembership(const std::string& guild_id,
                                                            const std::string& user_id,
                                                            int role_rank) {
    const nlohmann::json body{{"guild_id", guild_id}, {"user_id", user_id}, {"role_rank", role_rank}};
    const auto response = request("POST", "/internal/guild-memberships", body.dump());
    if (!response || response->status != 201) {
        return std::nullopt;
    }

    try {
        const auto json = nlohmann::json::parse(response->body);
        if (!json.is_object() || !json.contains("role_rank") || !json["role_rank"].is_number_integer()) {
            return std::nullopt;
        }
        return json["role_rank"].get<int>();
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

bool CurlInternalApiClient::deleteMembership(const std::string& guild_id,
                                             const std::string& user_id) {
    const auto response =
        request("DELETE", "/internal/guild-memberships/" + guild_id + "/" + user_id, "");
    return response && response->status == 200;
}

std::optional<std::vector<WireMember>>
CurlInternalApiClient::fetchGuildMembers(const std::string& guild_id) {
    const auto response =
        request("GET", "/internal/guild-memberships?guild_id=" + urlEncode(guild_id), "");
    if (!response || response->status != 200) {
        return std::nullopt;
    }

    std::vector<WireMember> members;
    try {
        const auto json = nlohmann::json::parse(response->body);
        if (!json.is_object() || !json.contains("members")) {
            return std::nullopt;
        }
        for (const auto& m : json["members"]) {
            if (auto member = parseWireMember(m)) {
                members.push_back(*member);
            }
        }
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }

    return members;
}

std::optional<std::string> CurlInternalApiClient::setMemberRole(const std::string& guild_id,
                                                                 const std::string& user_id,
                                                                 int role_rank) {
    const nlohmann::json body{{"role_rank", role_rank}};
    const auto response =
        request("PATCH", "/internal/guild-memberships/" + guild_id + "/" + user_id, body.dump());
    if (!response || response->status != 200) {
        return std::nullopt;
    }

    try {
        const auto json = nlohmann::json::parse(response->body);
        if (!json.is_object() || !json.contains("role_label") || !json["role_label"].is_string()) {
            return std::nullopt;
        }
        return json["role_label"].get<std::string>();
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

std::optional<WireChannel> CurlInternalApiClient::createChannel(const std::string& guild_id,
                                                                const std::string& name,
                                                                const std::string& channel_type) {
    const nlohmann::json body{
        {"guild_id", guild_id}, {"name", name}, {"channel_type", channel_type}};
    const auto response = request("POST", "/internal/channels", body.dump());
    if (!response || response->status != 201) {
        return std::nullopt;
    }

    try {
        return parseWireChannel(nlohmann::json::parse(response->body));
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

bool CurlInternalApiClient::deleteChannel(const std::string& channel_id) {
    const auto response = request("DELETE", "/internal/channels/" + channel_id, "");
    return response && response->status == 200;
}

std::vector<std::string>
CurlInternalApiClient::fetchRevokedSessionIds(const std::string& since_iso8601,
                                              std::string& out_as_of) {
    char* escaped =
        curl_easy_escape(nullptr, since_iso8601.c_str(), static_cast<int>(since_iso8601.size()));
    const std::string query = escaped ? std::string(escaped) : since_iso8601;
    if (escaped) {
        curl_free(escaped);
    }

    const auto response = request("GET", "/internal/revoked-sessions?since=" + query, "");
    if (!response || response->status != 200) {
        return {};
    }

    std::vector<std::string> ids;
    try {
        const auto json = nlohmann::json::parse(response->body);
        if (!json.is_object() || !json.contains("revoked_session_ids") || !json.contains("as_of")) {
            return {};
        }
        for (const auto& id : json["revoked_session_ids"]) {
            if (id.is_string()) {
                ids.push_back(id.get<std::string>());
            }
        }
        if (json["as_of"].is_string()) {
            out_as_of = json["as_of"].get<std::string>();
        }
    } catch (const nlohmann::json::exception&) {
        return {};
    }

    return ids;
}

bool CurlInternalApiClient::createMessage(const std::optional<std::string>& channel_id,
                                          const std::string& user_id, const std::string& content,
                                          int seq) {
    const nlohmann::json body{{"channel_id", channel_id.has_value() ? nlohmann::json(*channel_id) : nullptr},
                              {"user_id", user_id},
                              {"content", content},
                              {"seq", seq}};
    const auto response = request("POST", "/internal/messages", body.dump());
    return response && response->status == 201;
}

std::optional<HistoryPage> CurlInternalApiClient::fetchMessages(const std::optional<std::string>& channel_id,
                                                                 std::optional<int> before_seq, int limit) {
    std::string path = "/internal/messages?limit=" + std::to_string(limit);
    if (channel_id.has_value()) {
        path += "&channel_id=" + urlEncode(*channel_id);
    }
    if (before_seq.has_value()) {
        path += "&before_seq=" + std::to_string(*before_seq);
    }

    const auto response = request("GET", path, "");
    if (!response || response->status != 200) {
        return std::nullopt;
    }

    HistoryPage page;
    try {
        const auto json = nlohmann::json::parse(response->body);
        if (!json.is_object() || !json.contains("messages") || !json.contains("has_more") ||
            !json["has_more"].is_boolean()) {
            return std::nullopt;
        }
        for (const auto& m : json["messages"]) {
            if (auto message = parseWireMessage(m)) {
                page.messages.push_back(*message);
            }
        }
        page.has_more = json["has_more"].get<bool>();
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }

    return page;
}

LastSequence CurlInternalApiClient::fetchLastSequence() {
    const auto response = request("GET", "/internal/messages/last-sequence", "");
    if (!response || response->status != 200) {
        return {};
    }

    LastSequence result;
    try {
        const auto json = nlohmann::json::parse(response->body);
        if (!json.is_object() || !json.contains("lobby_seq") || !json.contains("channel_seq")) {
            return {};
        }
        if (json["lobby_seq"].is_number_integer()) {
            result.lobby_seq = json["lobby_seq"].get<int>();
        }
        if (json["channel_seq"].is_number_integer()) {
            result.channel_seq = json["channel_seq"].get<int>();
        }
    } catch (const nlohmann::json::exception&) {
        return {};
    }

    return result;
}

std::optional<WireInvite> CurlInternalApiClient::createInvite(const std::string& guild_id,
                                                               const std::string& created_by,
                                                               std::optional<int> max_uses,
                                                               std::optional<int> expires_in_seconds) {
    const nlohmann::json body{
        {"guild_id", guild_id},
        {"created_by", created_by},
        {"max_uses", max_uses.has_value() ? nlohmann::json(*max_uses) : nullptr},
        {"expires_in_seconds",
         expires_in_seconds.has_value() ? nlohmann::json(*expires_in_seconds) : nullptr}};
    const auto response = request("POST", "/internal/guild-invites", body.dump());
    if (!response || response->status != 201) {
        return std::nullopt;
    }

    try {
        return parseWireInvite(nlohmann::json::parse(response->body));
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

std::optional<std::vector<WireInvite>> CurlInternalApiClient::fetchInvites(const std::string& guild_id) {
    const auto response = request("GET", "/internal/guild-invites?guild_id=" + urlEncode(guild_id), "");
    if (!response || response->status != 200) {
        return std::nullopt;
    }

    std::vector<WireInvite> invites;
    try {
        const auto json = nlohmann::json::parse(response->body);
        if (!json.is_object() || !json.contains("invites")) {
            return std::nullopt;
        }
        for (const auto& i : json["invites"]) {
            if (auto invite = parseWireInvite(i)) {
                invites.push_back(*invite);
            }
        }
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }

    return invites;
}

bool CurlInternalApiClient::revokeInvite(const std::string& guild_id, const std::string& code) {
    const auto response = request(
        "DELETE", "/internal/guild-invites/" + urlEncode(code) + "?guild_id=" + urlEncode(guild_id), "");
    return response && response->status == 200;
}

RedeemInviteResult CurlInternalApiClient::redeemInvite(const std::string& code,
                                                       const std::string& user_id) {
    const nlohmann::json body{{"user_id", user_id}};
    const auto response = request("POST", "/internal/guild-invites/" + urlEncode(code) + "/redeem", body.dump());
    if (!response || response->status != 200) {
        return RedeemInviteResult{};
    }

    try {
        return parseRedeemInviteResult(nlohmann::json::parse(response->body));
    } catch (const nlohmann::json::exception&) {
        return RedeemInviteResult{};
    }
}

CreateJoinRequestResult CurlInternalApiClient::createJoinRequest(const std::string& guild_id,
                                                                  const std::string& user_id) {
    const nlohmann::json body{{"guild_id", guild_id}, {"user_id", user_id}};
    const auto response = request("POST", "/internal/guild-join-requests", body.dump());
    if (!response || response->status != 201) {
        return CreateJoinRequestResult::FAILED;
    }

    try {
        const auto json = nlohmann::json::parse(response->body);
        if (!json.is_object() || !json.contains("result") || !json["result"].is_string()) {
            return CreateJoinRequestResult::FAILED;
        }
        const std::string result = json["result"].get<std::string>();
        if (result == "created") {
            return CreateJoinRequestResult::CREATED;
        }
        if (result == "already_pending") {
            return CreateJoinRequestResult::ALREADY_PENDING;
        }
        return CreateJoinRequestResult::FAILED;
    } catch (const nlohmann::json::exception&) {
        return CreateJoinRequestResult::FAILED;
    }
}

std::optional<std::vector<WireJoinRequest>>
CurlInternalApiClient::fetchJoinRequests(const std::string& guild_id) {
    const auto response =
        request("GET", "/internal/guild-join-requests?guild_id=" + urlEncode(guild_id), "");
    if (!response || response->status != 200) {
        return std::nullopt;
    }

    std::vector<WireJoinRequest> requests;
    try {
        const auto json = nlohmann::json::parse(response->body);
        if (!json.is_object() || !json.contains("requests")) {
            return std::nullopt;
        }
        for (const auto& r : json["requests"]) {
            if (auto req = parseWireJoinRequest(r)) {
                requests.push_back(*req);
            }
        }
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }

    return requests;
}

std::optional<int> CurlInternalApiClient::approveJoinRequest(const std::string& guild_id,
                                                              const std::string& user_id) {
    const auto response =
        request("POST", "/internal/guild-join-requests/" + guild_id + "/" + user_id + "/approve", "");
    if (!response || response->status != 200) {
        return std::nullopt;
    }

    try {
        const auto json = nlohmann::json::parse(response->body);
        if (!json.is_object() || !json.contains("role_rank") || !json["role_rank"].is_number_integer()) {
            return std::nullopt;
        }
        return json["role_rank"].get<int>();
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

bool CurlInternalApiClient::rejectJoinRequest(const std::string& guild_id, const std::string& user_id) {
    const auto response =
        request("DELETE", "/internal/guild-join-requests/" + guild_id + "/" + user_id, "");
    return response && response->status == 200;
}

} // namespace http
