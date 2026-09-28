#include "http/CurlInternalApiClient.hpp"
#include "WireParsers.hpp"

#include <nlohmann/json.hpp>

#include <curl/curl.h>

namespace http {

namespace {

// The internal API is called synchronously from the single-threaded
// Server::start() loop (Server.cpp) — a stalled call must not hang the
// server indefinitely.
constexpr long kTimeoutSeconds = 5;

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

std::optional<std::string>
CurlInternalApiClient::setGuildVisibility(const std::string& guild_id,
                                          const std::string& visibility) {
    const nlohmann::json body{{"visibility", visibility}};
    const auto response =
        request("POST", "/internal/guilds/" + guild_id + "/visibility", body.dump());
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
    const nlohmann::json body{
        {"guild_id", guild_id}, {"user_id", user_id}, {"role_rank", role_rank}};
    const auto response = request("POST", "/internal/guild-memberships", body.dump());
    if (!response || response->status != 201) {
        return std::nullopt;
    }

    try {
        const auto json = nlohmann::json::parse(response->body);
        if (!json.is_object() || !json.contains("role_rank") ||
            !json["role_rank"].is_number_integer()) {
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
                                          const std::optional<std::string>& dm_peer_id,
                                          const std::string& user_id, const std::string& content,
                                          int seq) {
    const nlohmann::json body{
        {"channel_id", channel_id.has_value() ? nlohmann::json(*channel_id) : nullptr},
        {"peer_id", dm_peer_id.has_value() ? nlohmann::json(*dm_peer_id) : nullptr},
        {"user_id", user_id},
        {"content", content},
        {"seq", seq}};
    const auto response = request("POST", "/internal/messages", body.dump());
    return response && response->status == 201;
}

std::optional<HistoryPage> CurlInternalApiClient::fetchMessages(
    const std::optional<std::string>& channel_id, const std::optional<std::string>& dm_peer_id,
    const std::string& requester_user_id, std::optional<int> before_seq, int limit) {
    std::string path = "/internal/messages?limit=" + std::to_string(limit);
    if (channel_id.has_value()) {
        path += "&channel_id=" + urlEncode(*channel_id);
    }
    if (dm_peer_id.has_value()) {
        path +=
            "&peer_id=" + urlEncode(*dm_peer_id) + "&requester_id=" + urlEncode(requester_user_id);
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
        if (json.contains("dm_seq") && json["dm_seq"].is_number_integer()) {
            result.dm_seq = json["dm_seq"].get<int>();
        }
    } catch (const nlohmann::json::exception&) {
        return {};
    }

    return result;
}

std::optional<std::vector<WireDmConversation>>
CurlInternalApiClient::fetchDmConversations(const std::string& user_id) {
    const auto response =
        request("GET", "/internal/dm-conversations?user_id=" + urlEncode(user_id), "");
    if (!response || response->status != 200) {
        return std::nullopt;
    }

    std::vector<WireDmConversation> conversations;
    try {
        const auto json = nlohmann::json::parse(response->body);
        if (!json.is_object() || !json.contains("conversations")) {
            return std::nullopt;
        }
        for (const auto& c : json["conversations"]) {
            if (!c.is_object() || !c.contains("peer_id") || !c["peer_id"].is_string() ||
                !c.contains("username") || !c["username"].is_string()) {
                continue;
            }
            WireDmConversation conversation;
            conversation.peer_id = c["peer_id"].get<std::string>();
            conversation.username = c["username"].get<std::string>();
            parseProfileFields(c, conversation.display_name, conversation.avatar_url);
            if (c.contains("last_message_at") && c["last_message_at"].is_string()) {
                conversation.last_message_at = c["last_message_at"].get<std::string>();
            }
            conversations.push_back(conversation);
        }
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
    return conversations;
}

std::optional<std::vector<std::string>>
CurlInternalApiClient::fetchGuildIdsForUser(const std::string& user_id) {
    const auto response =
        request("GET", "/internal/guild-memberships?user_id=" + urlEncode(user_id), "");
    if (!response || response->status != 200) {
        return std::nullopt;
    }

    std::vector<std::string> guild_ids;
    try {
        const auto json = nlohmann::json::parse(response->body);
        if (!json.is_object() || !json.contains("guild_ids")) {
            return std::nullopt;
        }
        for (const auto& g : json["guild_ids"]) {
            if (g.is_string()) {
                guild_ids.push_back(g.get<std::string>());
            }
        }
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
    return guild_ids;
}

std::optional<WireInvite>
CurlInternalApiClient::createInvite(const std::string& guild_id, const std::string& created_by,
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

std::optional<std::vector<WireInvite>>
CurlInternalApiClient::fetchInvites(const std::string& guild_id) {
    const auto response =
        request("GET", "/internal/guild-invites?guild_id=" + urlEncode(guild_id), "");
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
        "DELETE", "/internal/guild-invites/" + urlEncode(code) + "?guild_id=" + urlEncode(guild_id),
        "");
    return response && response->status == 200;
}

RedeemInviteResult CurlInternalApiClient::redeemInvite(const std::string& code,
                                                       const std::string& user_id) {
    const nlohmann::json body{{"user_id", user_id}};
    const auto response =
        request("POST", "/internal/guild-invites/" + urlEncode(code) + "/redeem", body.dump());
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
    const auto response = request(
        "POST", "/internal/guild-join-requests/" + guild_id + "/" + user_id + "/approve", "");
    if (!response || response->status != 200) {
        return std::nullopt;
    }

    try {
        const auto json = nlohmann::json::parse(response->body);
        if (!json.is_object() || !json.contains("role_rank") ||
            !json["role_rank"].is_number_integer()) {
            return std::nullopt;
        }
        return json["role_rank"].get<int>();
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

bool CurlInternalApiClient::rejectJoinRequest(const std::string& guild_id,
                                              const std::string& user_id) {
    const auto response =
        request("DELETE", "/internal/guild-join-requests/" + guild_id + "/" + user_id, "");
    return response && response->status == 200;
}

SendFriendRequestResult CurlInternalApiClient::sendFriendRequest(const std::string& requester_id,
                                                                 const std::string& recipient_id) {
    const nlohmann::json body{{"requester_id", requester_id}, {"recipient_id", recipient_id}};
    const auto response = request("POST", "/internal/friend-requests", body.dump());
    if (!response || response->status != 200) {
        return SendFriendRequestResult{};
    }
    try {
        return parseSendFriendRequestResult(nlohmann::json::parse(response->body));
    } catch (const nlohmann::json::exception&) {
        return SendFriendRequestResult{};
    }
}

SendFriendRequestResult CurlInternalApiClient::addFriendByCode(const std::string& requester_id,
                                                               const std::string& code) {
    const nlohmann::json body{{"requester_id", requester_id}, {"code", code}};
    const auto response = request("POST", "/internal/friend-requests", body.dump());
    if (!response || response->status != 200) {
        return SendFriendRequestResult{};
    }
    try {
        return parseSendFriendRequestResult(nlohmann::json::parse(response->body));
    } catch (const nlohmann::json::exception&) {
        return SendFriendRequestResult{};
    }
}

AcceptFriendRequestResult
CurlInternalApiClient::acceptFriendRequest(const std::string& requester_id,
                                           const std::string& recipient_id) {
    const auto response = request(
        "POST", "/internal/friend-requests/" + requester_id + "/" + recipient_id + "/accept", "");
    if (!response || response->status != 200) {
        return AcceptFriendRequestResult{};
    }
    try {
        const auto json = nlohmann::json::parse(response->body);
        if (!json.is_object() || !json.contains("ok") || !json["ok"].is_boolean() ||
            !json["ok"].get<bool>() || !json.contains("user_id") || !json["user_id"].is_string() ||
            !json.contains("username") || !json["username"].is_string()) {
            return AcceptFriendRequestResult{};
        }
        return AcceptFriendRequestResult{true, json["user_id"].get<std::string>(),
                                         json["username"].get<std::string>()};
    } catch (const nlohmann::json::exception&) {
        return AcceptFriendRequestResult{};
    }
}

bool CurlInternalApiClient::deleteFriendRequest(const std::string& requester_id,
                                                const std::string& recipient_id) {
    const auto response =
        request("DELETE", "/internal/friend-requests/" + requester_id + "/" + recipient_id, "");
    return response && response->status == 200;
}

bool CurlInternalApiClient::removeFriend(const std::string& user_id_a,
                                         const std::string& user_id_b) {
    const auto response =
        request("DELETE", "/internal/friendships/" + user_id_a + "/" + user_id_b, "");
    return response && response->status == 200;
}

std::optional<std::vector<WireFriend>>
CurlInternalApiClient::fetchFriends(const std::string& user_id) {
    const auto response = request("GET", "/internal/friendships?user_id=" + urlEncode(user_id), "");
    if (!response || response->status != 200) {
        return std::nullopt;
    }

    std::vector<WireFriend> friends;
    try {
        const auto json = nlohmann::json::parse(response->body);
        if (!json.is_object() || !json.contains("friends")) {
            return std::nullopt;
        }
        for (const auto& f : json["friends"]) {
            if (auto friend_ = parseWireFriend(f)) {
                friends.push_back(*friend_);
            }
        }
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
    return friends;
}

std::optional<FriendRequestList>
CurlInternalApiClient::fetchFriendRequests(const std::string& user_id) {
    const auto response =
        request("GET", "/internal/friend-requests?user_id=" + urlEncode(user_id), "");
    if (!response || response->status != 200) {
        return std::nullopt;
    }

    FriendRequestList list;
    try {
        const auto json = nlohmann::json::parse(response->body);
        if (!json.is_object() || !json.contains("incoming") || !json.contains("outgoing")) {
            return std::nullopt;
        }
        for (const auto& r : json["incoming"]) {
            if (auto req = parseWireFriendRequest(r)) {
                list.incoming.push_back(*req);
            }
        }
        for (const auto& r : json["outgoing"]) {
            if (auto req = parseWireFriendRequest(r)) {
                list.outgoing.push_back(*req);
            }
        }
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
    return list;
}

std::optional<std::string> CurlInternalApiClient::fetchFriendCode(const std::string& user_id) {
    const auto response = request("GET", "/internal/friend-codes/" + user_id, "");
    if (!response || response->status != 200) {
        return std::nullopt;
    }
    try {
        const auto json = nlohmann::json::parse(response->body);
        if (!json.is_object() || !json.contains("code") || !json["code"].is_string()) {
            return std::nullopt;
        }
        return json["code"].get<std::string>();
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

std::optional<std::string> CurlInternalApiClient::regenerateFriendCode(const std::string& user_id) {
    const auto response = request("POST", "/internal/friend-codes/" + user_id + "/regenerate", "");
    if (!response || response->status != 200) {
        return std::nullopt;
    }
    try {
        const auto json = nlohmann::json::parse(response->body);
        if (!json.is_object() || !json.contains("code") || !json["code"].is_string()) {
            return std::nullopt;
        }
        return json["code"].get<std::string>();
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

bool CurlInternalApiClient::blockUser(const std::string& blocker_id,
                                      const std::string& blocked_id) {
    const nlohmann::json body{{"blocker_id", blocker_id}, {"blocked_id", blocked_id}};
    const auto response = request("POST", "/internal/blocks", body.dump());
    return response && response->status == 200;
}

bool CurlInternalApiClient::unblockUser(const std::string& blocker_id,
                                        const std::string& blocked_id) {
    const auto response =
        request("DELETE", "/internal/blocks/" + blocker_id + "/" + blocked_id, "");
    return response && response->status == 200;
}

std::optional<std::vector<WireBlock>>
CurlInternalApiClient::fetchBlocks(const std::string& user_id) {
    const auto response = request("GET", "/internal/blocks?user_id=" + urlEncode(user_id), "");
    if (!response || response->status != 200) {
        return std::nullopt;
    }

    std::vector<WireBlock> blocks;
    try {
        const auto json = nlohmann::json::parse(response->body);
        if (!json.is_object() || !json.contains("blocks")) {
            return std::nullopt;
        }
        for (const auto& b : json["blocks"]) {
            if (auto block = parseWireBlock(b)) {
                blocks.push_back(*block);
            }
        }
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
    return blocks;
}

std::optional<WireUserProfile> CurlInternalApiClient::fetchUserProfile(const std::string& user_id) {
    const auto response = request("GET", "/internal/users/" + user_id + "/profile", "");
    if (!response || response->status != 200) {
        return std::nullopt;
    }
    try {
        const auto json = nlohmann::json::parse(response->body);
        if (!json.is_object() || !json.contains("user_id") || !json["user_id"].is_string() ||
            !json.contains("username") || !json["username"].is_string()) {
            return std::nullopt;
        }
        WireUserProfile profile;
        profile.user_id = json["user_id"].get<std::string>();
        profile.username = json["username"].get<std::string>();
        parseProfileFields(json, profile.display_name, profile.avatar_url);
        return profile;
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

std::optional<WireSessionContext>
CurlInternalApiClient::fetchSessionContext(const std::string& user_id) {
    const auto response = request("GET", "/internal/users/" + user_id + "/session-context", "");
    if (!response || response->status != 200) {
        return std::nullopt;
    }

    try {
        const auto json = nlohmann::json::parse(response->body);
        if (!json.is_object() || !json.contains("user_id") || !json["user_id"].is_string() ||
            !json.contains("username") || !json["username"].is_string() ||
            !json.contains("blocks") || !json["blocks"].is_array() || !json.contains("friends") ||
            !json["friends"].is_array()) {
            return std::nullopt;
        }

        WireSessionContext context;
        context.profile.user_id = json["user_id"].get<std::string>();
        context.profile.username = json["username"].get<std::string>();
        parseProfileFields(json, context.profile.display_name, context.profile.avatar_url);

        for (const auto& b : json["blocks"]) {
            if (auto block = parseWireBlock(b)) {
                context.blocks.push_back(*block);
            }
        }
        for (const auto& f : json["friends"]) {
            if (auto friend_ = parseWireFriend(f)) {
                context.friends.push_back(*friend_);
            }
        }
        return context;
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

} // namespace http
