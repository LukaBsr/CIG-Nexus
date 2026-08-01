#include "http/CurlInternalApiClient.hpp"

#include "TestHttpServer.hpp"

#include <catch2/catch_test_macros.hpp>

using test_helpers::TestHttpServer;

TEST_CASE("CurlInternalApiClient::fetchCatalog parses a valid catalog and sends the shared secret",
          "[CurlInternalApiClient]") {
    TestHttpServer server(200, R"({
        "guilds": [{"guild_id": "g_1", "name": "My Guild", "owner_id": "u_1", "visibility": "open"}],
        "memberships": [{"guild_id": "g_1", "user_id": "u_1", "role_rank": 2}],
        "channels": [{"channel_id": "c_1", "guild_id": "g_1", "name": "general", "channel_type": "TEXT"}]
    })");

    http::CurlInternalApiClient client(server.baseUrl(), "test-secret");
    const auto catalog = client.fetchCatalog();

    REQUIRE(catalog.has_value());
    REQUIRE(catalog->guilds.size() == 1);
    CHECK(catalog->guilds[0].guild_id == "g_1");
    CHECK(catalog->guilds[0].visibility == "open");
    REQUIRE(catalog->memberships.size() == 1);
    CHECK(catalog->memberships[0].role_rank == 2);
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
    TestHttpServer server(
        201, R"({"guild_id": "g_2", "name": "My Guild", "owner_id": "u_1", "visibility": "private"})");
    http::CurlInternalApiClient client(server.baseUrl(), "test-secret");

    const auto guild = client.createGuild("My Guild", "u_1", "private");
    REQUIRE(guild.has_value());
    CHECK(guild->guild_id == "g_2");
    CHECK(guild->visibility == "private");

    const auto request = server.waitForRequest();
    REQUIRE(request.has_value());
    CHECK(request->method == "POST");
    CHECK(request->path == "/internal/guilds");
    CHECK(request->has_content_type_json);
    CHECK(request->body.find("\"name\":\"My Guild\"") != std::string::npos);
    CHECK(request->body.find("\"owner_id\":\"u_1\"") != std::string::npos);
    CHECK(request->body.find("\"visibility\":\"private\"") != std::string::npos);
}

TEST_CASE("CurlInternalApiClient::createGuild returns nullopt on a 400",
          "[CurlInternalApiClient]") {
    TestHttpServer server(400, R"({"error": "invalid request"})");
    http::CurlInternalApiClient client(server.baseUrl(), "test-secret");

    CHECK_FALSE(client.createGuild("", "u_1", "open").has_value());
}

TEST_CASE("CurlInternalApiClient::setGuildVisibility posts visibility and parses the response",
          "[CurlInternalApiClient]") {
    TestHttpServer server(200, R"({"visibility": "private"})");
    http::CurlInternalApiClient client(server.baseUrl(), "test-secret");

    const auto result = client.setGuildVisibility("g_1", "private");
    REQUIRE(result.has_value());
    CHECK(*result == "private");

    const auto request = server.waitForRequest();
    REQUIRE(request.has_value());
    CHECK(request->method == "POST");
    CHECK(request->path == "/internal/guilds/g_1/visibility");
    CHECK(request->body.find("\"visibility\":\"private\"") != std::string::npos);
}

TEST_CASE("CurlInternalApiClient::setGuildVisibility returns nullopt on a 404",
          "[CurlInternalApiClient]") {
    TestHttpServer server(404, R"({"error": "not found"})");
    http::CurlInternalApiClient client(server.baseUrl(), "test-secret");

    CHECK_FALSE(client.setGuildVisibility("g_unknown", "open").has_value());
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

TEST_CASE("CurlInternalApiClient::createMembership sends guild_id/user_id/role_rank and returns "
          "the resulting rank",
          "[CurlInternalApiClient]") {
    TestHttpServer server(201, R"({"guild_id": "g_1", "user_id": "u_2", "role_rank": 0})");
    http::CurlInternalApiClient client(server.baseUrl(), "test-secret");

    const auto result = client.createMembership("g_1", "u_2", 0);
    REQUIRE(result.has_value());
    CHECK(*result == 0);

    const auto request = server.waitForRequest();
    REQUIRE(request.has_value());
    CHECK(request->path == "/internal/guild-memberships");
    CHECK(request->body.find("\"role_rank\":0") != std::string::npos);
}

TEST_CASE("CurlInternalApiClient::createMembership returns nullopt on a non-201",
          "[CurlInternalApiClient]") {
    TestHttpServer server(400, R"({"error": "invalid request"})");
    http::CurlInternalApiClient client(server.baseUrl(), "test-secret");

    CHECK_FALSE(client.createMembership("g_1", "u_2", 0).has_value());
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

TEST_CASE("CurlInternalApiClient::fetchGuildMembers parses the roster and sends guild_id as a query param",
          "[CurlInternalApiClient]") {
    TestHttpServer server(200, R"({
        "members": [
            {"user_id": "u_1", "username": "alice", "role_rank": 2, "role_label": "Captain", "joined_at": "2026-07-14T18:00:00Z"}
        ]
    })");
    http::CurlInternalApiClient client(server.baseUrl(), "test-secret");

    const auto members = client.fetchGuildMembers("g_1");
    REQUIRE(members.has_value());
    REQUIRE(members->size() == 1);
    CHECK((*members)[0].username == "alice");
    CHECK((*members)[0].role_label == "Captain");

    const auto request = server.waitForRequest();
    REQUIRE(request.has_value());
    CHECK(request->method == "GET");
    CHECK(request->path == "/internal/guild-memberships?guild_id=g_1");
}

TEST_CASE("CurlInternalApiClient::fetchGuildMembers returns nullopt on a 404",
          "[CurlInternalApiClient]") {
    TestHttpServer server(404, R"({"error": "not found"})");
    http::CurlInternalApiClient client(server.baseUrl(), "test-secret");

    CHECK_FALSE(client.fetchGuildMembers("g_unknown").has_value());
}

TEST_CASE("CurlInternalApiClient::setMemberRole sends a PATCH with role_rank and parses role_label",
          "[CurlInternalApiClient]") {
    TestHttpServer server(200, R"({"role_rank": 1, "role_label": "Officer"})");
    http::CurlInternalApiClient client(server.baseUrl(), "test-secret");

    const auto label = client.setMemberRole("g_1", "u_2", 1);
    REQUIRE(label.has_value());
    CHECK(*label == "Officer");

    const auto request = server.waitForRequest();
    REQUIRE(request.has_value());
    CHECK(request->method == "PATCH");
    CHECK(request->path == "/internal/guild-memberships/g_1/u_2");
    CHECK(request->body.find("\"role_rank\":1") != std::string::npos);
}

TEST_CASE("CurlInternalApiClient::setMemberRole returns nullopt on a 404",
          "[CurlInternalApiClient]") {
    TestHttpServer server(404, R"({"error": "not found"})");
    http::CurlInternalApiClient client(server.baseUrl(), "test-secret");

    CHECK_FALSE(client.setMemberRole("g_1", "u_404", 1).has_value());
}

TEST_CASE("CurlInternalApiClient::createChannel posts guild_id/name/channel_type",
          "[CurlInternalApiClient]") {
    TestHttpServer server(
        201,
        R"({"channel_id": "c_5", "guild_id": "g_1", "name": "general", "channel_type": "TEXT"})");
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

TEST_CASE("CurlInternalApiClient::createInvite posts the expected body and parses the response",
          "[CurlInternalApiClient]") {
    TestHttpServer server(201, R"({
        "code": "abc123", "max_uses": 5, "use_count": 0, "expires_at": null, "revoked_at": null,
        "created_at": "2026-01-01T00:00:00Z"
    })");
    http::CurlInternalApiClient client(server.baseUrl(), "test-secret");

    const auto invite = client.createInvite("g_1", "u_1", 5, std::nullopt);
    REQUIRE(invite.has_value());
    CHECK(invite->code == "abc123");
    REQUIRE(invite->max_uses.has_value());
    CHECK(*invite->max_uses == 5);
    CHECK_FALSE(invite->expires_at.has_value());

    const auto request = server.waitForRequest();
    REQUIRE(request.has_value());
    CHECK(request->path == "/internal/guild-invites");
    CHECK(request->body.find("\"max_uses\":5") != std::string::npos);
    CHECK(request->body.find("\"expires_in_seconds\":null") != std::string::npos);
}

TEST_CASE("CurlInternalApiClient::createInvite returns nullopt on a 400", "[CurlInternalApiClient]") {
    TestHttpServer server(400, R"({"error": "invalid request"})");
    http::CurlInternalApiClient client(server.baseUrl(), "test-secret");

    CHECK_FALSE(client.createInvite("g_1", "u_1", std::nullopt, std::nullopt).has_value());
}

TEST_CASE("CurlInternalApiClient::fetchInvites parses the list and sends guild_id as a query param",
          "[CurlInternalApiClient]") {
    TestHttpServer server(200, R"({
        "invites": [
            {"code": "abc", "max_uses": null, "use_count": 3, "expires_at": null, "revoked_at": null, "created_at": "2026-01-01T00:00:00Z"}
        ]
    })");
    http::CurlInternalApiClient client(server.baseUrl(), "test-secret");

    const auto invites = client.fetchInvites("g_1");
    REQUIRE(invites.has_value());
    REQUIRE(invites->size() == 1);
    CHECK((*invites)[0].use_count == 3);

    const auto request = server.waitForRequest();
    REQUIRE(request.has_value());
    CHECK(request->path == "/internal/guild-invites?guild_id=g_1");
}

TEST_CASE("CurlInternalApiClient::revokeInvite issues DELETE with guild_id as a query param",
          "[CurlInternalApiClient]") {
    TestHttpServer server(200, R"({"ok": true})");
    http::CurlInternalApiClient client(server.baseUrl(), "test-secret");

    CHECK(client.revokeInvite("g_1", "abc123"));

    const auto request = server.waitForRequest();
    REQUIRE(request.has_value());
    CHECK(request->method == "DELETE");
    CHECK(request->path == "/internal/guild-invites/abc123?guild_id=g_1");
}

TEST_CASE("CurlInternalApiClient::redeemInvite parses an ok:true member result",
          "[CurlInternalApiClient]") {
    TestHttpServer server(200, R"({"ok": true, "kind": "member", "guild_id": "g_1", "role_rank": 0})");
    http::CurlInternalApiClient client(server.baseUrl(), "test-secret");

    const auto result = client.redeemInvite("abc123", "u_2");
    CHECK(result.ok);
    CHECK_FALSE(result.is_join_request);
    CHECK(result.guild_id == "g_1");
    REQUIRE(result.role_rank.has_value());
    CHECK(*result.role_rank == 0);

    const auto request = server.waitForRequest();
    REQUIRE(request.has_value());
    CHECK(request->path == "/internal/guild-invites/abc123/redeem");
}

TEST_CASE("CurlInternalApiClient::redeemInvite parses an ok:true join_request result",
          "[CurlInternalApiClient]") {
    TestHttpServer server(200, R"({"ok": true, "kind": "join_request", "guild_id": "g_1"})");
    http::CurlInternalApiClient client(server.baseUrl(), "test-secret");

    const auto result = client.redeemInvite("abc123", "u_2");
    CHECK(result.ok);
    CHECK(result.is_join_request);
    CHECK(result.guild_id == "g_1");
    CHECK_FALSE(result.role_rank.has_value());
}

TEST_CASE("CurlInternalApiClient::redeemInvite parses each ok:false error", "[CurlInternalApiClient]") {
    auto check_error = [](const std::string& wire_error, http::RedeemInviteError expected) {
        TestHttpServer server(200, R"({"ok": false, "error": ")" + wire_error + R"("})");
        http::CurlInternalApiClient client(server.baseUrl(), "test-secret");
        const auto result = client.redeemInvite("abc123", "u_2");
        CHECK_FALSE(result.ok);
        CHECK(result.error == expected);
    };

    check_error("not_found", http::RedeemInviteError::NOT_FOUND);
    check_error("revoked", http::RedeemInviteError::REVOKED);
    check_error("expired", http::RedeemInviteError::EXPIRED);
    check_error("max_uses_reached", http::RedeemInviteError::MAX_USES_REACHED);
    check_error("already_member", http::RedeemInviteError::ALREADY_MEMBER);
}

TEST_CASE("CurlInternalApiClient::redeemInvite returns a failed result on a non-200",
          "[CurlInternalApiClient]") {
    TestHttpServer server(401, R"({"error": "unauthorized"})");
    http::CurlInternalApiClient client(server.baseUrl(), "test-secret");

    const auto result = client.redeemInvite("abc123", "u_2");
    CHECK_FALSE(result.ok);
    CHECK(result.error == http::RedeemInviteError::FAILED);
}

TEST_CASE("CurlInternalApiClient::createJoinRequest parses created and already_pending",
          "[CurlInternalApiClient]") {
    {
        TestHttpServer server(201, R"({"result": "created"})");
        http::CurlInternalApiClient client(server.baseUrl(), "test-secret");
        CHECK(client.createJoinRequest("g_1", "u_2") == http::CreateJoinRequestResult::CREATED);

        const auto request = server.waitForRequest();
        REQUIRE(request.has_value());
        CHECK(request->path == "/internal/guild-join-requests");
    }
    {
        TestHttpServer server(201, R"({"result": "already_pending"})");
        http::CurlInternalApiClient client(server.baseUrl(), "test-secret");
        CHECK(client.createJoinRequest("g_1", "u_2") == http::CreateJoinRequestResult::ALREADY_PENDING);
    }
}

TEST_CASE("CurlInternalApiClient::createJoinRequest returns FAILED on a non-201",
          "[CurlInternalApiClient]") {
    TestHttpServer server(400, R"({"error": "invalid request"})");
    http::CurlInternalApiClient client(server.baseUrl(), "test-secret");

    CHECK(client.createJoinRequest("g_1", "u_2") == http::CreateJoinRequestResult::FAILED);
}

TEST_CASE("CurlInternalApiClient::fetchJoinRequests parses the list and sends guild_id as a query param",
          "[CurlInternalApiClient]") {
    TestHttpServer server(
        200,
        R"({"requests": [{"user_id": "u_2", "username": "bob", "requested_at": "2026-01-01T00:00:00Z"}]})");
    http::CurlInternalApiClient client(server.baseUrl(), "test-secret");

    const auto requests = client.fetchJoinRequests("g_1");
    REQUIRE(requests.has_value());
    REQUIRE(requests->size() == 1);
    CHECK((*requests)[0].username == "bob");

    const auto request = server.waitForRequest();
    REQUIRE(request.has_value());
    CHECK(request->path == "/internal/guild-join-requests?guild_id=g_1");
}

TEST_CASE("CurlInternalApiClient::approveJoinRequest posts to the composite approve path",
          "[CurlInternalApiClient]") {
    TestHttpServer server(200, R"({"role_rank": 0})");
    http::CurlInternalApiClient client(server.baseUrl(), "test-secret");

    const auto result = client.approveJoinRequest("g_1", "u_2");
    REQUIRE(result.has_value());
    CHECK(*result == 0);

    const auto request = server.waitForRequest();
    REQUIRE(request.has_value());
    CHECK(request->method == "POST");
    CHECK(request->path == "/internal/guild-join-requests/g_1/u_2/approve");
}

TEST_CASE("CurlInternalApiClient::approveJoinRequest returns nullopt on a 404",
          "[CurlInternalApiClient]") {
    TestHttpServer server(404, R"({"error": "not found"})");
    http::CurlInternalApiClient client(server.baseUrl(), "test-secret");

    CHECK_FALSE(client.approveJoinRequest("g_1", "u_404").has_value());
}

TEST_CASE("CurlInternalApiClient::rejectJoinRequest issues DELETE to the composite path",
          "[CurlInternalApiClient]") {
    TestHttpServer server(200, R"({"ok": true})");
    http::CurlInternalApiClient client(server.baseUrl(), "test-secret");

    CHECK(client.rejectJoinRequest("g_1", "u_2"));

    const auto request = server.waitForRequest();
    REQUIRE(request.has_value());
    CHECK(request->method == "DELETE");
    CHECK(request->path == "/internal/guild-join-requests/g_1/u_2");
}
