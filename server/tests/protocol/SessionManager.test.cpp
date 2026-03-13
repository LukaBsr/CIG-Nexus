#include <catch2/catch_test_macros.hpp>

#include "session/SessionManager.hpp"

TEST_CASE("SessionManager creates and retrieves sessions") {
    session::SessionManager manager;

    session::Session& session = manager.createSession(42);

    REQUIRE(session.session_id == "s_1");
    REQUIRE(session.user_id == "u_1");
    REQUIRE(session.username.empty());
    REQUIRE(session.socket_fd == 42);
    REQUIRE(session.connected_at > 0);
    REQUIRE(manager.hasSession(42));

    const session::Session* fetched = manager.getSession(42);
    REQUIRE(fetched != nullptr);
    REQUIRE(fetched->user_id == "u_1");
}

TEST_CASE("SessionManager updates username and supports lookup by user id") {
    session::SessionManager manager;
    session::Session& session = manager.createSession(7);

    REQUIRE(manager.updateUsername(7, "alice"));

    const session::Session* by_fd = manager.getSession(7);
    REQUIRE(by_fd != nullptr);
    REQUIRE(by_fd->username == "alice");

    const session::Session* by_user = manager.getSessionByUserId(session.user_id);
    REQUIRE(by_user != nullptr);
    REQUIRE(by_user->socket_fd == 7);
}

TEST_CASE("SessionManager removeSession clears state") {
    session::SessionManager manager;
    manager.createSession(99);

    manager.removeSession(99);

    REQUIRE_FALSE(manager.hasSession(99));
    REQUIRE(manager.getSession(99) == nullptr);
}

TEST_CASE("SessionManager updateUsername fails when session does not exist") {
    session::SessionManager manager;
    REQUIRE_FALSE(manager.updateUsername(111, "ghost"));
}
