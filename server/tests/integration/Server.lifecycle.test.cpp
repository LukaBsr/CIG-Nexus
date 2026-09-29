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
    server.configureAuth(keys.publicKeyPem(), test_helpers::kTestIssuer);
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
    server.configureAuth(keys.publicKeyPem(), test_helpers::kTestIssuer);
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
    server.configureAuth(keys.publicKeyPem(), test_helpers::kTestIssuer);

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
// BROADCAST must reach identified sessions only. Server::broadcast() used to
// iterate every open socket, so a WebSocket that never completed IDENTIFY
// (no login, no session token) still received every lobby CHAT_MESSAGE and
// PRESENCE_UPDATE — including user ids and online status. Covers both an
// utterly silent socket and one that only got as far as HELLO/WELCOME, then
// confirms that identifying is what starts delivery (not some other state).
// ----------------------------------------------------------------------------

TEST_CASE("Server does not deliver BROADCAST messages to connections that haven't identified") {
    test_helpers::TestRsaKeyPair keys;
    Server server(0);
    server.configureAuth(keys.publicKeyPem(), test_helpers::kTestIssuer);

    std::thread t([&server] { server.start(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto identify_raw = [&](int fd, const std::string& user_id, const std::string& username) {
        send_framed(fd, R"({"type":"IDENTIFY","session_token":")" +
                            make_session_token(keys.key, user_id, username) + R"("})");
        REQUIRE(!recv_frame_raw(fd).empty()); // IDENTIFIED
    };

    int fd_silent = tcp_connect(server.bound_port());
    REQUIRE(fd_silent >= 0);

    int fd_greeted = tcp_connect(server.bound_port());
    REQUIRE(fd_greeted >= 0);
    send_framed(fd_greeted, R"({"type":"HELLO","version":"0.1","client":"web"})");
    REQUIRE(!recv_frame_raw(fd_greeted).empty()); // WELCOME

    int fd_alice = tcp_connect(server.bound_port());
    REQUIRE(fd_alice >= 0);
    send_framed(fd_alice, R"({"type":"HELLO","version":"0.1","client":"web"})");
    REQUIRE(!recv_frame_raw(fd_alice).empty()); // WELCOME
    identify_raw(fd_alice, "u_alice", "alice");
    REQUIRE(!recv_frame_raw(fd_alice).empty()); // alice's own PRESENCE_UPDATE

    send_framed(fd_alice, R"({"type":"CHAT_MESSAGE","content":"secret lobby chatter"})");
    auto echoed = nlohmann::json::parse(recv_framed(fd_alice));
    REQUIRE(echoed["type"] == "CHAT_MESSAGE");

    set_recv_timeout(fd_silent, 0, 300000);
    set_recv_timeout(fd_greeted, 0, 300000);
    REQUIRE(recv_frame_raw(fd_silent).empty());
    REQUIRE(recv_frame_raw(fd_greeted).empty());

    // alice going offline is a BROADCAST too (PRESENCE_UPDATE) — same rule.
    ::close(fd_alice);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    REQUIRE(recv_frame_raw(fd_silent).empty());
    REQUIRE(recv_frame_raw(fd_greeted).empty());

    // Identifying is what starts delivery: once fd_greeted identifies as bob,
    // he receives carol's lobby chat like any other identified client.
    set_recv_timeout(fd_greeted, 2, 0);
    identify_raw(fd_greeted, "u_bob", "bob");
    REQUIRE(!recv_frame_raw(fd_greeted).empty()); // bob's own PRESENCE_UPDATE

    int fd_carol = tcp_connect(server.bound_port());
    REQUIRE(fd_carol >= 0);
    send_framed(fd_carol, R"({"type":"HELLO","version":"0.1","client":"web"})");
    REQUIRE(!recv_frame_raw(fd_carol).empty()); // WELCOME
    identify_raw(fd_carol, "u_carol", "carol");
    send_framed(fd_carol, R"({"type":"CHAT_MESSAGE","content":"hi bob"})");

    auto bob_saw = nlohmann::json::parse(recv_framed(fd_greeted));
    REQUIRE(bob_saw["type"] == "CHAT_MESSAGE");
    REQUIRE(bob_saw["content"] == "hi bob");

    // The socket that never identified is still shut out.
    set_recv_timeout(fd_silent, 0, 300000);
    REQUIRE(recv_frame_raw(fd_silent).empty());

    ::close(fd_silent);
    ::close(fd_greeted);
    ::close(fd_carol);
    server.stop();
    t.join();
}
