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
        !json.contains("owner_id")) {
        return std::nullopt;
    }
    if (!json["guild_id"].is_string() || !json["name"].is_string() ||
        !json["owner_id"].is_string()) {
        return std::nullopt;
    }
    return WireGuild{json["guild_id"].get<std::string>(), json["name"].get<std::string>(),
                     json["owner_id"].get<std::string>()};
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
            if (!m.is_object() || !m.contains("guild_id") || !m.contains("user_id")) {
                continue;
            }
            catalog.memberships.push_back(
                {m["guild_id"].get<std::string>(), m["user_id"].get<std::string>()});
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
                                                            const std::string& owner_id) {
    const nlohmann::json body{{"name", name}, {"owner_id", owner_id}};
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

bool CurlInternalApiClient::createMembership(const std::string& guild_id,
                                             const std::string& user_id, const std::string& role) {
    const nlohmann::json body{{"guild_id", guild_id}, {"user_id", user_id}, {"role", role}};
    const auto response = request("POST", "/internal/guild-memberships", body.dump());
    return response && response->status == 201;
}

bool CurlInternalApiClient::deleteMembership(const std::string& guild_id,
                                             const std::string& user_id) {
    const auto response =
        request("DELETE", "/internal/guild-memberships/" + guild_id + "/" + user_id, "");
    return response && response->status == 200;
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

} // namespace http
