#include <catch2/catch_test_macros.hpp>

#include "protocol/handlers/DMHandler.hpp"
#include "protocol/handlers/DMReadStateHandler.hpp"
#include "session/SessionManager.hpp"
#include "util/RateLimiter.hpp"

#include "../http/FakeInternalApiClient.hpp"

#include <algorithm>

namespace {

protocol::Message make_message(const std::string& type, nlohmann::json extra = {}) {
    protocol::Message message;
    message.type = type;
    extra["type"] = type;
    message.payload = extra;
    return message;
}

struct Fixture {
    session::SessionManager sessions;
    test_helpers::FakeInternalApiClient api;
    util::RateLimiter limiter;
    protocol::DMHandler dm_handler;
    protocol::DMReadStateHandler handler;

    Fixture() {
        handler.setSessionManager(&sessions);
        handler.setInternalApiClient(&api);
        handler.setRateLimiter(&limiter);
        handler.setDmHandler(&dm_handler);
        // Default high enough that every existing test's up_to_message_id
        // (small literals like 1, 5, 7, 99) passes the bound check without
        // needing to seed it itself. Tests of the bound check seed a
        // specific, lower value instead.
        dm_handler.seedMessageCounter(1000);
    }

    session::Session& identify(int fd, const std::string& username) {
        session::Session& session = sessions.createSession(fd);
        session.username = username;
        session.user_id = "u_" + std::to_string(fd);
        session.session_context_ready = true;
        return session;
    }
};

} // namespace

// --- MARK_DM_READ: validation -------------------------------------------

TEST_CASE("DMReadStateHandler MARK_DM_READ requires identification") {
    Fixture f;
    const auto response = f.handler.handleMarkDmRead(
        make_message("MARK_DM_READ", {{"user_id", "u_2"}, {"up_to_message_id", 1}}), 1);
    REQUIRE(response.payload["code"] == "NOT_IDENTIFIED");
}

TEST_CASE("DMReadStateHandler MARK_DM_READ requires user_id and an integer up_to_message_id") {
    Fixture f;
    f.identify(1, "alice");
    REQUIRE(f.handler.handleMarkDmRead(make_message("MARK_DM_READ", {{"up_to_message_id", 1}}), 1)
                .payload["code"] == "MALFORMED_MESSAGE");
    REQUIRE(f.handler.handleMarkDmRead(make_message("MARK_DM_READ", {{"user_id", "u_2"}}), 1)
                .payload["code"] == "MALFORMED_MESSAGE");
    REQUIRE(
        f.handler
            .handleMarkDmRead(
                make_message("MARK_DM_READ", {{"user_id", "u_2"}, {"up_to_message_id", "1"}}), 1)
            .payload["code"] == "MALFORMED_MESSAGE");
}

TEST_CASE("DMReadStateHandler MARK_DM_READ maps the internal API's !ok to MALFORMED_MESSAGE") {
    Fixture f;
    f.identify(1, "alice");
    f.api.mark_dm_read_returns = {}; // ok = false: no conversation at all with that peer

    const auto response = f.handler.handleMarkDmRead(
        make_message("MARK_DM_READ", {{"user_id", "u_2"}, {"up_to_message_id", 99}}), 1);

    REQUIRE(response.payload["code"] == "MALFORMED_MESSAGE");
}

// --- MARK_DM_READ: up_to_message_id bound check -------------------------
//
// Reproduces, at the mechanism level, the race DM_SEND's async persistence
// creates: a client can receive DM_MESSAGE with a message_id before that
// message's row exists in Postgres. These tests show the bound check uses
// dm_handler_'s live counter, never the (here, entirely absent) database —
// FakeInternalApiClient never touches real persistence, so a value passing
// this check is, by construction, independent of whether any row for it
// exists anywhere. The actual Postgres-timing reproduction (insert nothing
// into messages, call markDmRead anyway) lives in
// web/lib/internal/dmReadState.test.ts, the only layer with a real database
// to race against.

TEST_CASE("DMReadStateHandler MARK_DM_READ rejects an up_to_message_id higher than any the "
          "server has issued") {
    Fixture f;
    f.identify(1, "alice");
    f.dm_handler.seedMessageCounter(5);

    const auto response = f.handler.handleMarkDmRead(
        make_message("MARK_DM_READ", {{"user_id", "u_2"}, {"up_to_message_id", 6}}), 1);

    REQUIRE(response.payload["code"] == "MALFORMED_MESSAGE");
    REQUIRE(f.api.mark_dm_read_call_count == 0); // rejected before ever reaching the internal API
}

TEST_CASE("DMReadStateHandler MARK_DM_READ rejects a non-positive up_to_message_id") {
    Fixture f;
    f.identify(1, "alice");

    REQUIRE(f.handler
                .handleMarkDmRead(
                    make_message("MARK_DM_READ", {{"user_id", "u_2"}, {"up_to_message_id", 0}}), 1)
                .payload["code"] == "MALFORMED_MESSAGE");
    REQUIRE(f.handler
                .handleMarkDmRead(
                    make_message("MARK_DM_READ", {{"user_id", "u_2"}, {"up_to_message_id", -1}}), 1)
                .payload["code"] == "MALFORMED_MESSAGE");
}

TEST_CASE("DMReadStateHandler MARK_DM_READ accepts an up_to_message_id exactly at the highest "
          "issued one — the value a client would have from a DM_MESSAGE it just received, "
          "whether or not that message is persisted yet") {
    Fixture f;
    f.identify(1, "alice");
    f.dm_handler.seedMessageCounter(5);
    f.api.mark_dm_read_returns = {true, 5, 0};

    const auto response = f.handler.handleMarkDmRead(
        make_message("MARK_DM_READ", {{"user_id", "u_2"}, {"up_to_message_id", 5}}), 1);

    REQUIRE(response.type == "DM_READ");
    REQUIRE(f.api.mark_dm_read_call_count == 1);
}

// --- MARK_DM_READ: success, delivery ------------------------------------

TEST_CASE("DMReadStateHandler MARK_DM_READ pushes DM_READ to every one of the caller's own "
          "sessions, self-inclusive") {
    Fixture f;
    f.identify(1, "alice");
    session::Session& alice_second_tab = f.identify(2, "alice-second-tab");
    alice_second_tab.user_id = "u_1"; // a second tab/device, same account as fd 1
    f.identify(3, "bob");             // the peer — must never receive this
    f.api.mark_dm_read_returns = {true, 7, 2};

    const auto response = f.handler.handleMarkDmRead(
        make_message("MARK_DM_READ", {{"user_id", "u_3"}, {"up_to_message_id", 7}}), 1);

    REQUIRE(response.type == "DM_READ");
    REQUIRE(response.scope == protocol::Scope::TARGETED);
    REQUIRE(response.payload["user_id"] == "u_3");
    REQUIRE(response.payload["up_to_message_id"] == 7);
    REQUIRE(response.payload["unread"] == 2);

    std::vector<int> fds = response.target_fds;
    std::sort(fds.begin(), fds.end());
    REQUIRE(fds == std::vector<int>{1, 2}); // both of alice's own sessions
}

TEST_CASE("DMReadStateHandler MARK_DM_READ never targets the peer's fd (no read receipts)") {
    Fixture f;
    f.identify(1, "alice");
    f.identify(2, "bob"); // the peer
    f.api.mark_dm_read_returns = {true, 1, 0};

    const auto response = f.handler.handleMarkDmRead(
        make_message("MARK_DM_READ", {{"user_id", "u_2"}, {"up_to_message_id", 1}}), 1);

    REQUIRE(std::find(response.target_fds.begin(), response.target_fds.end(), 2) ==
            response.target_fds.end());
}

TEST_CASE("DMReadStateHandler MARK_DM_READ reports the current marker unchanged when the "
          "internal API says a lower up_to_message_id didn't move it (monotonic, idempotent)") {
    Fixture f;
    f.identify(1, "alice");
    // The web layer already enforces monotonicity (dmReadState.test.ts); the
    // handler's job is just to relay whatever marker_seq it's given. A
    // lower request still maps to ok=true with the *current*, higher
    // marker — not an error.
    f.api.mark_dm_read_returns = {true, 5, 0};

    const auto response = f.handler.handleMarkDmRead(
        make_message("MARK_DM_READ", {{"user_id", "u_2"}, {"up_to_message_id", 1}}), 1);

    REQUIRE(response.type == "DM_READ");
    REQUIRE(response.payload["up_to_message_id"] == 5);
}

TEST_CASE("DMReadStateHandler MARK_DM_READ returns RATE_LIMITED after the per-10-second cap") {
    Fixture f;
    f.identify(1, "alice");
    f.api.mark_dm_read_returns = {true, 1, 0};

    for (int i = 0; i < 10; ++i) {
        const auto response = f.handler.handleMarkDmRead(
            make_message("MARK_DM_READ", {{"user_id", "u_2"}, {"up_to_message_id", 1}}), 1);
        REQUIRE(response.type == "DM_READ");
    }

    const auto limited = f.handler.handleMarkDmRead(
        make_message("MARK_DM_READ", {{"user_id", "u_2"}, {"up_to_message_id", 1}}), 1);

    REQUIRE(limited.type == "ERROR");
    REQUIRE(limited.payload["code"] == "RATE_LIMITED");
}

// --- FETCH_UNREAD_COUNTS -------------------------------------------------

TEST_CASE("DMReadStateHandler FETCH_UNREAD_COUNTS requires identification") {
    Fixture f;
    const auto response = f.handler.handleFetchUnreadCounts(make_message("FETCH_UNREAD_COUNTS"), 1);
    REQUIRE(response.payload["code"] == "NOT_IDENTIFIED");
}

TEST_CASE("DMReadStateHandler FETCH_UNREAD_COUNTS returns counts keyed by peer user_id") {
    Fixture f;
    f.identify(1, "alice");
    f.api.unread_counts_to_return = {{"u_2", 3}, {"u_3", 100}};

    const auto response = f.handler.handleFetchUnreadCounts(make_message("FETCH_UNREAD_COUNTS"), 1);

    REQUIRE(response.type == "UNREAD_COUNTS");
    REQUIRE(response.payload["counts"]["u_2"] == 3);
    REQUIRE(response.payload["counts"]["u_3"] == 100);
    REQUIRE(response.payload["counts"].size() == 2);
}

TEST_CASE("DMReadStateHandler FETCH_UNREAD_COUNTS returns INTERNAL_ERROR when the internal API "
          "call fails") {
    Fixture f;
    f.identify(1, "alice");
    f.api.fail_fetch_unread_counts = true;

    const auto response = f.handler.handleFetchUnreadCounts(make_message("FETCH_UNREAD_COUNTS"), 1);

    REQUIRE(response.payload["code"] == "INTERNAL_ERROR");
}
