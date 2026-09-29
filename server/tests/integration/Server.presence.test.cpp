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
// docs/guilds/social-presence-design.md §3: PRESENCE_UPDATE is a lobby-wide
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
    server.configureAuth(keys.publicKeyPem(), test_helpers::kTestIssuer);

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

// ----------------------------------------------------------------------------
// docs/social/friends-dms-design.md §2.5: a user alice has blocked must not
// see alice's PRESENCE_UPDATE broadcasts, while an unrelated third
// connection (carol, blocked by no one) still does — proving this is a
// targeted exclusion of the blocked user's connections specifically, not a
// suppression of the broadcast for everyone. FakeInternalApiClient's
// blocks_to_return is shared process-wide, so bob and carol identify
// *before* it's populated (hydrating empty blocked_user_ids for both);
// it's set to alice's block list only right before alice's own IDENTIFY.
// ----------------------------------------------------------------------------

TEST_CASE("Server excludes a blocked user's connections from the blocker's PRESENCE_UPDATE") {
    test_helpers::TestRsaKeyPair keys;
    Server server(0);
    server.configureAuth(keys.publicKeyPem(), test_helpers::kTestIssuer);
    auto api = std::make_unique<test_helpers::FakeInternalApiClient>();
    test_helpers::FakeInternalApiClient* api_ptr = api.get();
    server.setInternalApiClient(std::move(api));

    std::thread t([&server] { server.start(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto identify_raw = [&](int fd, const std::string& user_id, const std::string& username) {
        send_framed(fd, R"({"type":"HELLO","version":"0.1","client":"web"})");
        REQUIRE(!recv_frame_raw(fd).empty()); // WELCOME
        send_framed(fd, R"({"type":"IDENTIFY","session_token":")" +
                            make_session_token(keys.key, user_id, username) + R"("})");
        REQUIRE(!recv_frame_raw(fd).empty()); // IDENTIFIED
    };

    // carol connects first, as a pure observer.
    int fd_carol = tcp_connect(server.bound_port());
    REQUIRE(fd_carol >= 0);
    identify_raw(fd_carol, "u_carol", "carol");
    REQUIRE(!recv_frame_raw(fd_carol).empty()); // carol's own online broadcast, drained

    // bob connects second — carol observes it, proving unrelated presence
    // still works normally (this is the sanity check the exclusion-specific
    // assertions below are contrasted against).
    int fd_bob = tcp_connect(server.bound_port());
    REQUIRE(fd_bob >= 0);
    identify_raw(fd_bob, "u_bob", "bob");
    REQUIRE(!recv_frame_raw(fd_bob).empty()); // bob's own online broadcast, drained
    auto bob_online_seen_by_carol = nlohmann::json::parse(recv_frame_raw(fd_carol));
    REQUIRE(bob_online_seen_by_carol["user_id"] ==
            "u_bob"); // sanity: carol sees unrelated presence normally

    // alice has blocked bob — hydrated at her IDENTIFY below.
    api_ptr->blocks_to_return = {
        {"u_bob", "bob", "2026-01-01T00:00:00Z", std::nullopt, std::nullopt}};

    int fd_alice = tcp_connect(server.bound_port());
    REQUIRE(fd_alice >= 0);
    identify_raw(fd_alice, "u_alice", "alice");
    REQUIRE(!recv_frame_raw(fd_alice).empty()); // alice's own broadcast, drained

    // carol (not blocked) sees alice come online.
    auto alice_online_seen_by_carol = nlohmann::json::parse(recv_frame_raw(fd_carol));
    REQUIRE(alice_online_seen_by_carol["type"] == "PRESENCE_UPDATE");
    REQUIRE(alice_online_seen_by_carol["user_id"] == "u_alice");
    REQUIRE(alice_online_seen_by_carol["status"] == "online");

    // bob (blocked by alice) does not see alice come online.
    set_recv_timeout(fd_bob, 0, 300000);
    REQUIRE(recv_frame_raw(fd_bob).empty());
    set_recv_timeout(fd_bob, 2, 0);

    // Same exclusion on the offline transition.
    ::close(fd_alice);
    auto alice_offline_seen_by_carol = nlohmann::json::parse(recv_frame_raw(fd_carol));
    REQUIRE(alice_offline_seen_by_carol["type"] == "PRESENCE_UPDATE");
    REQUIRE(alice_offline_seen_by_carol["user_id"] == "u_alice");
    REQUIRE(alice_offline_seen_by_carol["status"] == "offline");

    set_recv_timeout(fd_bob, 0, 300000);
    REQUIRE(recv_frame_raw(fd_bob).empty());

    ::close(fd_bob);
    ::close(fd_carol);
    server.stop();
    t.join();
}

// ----------------------------------------------------------------------------
// Regression: a user reconnecting repeatedly (every browser reload is a
// fresh TCP connection) must reliably see their own online transition every
// single time, not just the first. A leaked presence count — some disconnect
// path failing to reach decrementPresence() for one of the earlier
// connections — would manifest as later reconnects silently failing to
// broadcast online, since the count never actually returns to 0.
// ----------------------------------------------------------------------------

TEST_CASE("Server broadcasts online on every reconnect, not just the first, across repeated "
          "connect/disconnect cycles") {
    test_helpers::TestRsaKeyPair keys;
    Server server(0);
    server.configureAuth(keys.publicKeyPem(), test_helpers::kTestIssuer);

    std::thread t([&server] { server.start(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // A pure observer, same role bob plays above: proves the broadcast is
    // real (lobby-wide), not just alice's own connection echoing itself.
    int fd_observer = tcp_connect(server.bound_port());
    REQUIRE(fd_observer >= 0);
    send_framed(fd_observer, R"({"type":"HELLO","version":"0.1","client":"web"})");
    REQUIRE(!recv_frame_raw(fd_observer).empty()); // WELCOME
    send_framed(fd_observer, R"({"type":"IDENTIFY","session_token":")" +
                                 make_session_token(keys.key, "u_observer", "observer") + R"("})");
    REQUIRE(!recv_frame_raw(fd_observer).empty()); // IDENTIFIED
    REQUIRE(!recv_frame_raw(fd_observer).empty()); // observer's own online broadcast

    for (int cycle = 0; cycle < 5; ++cycle) {
        INFO("reconnect cycle " << cycle);

        int fd_alice = tcp_connect(server.bound_port());
        REQUIRE(fd_alice >= 0);
        send_framed(fd_alice, R"({"type":"HELLO","version":"0.1","client":"web"})");
        REQUIRE(!recv_frame_raw(fd_alice).empty()); // WELCOME
        send_framed(fd_alice, R"({"type":"IDENTIFY","session_token":")" +
                                  make_session_token(keys.key, "u_alice", "alice") + R"("})");
        REQUIRE(!recv_frame_raw(fd_alice).empty()); // IDENTIFIED
        REQUIRE(!recv_frame_raw(fd_alice).empty()); // alice's own online broadcast, drained

        // The observer must see alice come online on *every* cycle — this
        // is the assertion that fails if an earlier cycle's disconnect
        // leaked its presence count.
        set_recv_timeout(fd_observer, 2, 0);
        std::string raw = recv_frame_raw(fd_observer);
        REQUIRE(!raw.empty());
        auto alice_online = nlohmann::json::parse(raw);
        REQUIRE(alice_online["type"] == "PRESENCE_UPDATE");
        REQUIRE(alice_online["user_id"] == "u_alice");
        REQUIRE(alice_online["status"] == "online");

        ::close(fd_alice);

        // Alice must also be seen going offline before the next cycle's
        // reconnect — otherwise a slow-to-notice disconnect could make the
        // *next* cycle's "come online" assertion above pass for the wrong
        // reason (a fresh 0->1 that raced a stale, not-yet-processed close).
        std::string offline_raw = recv_frame_raw(fd_observer);
        REQUIRE(!offline_raw.empty());
        auto alice_offline = nlohmann::json::parse(offline_raw);
        REQUIRE(alice_offline["type"] == "PRESENCE_UPDATE");
        REQUIRE(alice_offline["user_id"] == "u_alice");
        REQUIRE(alice_offline["status"] == "offline");
    }

    ::close(fd_observer);
    server.stop();
    t.join();
}
