#include "http/CurlInternalApiClient.hpp"

#include "TestHttpServer.hpp"

#include <catch2/catch_test_macros.hpp>

using test_helpers::TestHttpServer;

TEST_CASE("CurlInternalApiClient::fetchCatalog parses a valid catalog and sends the shared secret",
          "[CurlInternalApiClient]") {
    TestHttpServer server(200, R"({
        "guilds": [{"guild_id": "g_1", "name": "My Guild", "owner_id": "u_1"}],
        "memberships": [{"guild_id": "g_1", "user_id": "u_1"}],
        "channels": [{"channel_id": "c_1", "guild_id": "g_1", "name": "general", "channel_type": "TEXT"}]
    })");

    http::CurlInternalApiClient client(server.baseUrl(), "test-secret");
    const auto catalog = client.fetchCatalog();

    REQUIRE(catalog.has_value());
    REQUIRE(catalog->guilds.size() == 1);
    CHECK(catalog->guilds[0].guild_id == "g_1");
    REQUIRE(catalog->memberships.size() == 1);
    REQUIRE(catalog->channels.size() == 1);
    CHECK(catalog->channels[0].channel_type == "TEXT");

    const auto request = server.waitForRequest();
    REQUIRE(request.has_value());
    CHECK(request->method == "GET");
    CHECK(request->path == "/internal/catalog");
    CHECK(request->internal_secret_header == "test-secret");
}

TEST_CASE("CurlInternalApiClient::fetchCatalog returns nullopt on a non-200 response",
          "[CurlInternalApiClient]") {
    TestHttpServer server(500, R"({"error": "boom"})");
    http::CurlInternalApiClient client(server.baseUrl(), "test-secret");

    CHECK_FALSE(client.fetchCatalog().has_value());
}

TEST_CASE("CurlInternalApiClient::createGuild posts the expected JSON body and parses the response",
          "[CurlInternalApiClient]") {
    TestHttpServer server(201, R"({"guild_id": "g_2", "name": "My Guild", "owner_id": "u_1"})");
    http::CurlInternalApiClient client(server.baseUrl(), "test-secret");

    const auto guild = client.createGuild("My Guild", "u_1");
    REQUIRE(guild.has_value());
    CHECK(guild->guild_id == "g_2");

    const auto request = server.waitForRequest();
    REQUIRE(request.has_value());
    CHECK(request->method == "POST");
    CHECK(request->path == "/internal/guilds");
    CHECK(request->has_content_type_json);
    CHECK(request->body.find("\"name\":\"My Guild\"") != std::string::npos);
    CHECK(request->body.find("\"owner_id\":\"u_1\"") != std::string::npos);
}

TEST_CASE("CurlInternalApiClient::createGuild returns nullopt on a 400", "[CurlInternalApiClient]") {
    TestHttpServer server(400, R"({"error": "invalid request"})");
    http::CurlInternalApiClient client(server.baseUrl(), "test-secret");

    CHECK_FALSE(client.createGuild("", "u_1").has_value());
}

TEST_CASE("CurlInternalApiClient::deleteGuild issues DELETE to the right path",
          "[CurlInternalApiClient]") {
    TestHttpServer server(200, R"({"ok": true})");
    http::CurlInternalApiClient client(server.baseUrl(), "test-secret");

    CHECK(client.deleteGuild("g_3"));

    const auto request = server.waitForRequest();
    REQUIRE(request.has_value());
    CHECK(request->method == "DELETE");
    CHECK(request->path == "/internal/guilds/g_3");
}

TEST_CASE("CurlInternalApiClient::deleteGuild returns false on 404", "[CurlInternalApiClient]") {
    TestHttpServer server(404, R"({"error": "not found"})");
    http::CurlInternalApiClient client(server.baseUrl(), "test-secret");

    CHECK_FALSE(client.deleteGuild("g_unknown"));
}

TEST_CASE("CurlInternalApiClient::createMembership sends guild_id/user_id/role",
          "[CurlInternalApiClient]") {
    TestHttpServer server(201, R"({"guild_id": "g_1", "user_id": "u_2"})");
    http::CurlInternalApiClient client(server.baseUrl(), "test-secret");

    CHECK(client.createMembership("g_1", "u_2", "member"));

    const auto request = server.waitForRequest();
    REQUIRE(request.has_value());
    CHECK(request->path == "/internal/guild-memberships");
    CHECK(request->body.find("\"role\":\"member\"") != std::string::npos);
}

TEST_CASE("CurlInternalApiClient::deleteMembership targets the composite path",
          "[CurlInternalApiClient]") {
    TestHttpServer server(200, R"({"ok": true})");
    http::CurlInternalApiClient client(server.baseUrl(), "test-secret");

    CHECK(client.deleteMembership("g_1", "u_2"));

    const auto request = server.waitForRequest();
    REQUIRE(request.has_value());
    CHECK(request->path == "/internal/guild-memberships/g_1/u_2");
}

TEST_CASE("CurlInternalApiClient::createChannel posts guild_id/name/channel_type",
          "[CurlInternalApiClient]") {
    TestHttpServer server(
        201, R"({"channel_id": "c_5", "guild_id": "g_1", "name": "general", "channel_type": "TEXT"})");
    http::CurlInternalApiClient client(server.baseUrl(), "test-secret");

    const auto channel = client.createChannel("g_1", "general", "TEXT");
    REQUIRE(channel.has_value());
    CHECK(channel->channel_id == "c_5");

    const auto request = server.waitForRequest();
    REQUIRE(request.has_value());
    CHECK(request->path == "/internal/channels");
}

TEST_CASE("CurlInternalApiClient::deleteChannel issues DELETE to the right path",
          "[CurlInternalApiClient]") {
    TestHttpServer server(200, R"({"ok": true})");
    http::CurlInternalApiClient client(server.baseUrl(), "test-secret");

    CHECK(client.deleteChannel("c_9"));

    const auto request = server.waitForRequest();
    REQUIRE(request.has_value());
    CHECK(request->path == "/internal/channels/c_9");
}

TEST_CASE("CurlInternalApiClient::fetchRevokedSessionIds parses ids and as_of, URL-encoding since",
          "[CurlInternalApiClient]") {
    TestHttpServer server(
        200, R"({"revoked_session_ids": ["s1", "s2"], "as_of": "2026-01-01T00:00:00.000Z"})");
    http::CurlInternalApiClient client(server.baseUrl(), "test-secret");

    std::string as_of;
    const auto ids = client.fetchRevokedSessionIds("2026-01-01T00:00:00.000Z", as_of);

    CHECK(ids == std::vector<std::string>{"s1", "s2"});
    CHECK(as_of == "2026-01-01T00:00:00.000Z");

    const auto request = server.waitForRequest();
    REQUIRE(request.has_value());
    CHECK(request->method == "GET");
    CHECK(request->path.rfind("/internal/revoked-sessions?since=", 0) == 0);
    CHECK(request->path.find(':') == std::string::npos); // percent-encoded, not raw
}
