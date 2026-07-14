#include <catch2/catch_test_macros.hpp>

#include "session/SessionManager.hpp"

#include <algorithm>
#include <vector>

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

TEST_CASE("SessionManager tracks guild membership") {
    session::SessionManager manager;
    manager.createSession(1);

    REQUIRE_FALSE(manager.isMemberOfGuild(1, "g_1"));

    manager.addGuildMembership(1, "g_1");
    REQUIRE(manager.isMemberOfGuild(1, "g_1"));

    // Idempotent: adding the same guild twice doesn't duplicate membership.
    manager.addGuildMembership(1, "g_1");
    REQUIRE(manager.getFdsInGuild("g_1") == std::vector<int>{1});

    manager.addGuildMembership(1, "g_2");
    REQUIRE(manager.isMemberOfGuild(1, "g_2"));

    manager.removeGuildMembership(1, "g_1");
    REQUIRE_FALSE(manager.isMemberOfGuild(1, "g_1"));
    REQUIRE(manager.isMemberOfGuild(1, "g_2"));
}

TEST_CASE("SessionManager guild/channel helpers no-op on unknown fd") {
    session::SessionManager manager;

    manager.addGuildMembership(404, "g_1");
    manager.removeGuildMembership(404, "g_1");
    manager.setActiveChannel(404, "c_1");
    manager.clearActiveChannel(404);

    REQUIRE_FALSE(manager.isMemberOfGuild(404, "g_1"));
}

TEST_CASE("SessionManager getFdsInGuild returns every member fd") {
    session::SessionManager manager;
    manager.createSession(1);
    manager.createSession(2);
    manager.createSession(3);

    manager.addGuildMembership(1, "g_1");
    manager.addGuildMembership(2, "g_1");
    manager.addGuildMembership(3, "g_2");

    const auto members = manager.getFdsInGuild("g_1");
    REQUIRE(members.size() == 2);
    REQUIRE(std::find(members.begin(), members.end(), 1) != members.end());
    REQUIRE(std::find(members.begin(), members.end(), 2) != members.end());
}

TEST_CASE("SessionManager tracks a single active channel per connection") {
    session::SessionManager manager;
    manager.createSession(1);

    manager.setActiveChannel(1, "c_1");
    REQUIRE(manager.getSession(1)->active_channel_id == "c_1");

    // Joining a new channel implicitly replaces the previous one.
    manager.setActiveChannel(1, "c_2");
    REQUIRE(manager.getSession(1)->active_channel_id == "c_2");

    manager.clearActiveChannel(1);
    REQUIRE(manager.getSession(1)->active_channel_id.empty());
}

TEST_CASE("SessionManager getFdsWithActiveChannel returns only matching connections") {
    session::SessionManager manager;
    manager.createSession(1);
    manager.createSession(2);
    manager.createSession(3);

    manager.setActiveChannel(1, "c_1");
    manager.setActiveChannel(2, "c_1");
    manager.setActiveChannel(3, "c_2");

    const auto listeners = manager.getFdsWithActiveChannel("c_1");
    REQUIRE(listeners.size() == 2);
    REQUIRE(std::find(listeners.begin(), listeners.end(), 1) != listeners.end());
    REQUIRE(std::find(listeners.begin(), listeners.end(), 2) != listeners.end());
}

TEST_CASE("SessionManager purgeGuildMembership cascades to membership and active channel") {
    session::SessionManager manager;
    manager.createSession(1);
    manager.createSession(2);

    manager.addGuildMembership(1, "g_1");
    manager.addGuildMembership(1, "g_2");
    manager.setActiveChannel(1, "c_1"); // channel belongs to g_1

    manager.addGuildMembership(2, "g_1");
    manager.setActiveChannel(2, "c_9"); // unrelated channel, should survive

    manager.purgeGuildMembership("g_1", {"c_1"});

    REQUIRE_FALSE(manager.isMemberOfGuild(1, "g_1"));
    REQUIRE(manager.isMemberOfGuild(1, "g_2")); // untouched
    REQUIRE(manager.getSession(1)->active_channel_id.empty());

    REQUIRE_FALSE(manager.isMemberOfGuild(2, "g_1"));
    REQUIRE(manager.getSession(2)->active_channel_id == "c_9"); // untouched
}

TEST_CASE("SessionManager clearActiveChannelEverywhere only clears the matching channel") {
    session::SessionManager manager;
    manager.createSession(1);
    manager.createSession(2);

    manager.setActiveChannel(1, "c_1");
    manager.setActiveChannel(2, "c_2");

    manager.clearActiveChannelEverywhere("c_1");

    REQUIRE(manager.getSession(1)->active_channel_id.empty());
    REQUIRE(manager.getSession(2)->active_channel_id == "c_2");
}
