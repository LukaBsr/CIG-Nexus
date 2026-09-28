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
// IDENTIFY hardening (B2): the post-IDENTIFY blocks/friends/profile load now
// runs on a background worker instead of blocking Server::start()'s
// single-threaded main loop. These tests exercise that against the real
// Server class (not a unit-level fake), since the whole point is proving the
// main loop itself stays responsive, not just that the worker class works in
// isolation (see tests/session/SessionHydrationWorker.test.cpp for that).
// ----------------------------------------------------------------------------

TEST_CASE("Server keeps other connections responsive during one connection's slow IDENTIFY "
          "hydration") {
    test_helpers::TestRsaKeyPair keys;
    Server server(0);
    server.configureAuth(keys.publicKeyPem());
    auto api = std::make_unique<test_helpers::FakeInternalApiClient>();
    api->session_context_delay = std::chrono::milliseconds(400);
    server.setInternalApiClient(std::move(api));

    std::thread t([&server] { server.start(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    int fd_alice = tcp_connect(server.bound_port());
    REQUIRE(fd_alice >= 0);
    send_framed(fd_alice, R"({"type":"HELLO","version":"0.1","client":"web"})");
    REQUIRE(!recv_framed(fd_alice).empty()); // WELCOME
    send_framed(fd_alice, R"({"type":"IDENTIFY","session_token":")" +
                              make_session_token(keys.key, "u_alice", "alice") + R"("})");
    // IDENTIFIED must arrive immediately — it does not wait for the 400ms
    // hydration delay. A generous timeout here just confirms it isn't
    // pathologically slow; the real assertion is bob's round trip below.
    REQUIRE(!recv_framed(fd_alice).empty());

    int fd_bob = tcp_connect(server.bound_port());
    REQUIRE(fd_bob >= 0);
    send_framed(fd_bob, R"({"type":"HELLO","version":"0.1","client":"web"})");
    REQUIRE(!recv_framed(fd_bob).empty()); // WELCOME
    send_framed(fd_bob, R"({"type":"IDENTIFY","session_token":")" +
                            make_session_token(keys.key, "u_bob", "bob") + R"("})");
    REQUIRE(!recv_framed(fd_bob).empty()); // IDENTIFIED

    // The real assertion: bob's own, entirely unrelated CHAT_MESSAGE round
    // trip completes well within alice's 400ms hydration delay. Before B2,
    // this main loop was still single-threaded through IdentifyHandler's own
    // synchronous internal-API call, so alice's slow load would have stalled
    // bob's frame from ever being read/dispatched until it finished — this
    // is exactly the class of stall B1/B2 exist to fix, this time via a
    // background thread rather than fewer calls.
    set_recv_timeout(fd_bob, 0, 150000); // 150ms — well under the 400ms delay
    send_framed(fd_bob, R"({"type":"CHAT_MESSAGE","content":"hello"})");
    auto response = recv_framed(fd_bob);

    ::close(fd_alice);
    ::close(fd_bob);
    server.stop();
    t.join();

    REQUIRE(!response.empty());
    auto parsed = nlohmann::json::parse(response);
    REQUIRE(parsed["type"] == "CHAT_MESSAGE");
    REQUIRE(parsed["content"] == "hello");
}

// ----------------------------------------------------------------------------
// If the connection closes before its hydration job completes, the result
// must be dropped when it eventually arrives — not crash the server, and not
// be misapplied to whatever unrelated connection/session happens to exist by
// then (docs/known-issues.md's fd-reuse-race idea, closed here by matching on
// app_session_id, not just fd).
// ----------------------------------------------------------------------------

TEST_CASE("Server drops a hydration result for a connection that already closed, without "
          "crashing or affecting other connections") {
    test_helpers::TestRsaKeyPair keys;
    Server server(0);
    server.configureAuth(keys.publicKeyPem());
    auto api = std::make_unique<test_helpers::FakeInternalApiClient>();
    api->session_context_delay = std::chrono::milliseconds(200);
    server.setInternalApiClient(std::move(api));

    std::thread t([&server] { server.start(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    int fd_alice = tcp_connect(server.bound_port());
    REQUIRE(fd_alice >= 0);
    send_framed(fd_alice, R"({"type":"HELLO","version":"0.1","client":"web"})");
    REQUIRE(!recv_framed(fd_alice).empty());
    send_framed(fd_alice, R"({"type":"IDENTIFY","session_token":")" +
                              make_session_token(keys.key, "u_alice", "alice") + R"("})");
    REQUIRE(!recv_framed(fd_alice).empty()); // IDENTIFIED

    // Close alice well before her 200ms hydration finishes.
    ::close(fd_alice);

    // Give the stale result time to complete and be drained — this is the
    // window where a naive implementation would crash (dangling session
    // pointer) or corrupt unrelated state.
    std::this_thread::sleep_for(std::chrono::milliseconds(350));

    // The server must still be fully functional: an unrelated connection
    // identifies cleanly and gets its own presence broadcast.
    int fd_bob = tcp_connect(server.bound_port());
    REQUIRE(fd_bob >= 0);
    send_framed(fd_bob, R"({"type":"HELLO","version":"0.1","client":"web"})");
    REQUIRE(!recv_frame_raw(fd_bob).empty()); // WELCOME
    send_framed(fd_bob, R"({"type":"IDENTIFY","session_token":")" +
                            make_session_token(keys.key, "u_bob", "bob") + R"("})");
    REQUIRE(!recv_frame_raw(fd_bob).empty()); // IDENTIFIED

    auto bob_online = nlohmann::json::parse(recv_frame_raw(fd_bob));

    ::close(fd_bob);
    server.stop();
    t.join(); // would never be reached if the stale result crashed the loop

    REQUIRE(bob_online["type"] == "PRESENCE_UPDATE");
    REQUIRE(bob_online["user_id"] == "u_bob");
    REQUIRE(bob_online["status"] == "online");
}

// ----------------------------------------------------------------------------
// The specific correctness hazard B2 introduces if not handled: a second tab
// for the same user whose hydration never completes must not, on disconnect,
// decrement the *first* tab's real, already-online presence count. Only a
// connection that itself completed hydration (and therefore incremented
// presence) may decrement it on the way out —
// Server::removeSessionTrackingPresence's session_context_ready check.
// ----------------------------------------------------------------------------

TEST_CASE("A second tab's hydration never completing, then disconnecting, does not make the "
          "first tab's presence go offline") {
    test_helpers::TestRsaKeyPair keys;
    Server server(0);
    server.configureAuth(keys.publicKeyPem());
    auto api = std::make_unique<test_helpers::FakeInternalApiClient>();
    test_helpers::FakeInternalApiClient* api_ptr = api.get();
    server.setInternalApiClient(std::move(api));

    std::thread t([&server] { server.start(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    int fd_bob = tcp_connect(server.bound_port());
    REQUIRE(fd_bob >= 0);
    send_framed(fd_bob, R"({"type":"HELLO","version":"0.1","client":"web"})");
    REQUIRE(!recv_frame_raw(fd_bob).empty()); // WELCOME
    send_framed(fd_bob, R"({"type":"IDENTIFY","session_token":")" +
                            make_session_token(keys.key, "u_bob", "bob") + R"("})");
    REQUIRE(!recv_frame_raw(fd_bob).empty()); // IDENTIFIED
    REQUIRE(!recv_frame_raw(fd_bob).empty()); // bob's own online broadcast

    // alice's first tab: fast hydration (api_ptr's delay is still 0 here),
    // becomes genuinely online.
    int fd_a1 = tcp_connect(server.bound_port());
    REQUIRE(fd_a1 >= 0);
    send_framed(fd_a1, R"({"type":"HELLO","version":"0.1","client":"web"})");
    REQUIRE(!recv_frame_raw(fd_a1).empty());
    send_framed(fd_a1, R"({"type":"IDENTIFY","session_token":")" +
                           make_session_token(keys.key, "u_alice", "alice") + R"("})");
    REQUIRE(!recv_frame_raw(fd_a1).empty()); // IDENTIFIED

    auto alice_online = nlohmann::json::parse(recv_frame_raw(fd_bob));
    REQUIRE(alice_online["type"] == "PRESENCE_UPDATE");
    REQUIRE(alice_online["user_id"] == "u_alice");
    REQUIRE(alice_online["status"] == "online");

    // alice's second tab: this fd_a2-specific hydration is made slow so it's
    // still pending when we close it below.
    int fd_a2 = tcp_connect(server.bound_port());
    REQUIRE(fd_a2 >= 0);
    send_framed(fd_a2, R"({"type":"HELLO","version":"0.1","client":"web"})");
    REQUIRE(!recv_frame_raw(fd_a2).empty());

    // Only now, right before the second tab's IDENTIFY, does its hydration
    // become slow — fd_a1's fetchSessionContext call already ran and
    // returned before this line, so only fd_a2's call sees the delay (the
    // fake is shared per-Server, but fetchSessionContext is invoked
    // sequentially per job, one at a time, by the single hydration worker
    // thread).
    api_ptr->session_context_delay = std::chrono::milliseconds(300);
    send_framed(fd_a2, R"({"type":"IDENTIFY","session_token":")" +
                           make_session_token(keys.key, "u_alice", "alice-tab-2") + R"("})");
    REQUIRE(!recv_frame_raw(fd_a2).empty()); // IDENTIFIED — immediate, per B2, despite the delay

    // Close the second tab well before its 300ms hydration could complete.
    ::close(fd_a2);

    // Give fd_a2's stale hydration time to actually complete and be
    // drained — this is the window the fix must hold up in.
    std::this_thread::sleep_for(std::chrono::milliseconds(450));

    // The critical assertion: bob must NOT see alice go offline — tab one
    // is still fully connected and was genuinely online.
    set_recv_timeout(fd_bob, 0, 300000);
    REQUIRE(recv_frame_raw(fd_bob).empty());

    ::close(fd_a1);
    ::close(fd_bob);
    server.stop();
    t.join();
}

// ----------------------------------------------------------------------------
// docs/security-audit.md / shared/protocol/README.md's Asynchronous IDENTIFY
// Hydration section: exhausting the retry budget must fail closed — the
// server sends SESSION_CONTEXT_UNAVAILABLE and disconnects, rather than
// proceeding with an empty, unenforced block list.
// ----------------------------------------------------------------------------

TEST_CASE("Server sends SESSION_CONTEXT_UNAVAILABLE and disconnects once hydration retries are "
          "exhausted") {
    test_helpers::TestRsaKeyPair keys;
    Server server(0);
    server.configureAuth(keys.publicKeyPem());
    // Fast schedule — the real ~60s default would make this test unusable.
    server.setHydrationRetryDelaysForTesting(
        {std::chrono::milliseconds(5), std::chrono::milliseconds(5)});
    auto api = std::make_unique<test_helpers::FakeInternalApiClient>();
    api->fail_fetch_user_profile = true; // fetchSessionContext never succeeds
    server.setInternalApiClient(std::move(api));

    std::thread t([&server] { server.start(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    int fd = tcp_connect(server.bound_port());
    REQUIRE(fd >= 0);
    send_framed(fd, R"({"type":"HELLO","version":"0.1","client":"web"})");
    REQUIRE(!recv_frame_raw(fd).empty()); // WELCOME
    send_framed(fd, R"({"type":"IDENTIFY","session_token":")" +
                        make_session_token(keys.key, "u_alice", "alice") + R"("})");
    REQUIRE(!recv_frame_raw(fd).empty()); // IDENTIFIED — sent immediately regardless

    set_recv_timeout(fd, 2, 0);
    auto error_raw = recv_frame_raw(fd);
    REQUIRE(!error_raw.empty());
    auto error = nlohmann::json::parse(error_raw);
    REQUIRE(error["type"] == "ERROR");
    REQUIRE(error["code"] == "SESSION_CONTEXT_UNAVAILABLE");

    // The server closes its end after the error — a subsequent read sees EOF
    // (empty), not a hang.
    auto after_error = recv_frame_raw(fd);
    REQUIRE(after_error.empty());

    ::close(fd);
    server.stop();
    t.join();
}
