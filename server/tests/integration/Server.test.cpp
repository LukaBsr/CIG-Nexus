#include <catch2/catch_test_macros.hpp>

#include "Server.hpp"

#include "../auth/TestJwtHelper.hpp"
#include "../http/FakeInternalApiClient.hpp"

#include <arpa/inet.h>
#include <chrono>
#include <netinet/in.h>
#include <nlohmann/json.hpp>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

// ----------------------------------------------------------------------------
// TCP helpers shared by integration tests below
// ----------------------------------------------------------------------------

namespace {

int tcp_connect(uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    struct timeval tv = {2, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

void send_framed(int fd, const std::string& json) {
    uint32_t size_be = htonl(static_cast<uint32_t>(json.size()));
    ::send(fd, &size_be, 4, 0);
    ::send(fd, json.data(), json.size(), 0);
}

std::string recv_frame_raw(int fd) {
    uint32_t size_be = 0;
    if (::recv(fd, &size_be, 4, MSG_WAITALL) != 4)
        return "";
    uint32_t size = ntohl(size_be);
    std::string buf(size, '\0');
    if (::recv(fd, buf.data(), size, MSG_WAITALL) != static_cast<ssize_t>(size))
        return "";
    return buf;
}

// docs/social-presence-design.md §3.2: PRESENCE_UPDATE broadcasts to every
// open connection, including the one whose own IDENTIFY just triggered it
// (same delivery model CHAT_MESSAGE already uses) and every other already-
// connected socket. The tests below that predate presence, and every test
// that doesn't specifically assert on presence, use this instead of the raw
// primitive above so an interleaved PRESENCE_UPDATE never gets mistaken for
// the response a test is actually waiting for. Tests that DO want to
// observe a PRESENCE_UPDATE call recv_frame_raw() directly.
std::string recv_framed(int fd) {
    while (true) {
        std::string frame = recv_frame_raw(fd);
        if (frame.empty()) {
            return frame;
        }
        const auto parsed = nlohmann::json::parse(frame, nullptr, false);
        if (parsed.is_discarded() || parsed.value("type", "") != "PRESENCE_UPDATE") {
            return frame;
        }
    }
}

void set_recv_timeout(int fd, long seconds, long microseconds) {
    struct timeval tv {
        seconds, microseconds
    };
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

uint64_t now_seconds() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count());
}

// Signs a session_token for IDENTIFY (design doc §6/§8) — every caller in
// this file shares one server-wide keypair (configured via
// Server::configureAuth) but gets a distinct sub/username/sid per identity.
std::string make_session_token(EVP_PKEY* key, const std::string& user_id,
                               const std::string& username) {
    nlohmann::json header{{"alg", "RS256"}, {"typ", "JWT"}};
    nlohmann::json payload{{"sub", user_id},         {"discord_id", user_id},
                           {"username", username},   {"sid", user_id + "-sid"},
                           {"iat", now_seconds()},   {"exp", now_seconds() + 900},
                           {"iss", "cig-nexus-web"}, {"aud", "cig-nexus-server"}};
    return test_helpers::signTestJwt(key, header, payload);
}

} // namespace

// ----------------------------------------------------------------------------
// Fix #2: stop() causes start() to return (exercises the atomic running_ flag
// and verifies the signal-handler target works correctly).
// ----------------------------------------------------------------------------

TEST_CASE("Server exits start() when stop() is called externally") {
    Server server(0); // port 0: OS assigns a free ephemeral port
    std::thread t([&server] { server.start(); });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    server.stop();
    t.join(); // would hang indefinitely if stop() had no effect

    REQUIRE(true);
}

// ----------------------------------------------------------------------------
// Fix #3: server sends PROTOCOL_VIOLATION ERROR for unknown message type.
// ----------------------------------------------------------------------------

TEST_CASE("Server returns PROTOCOL_VIOLATION for unknown message type") {
    Server server(0); // port 0: OS assigns a free ephemeral port
    std::thread t([&server] { server.start(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    int fd = tcp_connect(server.bound_port());
    REQUIRE(fd >= 0);

    // Complete the HELLO handshake so the server is in a normal state
    send_framed(fd, R"({"type":"HELLO","version":"0.1","client":"web"})");
    std::string welcome = recv_framed(fd);
    REQUIRE(!welcome.empty());
    REQUIRE(nlohmann::json::parse(welcome)["type"] == "WELCOME");

    // Send a message type the dispatcher has no handler for
    send_framed(fd, R"({"type":"POKE","data":"test"})");
    std::string response = recv_framed(fd);
    REQUIRE(!response.empty());

    ::close(fd);
    server.stop();
    t.join();

    auto parsed = nlohmann::json::parse(response);
    REQUIRE(parsed["type"] == "ERROR");
    REQUIRE(parsed["code"] == "PROTOCOL_VIOLATION");
}

// ----------------------------------------------------------------------------
// GuildHandler is wired into Server's dispatcher. The GuildHandler unit
// tests call its methods directly and never exercise this registration, so
// this is the only coverage that CREATE_GUILD actually reaches it via a real
// socket round trip.
// ----------------------------------------------------------------------------

TEST_CASE("Server handles CREATE_GUILD end-to-end") {
    test_helpers::TestRsaKeyPair keys;
    Server server(0);
    server.configureAuth(keys.publicKeyPem());
    server.setInternalApiClient(std::make_unique<test_helpers::FakeInternalApiClient>());

    std::thread t([&server] { server.start(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    int fd = tcp_connect(server.bound_port());
    REQUIRE(fd >= 0);

    send_framed(fd, R"({"type":"HELLO","version":"0.1","client":"web"})");
    REQUIRE(!recv_framed(fd).empty());
    send_framed(fd, R"({"type":"IDENTIFY","session_token":")" +
                        make_session_token(keys.key, "u_alice", "alice") + R"("})");
    REQUIRE(!recv_framed(fd).empty());

    send_framed(fd, R"({"type":"CREATE_GUILD","name":"My Guild"})");
    std::string response = recv_framed(fd);

    ::close(fd);
    server.stop();
    t.join();

    REQUIRE(!response.empty());
    auto parsed = nlohmann::json::parse(response);
    REQUIRE(parsed["type"] == "GUILD_CREATED");
    REQUIRE(parsed["name"] == "My Guild");
    REQUIRE(parsed["guild_id"] == "g_fake_1"); // assigned by the fake InternalApiClient
}

// ----------------------------------------------------------------------------
// Scope::TARGETED end-to-end: CHANNEL_MESSAGE is the first handler that
// actually emits it (see the Scope::TARGETED commit — this closes out the
// "behavioral coverage lands with CHANNEL_MESSAGE" note from that step).
// Three real connections, only two of them joined to the channel; only
// those two may receive the broadcast.
// ----------------------------------------------------------------------------

TEST_CASE("Server delivers CHANNEL_MESSAGE only to connections with that channel active") {
    test_helpers::TestRsaKeyPair keys;
    Server server(0);
    server.configureAuth(keys.publicKeyPem());
    server.setInternalApiClient(std::make_unique<test_helpers::FakeInternalApiClient>());

    std::thread t([&server] { server.start(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto identify = [&](int fd, const std::string& user_id, const std::string& username) {
        send_framed(fd, R"({"type":"HELLO","version":"0.1","client":"web"})");
        REQUIRE(!recv_framed(fd).empty());
        send_framed(fd, R"({"type":"IDENTIFY","session_token":")" +
                            make_session_token(keys.key, user_id, username) + R"("})");
        REQUIRE(!recv_framed(fd).empty());
    };

    int fd_a = tcp_connect(server.bound_port());
    REQUIRE(fd_a >= 0);
    identify(fd_a, "u_alice", "alice");

    int fd_b = tcp_connect(server.bound_port());
    REQUIRE(fd_b >= 0);
    identify(fd_b, "u_bob", "bob");

    int fd_c = tcp_connect(server.bound_port());
    REQUIRE(fd_c >= 0);
    identify(fd_c, "u_carol", "carol");

    // alice creates a guild and a channel in it.
    send_framed(fd_a, R"({"type":"CREATE_GUILD","name":"My Guild"})");
    auto guild_created = nlohmann::json::parse(recv_framed(fd_a));
    const std::string guild_id = guild_created["guild_id"];

    send_framed(fd_a, R"({"type":"CREATE_CHANNEL","guild_id":")" + guild_id +
                          R"(","name":"general","channel_type":"TEXT"})");
    auto channel_created = nlohmann::json::parse(recv_framed(fd_a));
    const std::string channel_id = channel_created["channel_id"];

    // bob and carol join the guild so they're eligible to join the channel.
    send_framed(fd_b, R"({"type":"JOIN_GUILD","guild_id":")" + guild_id + R"("})");
    REQUIRE(!recv_framed(fd_b).empty());
    send_framed(fd_c, R"({"type":"JOIN_GUILD","guild_id":")" + guild_id + R"("})");
    REQUIRE(!recv_framed(fd_c).empty());

    // alice and bob join the channel; carol deliberately does not.
    send_framed(fd_a, R"({"type":"JOIN_CHANNEL","channel_id":")" + channel_id + R"("})");
    REQUIRE(!recv_framed(fd_a).empty());
    send_framed(fd_b, R"({"type":"JOIN_CHANNEL","channel_id":")" + channel_id + R"("})");
    REQUIRE(!recv_framed(fd_b).empty());

    send_framed(fd_a, R"({"type":"CHANNEL_MESSAGE","content":"hello channel"})");

    std::string a_response = recv_framed(fd_a);
    std::string b_response = recv_framed(fd_b);

    // Shorten carol's timeout for the negative check so this test doesn't
    // eat the full 2s default SO_RCVTIMEO waiting for something that should
    // never arrive.
    struct timeval short_tv {
        0, 300000
    };
    ::setsockopt(fd_c, SOL_SOCKET, SO_RCVTIMEO, &short_tv, sizeof(short_tv));
    std::string c_response = recv_framed(fd_c);

    ::close(fd_a);
    ::close(fd_b);
    ::close(fd_c);
    server.stop();
    t.join();

    REQUIRE(!a_response.empty());
    REQUIRE(!b_response.empty());
    REQUIRE(c_response.empty());

    auto parsed_a = nlohmann::json::parse(a_response);
    REQUIRE(parsed_a["type"] == "CHANNEL_MESSAGE");
    REQUIRE(parsed_a["channel_id"] == channel_id);
    REQUIRE(parsed_a["content"] == "hello channel");

    auto parsed_b = nlohmann::json::parse(b_response);
    REQUIRE(parsed_b["type"] == "CHANNEL_MESSAGE");
    REQUIRE(parsed_b["channel_id"] == channel_id);
}

// ----------------------------------------------------------------------------
// SIGPIPE fix: send_all() writes with MSG_NOSIGNAL. Without it, broadcasting
// to a connection whose peer already RST the socket would raise SIGPIPE and
// kill the whole process (default disposition), not just fail that one send.
// A clean close() (FIN) doesn't reproduce this — it has to be a reset.
// ----------------------------------------------------------------------------

TEST_CASE("Server survives broadcasting to a connection reset by its peer") {
    test_helpers::TestRsaKeyPair keys;
    Server server(0);
    server.configureAuth(keys.publicKeyPem());

    std::thread t([&server] { server.start(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto identify = [&](int fd, const std::string& user_id, const std::string& username) {
        send_framed(fd, R"({"type":"HELLO","version":"0.1","client":"web"})");
        REQUIRE(!recv_framed(fd).empty()); // WELCOME
        send_framed(fd, R"({"type":"IDENTIFY","session_token":")" +
                            make_session_token(keys.key, user_id, username) + R"("})");
        REQUIRE(!recv_framed(fd).empty()); // IDENTIFIED
    };

    // Connect fd_b first so it lands in an earlier unordered_map bucket than
    // fd_a: Server iterates connections_ once per tick, checking
    // readFromSocket() before processing each connection's frames, so
    // whichever of the two is visited first this tick determines whether
    // fd_a's own disconnect gets discovered (and it self-heals) before or
    // after fd_b's CHAT_MESSAGE triggers a broadcast into it.
    int fd_b = tcp_connect(server.bound_port());
    REQUIRE(fd_b >= 0);
    identify(fd_b, "u_bob", "bob");

    int fd_a = tcp_connect(server.bound_port());
    REQUIRE(fd_a >= 0);
    identify(fd_a, "u_alice", "alice");

    // Force an RST instead of a clean FIN. Server::start() checks every
    // connection's readFromSocket() once per tick, so an RST on fd_a
    // normally self-heals within a tick or two regardless of ordering —
    // the bucket ordering above is what gets fd_b's broadcast to race
    // ahead of that self-heal within the same tick, at least once.
    struct linger sl {
        1, 0
    };
    ::setsockopt(fd_a, SOL_SOCKET, SO_LINGER, &sl, sizeof(sl));
    ::close(fd_a);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    send_framed(fd_b, R"({"type":"CHAT_MESSAGE","content":"still alive"})");
    std::string response = recv_framed(fd_b);

    ::close(fd_b);
    server.stop();
    t.join(); // would never be reached if send_all() crashed the process

    REQUIRE(!response.empty());
    auto parsed = nlohmann::json::parse(response);
    REQUIRE(parsed["type"] == "CHAT_MESSAGE");
    REQUIRE(parsed["content"] == "still alive");
}

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
    api->catalog_to_return.guilds.push_back({"g_preexisting", "Pre-existing Guild", "u_owner", "open"});

    Server server(0);
    server.configureAuth(keys.publicKeyPem());
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
    server.configureAuth(keys.publicKeyPem());
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

// ----------------------------------------------------------------------------
// docs/social-presence-design.md §3: PRESENCE_UPDATE is a lobby-wide
// broadcast (Scope::BROADCAST, same delivery model CHAT_MESSAGE uses) fired
// only on a connection-count transition (0->1 online, 1->0 offline) — a
// second/third tab for the same user must not flicker anything, and an
// unrelated observer (bob, sharing nothing with alice) still sees it,
// proving the broadcast really is lobby-wide, not scoped to shared state.
// ----------------------------------------------------------------------------

TEST_CASE("Server broadcasts PRESENCE_UPDATE online/offline only on real transitions, not on "
          "additional tabs for the same user") {
    test_helpers::TestRsaKeyPair keys;
    Server server(0);
    server.configureAuth(keys.publicKeyPem());

    std::thread t([&server] { server.start(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto identify_raw = [&](int fd, const std::string& user_id, const std::string& username) {
        send_framed(fd, R"({"type":"HELLO","version":"0.1","client":"web"})");
        REQUIRE(!recv_frame_raw(fd).empty()); // WELCOME
        send_framed(fd, R"({"type":"IDENTIFY","session_token":")" +
                            make_session_token(keys.key, user_id, username) + R"("})");
        REQUIRE(!recv_frame_raw(fd).empty()); // IDENTIFIED
    };

    // bob is a pure observer: shares no guild/channel with alice, exists
    // only to prove presence really is lobby-wide.
    int fd_bob = tcp_connect(server.bound_port());
    REQUIRE(fd_bob >= 0);
    identify_raw(fd_bob, "u_bob", "bob");
    auto bob_online = nlohmann::json::parse(recv_frame_raw(fd_bob)); // bob's own broadcast
    REQUIRE(bob_online["type"] == "PRESENCE_UPDATE");
    REQUIRE(bob_online["user_id"] == "u_bob");
    REQUIRE(bob_online["status"] == "online");

    // alice's first tab: 0 -> 1, both bob and alice should see her come online.
    int fd_a1 = tcp_connect(server.bound_port());
    REQUIRE(fd_a1 >= 0);
    identify_raw(fd_a1, "u_alice", "alice");
    REQUIRE(!recv_frame_raw(fd_a1).empty()); // alice's own broadcast, drained without inspecting

    auto alice_online = nlohmann::json::parse(recv_frame_raw(fd_bob));
    REQUIRE(alice_online["type"] == "PRESENCE_UPDATE");
    REQUIRE(alice_online["user_id"] == "u_alice");
    REQUIRE(alice_online["status"] == "online");

    // alice's second tab: 1 -> 2, no transition, nothing should broadcast.
    int fd_a2 = tcp_connect(server.bound_port());
    REQUIRE(fd_a2 >= 0);
    identify_raw(fd_a2, "u_alice", "alice");
    set_recv_timeout(fd_bob, 0, 300000);
    REQUIRE(recv_frame_raw(fd_bob).empty());
    set_recv_timeout(fd_a1, 0, 300000);
    REQUIRE(recv_frame_raw(fd_a1).empty());
    set_recv_timeout(fd_bob, 2, 0);
    set_recv_timeout(fd_a1, 2, 0);

    // Closing the second tab: 2 -> 1, still online, nothing should broadcast.
    ::close(fd_a2);
    std::this_thread::sleep_for(std::chrono::milliseconds(150)); // let the server notice
    set_recv_timeout(fd_bob, 0, 300000);
    REQUIRE(recv_frame_raw(fd_bob).empty());
    set_recv_timeout(fd_bob, 2, 0);

    // Closing the last tab: 1 -> 0, bob should see alice go offline.
    ::close(fd_a1);
    auto alice_offline = nlohmann::json::parse(recv_frame_raw(fd_bob));
    REQUIRE(alice_offline["type"] == "PRESENCE_UPDATE");
    REQUIRE(alice_offline["user_id"] == "u_alice");
    REQUIRE(alice_offline["status"] == "offline");

    ::close(fd_bob);
    server.stop();
    t.join();
}
