#include <catch2/catch_test_macros.hpp>

#include "Server.hpp"

#include "../auth/TestJwtHelper.hpp"
#include "../http/FakeInternalApiClient.hpp"
#include "ServerTestSupport.hpp"

#include <arpa/inet.h>
#include <chrono>
#include <netinet/in.h>
#include <nlohmann/json.hpp>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

using namespace server_test_support;

// ----------------------------------------------------------------------------
// design doc §8.1: GuildManager is a write-through cache hydrated from the
// internal API's full-catalog snapshot at startup — a guild that already
// existed in Postgres before this process started must be immediately
// visible via LIST_GUILDS, without ever going through CREATE_GUILD on this
// connection.
// ----------------------------------------------------------------------------

TEST_CASE("Server hydrates the guild catalog from InternalApiClient at startup") {
    test_helpers::TestRsaKeyPair keys;
    auto api = std::make_unique<test_helpers::FakeInternalApiClient>();
    api->catalog_to_return.guilds.push_back(
        {"g_preexisting", "Pre-existing Guild", "u_owner", "open"});

    Server server(0);
    server.configureAuth(keys.publicKeyPem(), test_helpers::kTestIssuer);
    server.setInternalApiClient(std::move(api));

    std::thread t([&server] { server.start(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    int fd = tcp_connect(server.bound_port());
    REQUIRE(fd >= 0);

    send_framed(fd, R"({"type":"HELLO","version":"0.1","client":"web"})");
    REQUIRE(!recv_framed(fd).empty());
    send_framed(fd, R"({"type":"IDENTIFY","session_token":")" +
                        make_session_token(keys.key, "u_alice", "alice") + R"("})");
    REQUIRE(!recv_framed(fd).empty());

    send_framed(fd, R"({"type":"LIST_GUILDS"})");
    std::string response = recv_framed(fd);

    ::close(fd);
    server.stop();
    t.join();

    REQUIRE(!response.empty());
    auto parsed = nlohmann::json::parse(response);
    REQUIRE(parsed["type"] == "GUILD_LIST");
    REQUIRE(parsed["guilds"].size() == 1);
    REQUIRE(parsed["guilds"][0]["guild_id"] == "g_preexisting");
    REQUIRE(parsed["guilds"][0]["name"] == "Pre-existing Guild");
}

// ----------------------------------------------------------------------------
// docs/security-audit.md §1.5: a session revoked before this process started
// must not become valid again just because the process restarted —
// pollRevocationCache() has to run once, synchronously, before the accept
// loop (mirroring hydrateGuildCatalog() above), not only on the first
// interval tick ~30s later. This test only waits 50ms, so it would fail on
// a revert to the interval-only behavior.
// ----------------------------------------------------------------------------

TEST_CASE("Server rejects an already-revoked session immediately at startup, without waiting for "
          "the poll interval") {
    test_helpers::TestRsaKeyPair keys;
    auto api = std::make_unique<test_helpers::FakeInternalApiClient>();
    api->revoked_ids_to_return.push_back("u_alice-sid");

    Server server(0);
    server.configureAuth(keys.publicKeyPem(), test_helpers::kTestIssuer);
    server.setInternalApiClient(std::move(api));

    std::thread t([&server] { server.start(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    int fd = tcp_connect(server.bound_port());
    REQUIRE(fd >= 0);

    send_framed(fd, R"({"type":"HELLO","version":"0.1","client":"web"})");
    REQUIRE(!recv_framed(fd).empty());
    send_framed(fd, R"({"type":"IDENTIFY","session_token":")" +
                        make_session_token(keys.key, "u_alice", "alice") + R"("})");
    std::string response = recv_framed(fd);

    ::close(fd);
    server.stop();
    t.join();

    REQUIRE(!response.empty());
    auto parsed = nlohmann::json::parse(response);
    REQUIRE(parsed["type"] == "ERROR");
    REQUIRE(parsed["code"] == "SESSION_REVOKED");
}
