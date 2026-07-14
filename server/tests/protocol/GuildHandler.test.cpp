#include <catch2/catch_test_macros.hpp>

#include "guild/GuildManager.hpp"
#include "protocol/handlers/GuildHandler.hpp"
#include "session/SessionManager.hpp"

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
    guild::GuildManager guilds;
    protocol::GuildHandler handler;

    Fixture() {
        handler.setSessionManager(&sessions);
        handler.setGuildManager(&guilds);
    }

    // Creates a session and completes "identification" without going
    // through IdentifyHandler, matching how ChatHandler/IdentifyHandler
    // tests set up identified sessions directly via SessionManager.
    session::Session& identify(int fd, const std::string& username) {
        session::Session& session = sessions.createSession(fd);
        session.username = username;
        return session;
    }
};

} // namespace

TEST_CASE("GuildHandler CREATE_GUILD creates and auto-joins the creator") {
    Fixture f;
    f.identify(1, "alice");

    const auto response = f.handler.handleCreateGuild(make_message("CREATE_GUILD", {{"name", "My Guild"}}), 1);

    REQUIRE(response.type == "GUILD_CREATED");
    REQUIRE(response.payload["name"] == "My Guild");
    REQUIRE(response.payload["guild_id"] == "g_1");
    REQUIRE(response.payload["owner_id"] == f.sessions.getSession(1)->user_id);

    REQUIRE(f.sessions.isMemberOfGuild(1, "g_1"));
    REQUIRE(f.guilds.hasGuild("g_1"));
}

TEST_CASE("GuildHandler CREATE_GUILD requires identification") {
    Fixture f;

    const auto response = f.handler.handleCreateGuild(make_message("CREATE_GUILD", {{"name", "X"}}), 1);

    REQUIRE(response.type == "ERROR");
    REQUIRE(response.payload["code"] == "NOT_IDENTIFIED");
}

TEST_CASE("GuildHandler CREATE_GUILD rejects empty and oversized names") {
    Fixture f;
    f.identify(1, "alice");

    const auto empty = f.handler.handleCreateGuild(make_message("CREATE_GUILD", {{"name", ""}}), 1);
    REQUIRE(empty.payload["code"] == "MALFORMED_MESSAGE");

    const auto oversized =
        f.handler.handleCreateGuild(make_message("CREATE_GUILD", {{"name", std::string(65, 'x')}}), 1);
    REQUIRE(oversized.payload["code"] == "MALFORMED_MESSAGE");
}

TEST_CASE("GuildHandler LIST_GUILDS returns every guild") {
    Fixture f;
    f.identify(1, "alice");
    f.guilds.createGuild("First", "u_1");
    f.guilds.createGuild("Second", "u_2");

    const auto response = f.handler.handleListGuilds(make_message("LIST_GUILDS"), 1);

    REQUIRE(response.type == "GUILD_LIST");
    REQUIRE(response.payload["guilds"].size() == 2);
}

TEST_CASE("GuildHandler LIST_GUILDS requires identification") {
    Fixture f;

    const auto response = f.handler.handleListGuilds(make_message("LIST_GUILDS"), 1);

    REQUIRE(response.payload["code"] == "NOT_IDENTIFIED");
}

TEST_CASE("GuildHandler JOIN_GUILD adds membership and returns the channel list") {
    Fixture f;
    f.identify(1, "alice");
    f.guilds.createGuild("First", "u_owner");
    f.guilds.createChannel("g_1", "general", guild::ChannelType::TEXT);

    const auto response = f.handler.handleJoinGuild(make_message("JOIN_GUILD", {{"guild_id", "g_1"}}), 1);

    REQUIRE(response.type == "GUILD_JOINED");
    REQUIRE(response.payload["guild_id"] == "g_1");
    REQUIRE(response.payload["channels"].size() == 1);
    REQUIRE(response.payload["channels"][0]["channel_id"] == "c_1");
    REQUIRE(response.payload["channels"][0]["channel_type"] == "TEXT");
    REQUIRE(f.sessions.isMemberOfGuild(1, "g_1"));
}

TEST_CASE("GuildHandler JOIN_GUILD rejects unknown guild") {
    Fixture f;
    f.identify(1, "alice");

    const auto response = f.handler.handleJoinGuild(make_message("JOIN_GUILD", {{"guild_id", "g_404"}}), 1);

    REQUIRE(response.payload["code"] == "GUILD_NOT_FOUND");
}

TEST_CASE("GuildHandler JOIN_GUILD rejects double-join") {
    Fixture f;
    f.identify(1, "alice");
    f.guilds.createGuild("First", "u_owner");
    f.sessions.addGuildMembership(1, "g_1");

    const auto response = f.handler.handleJoinGuild(make_message("JOIN_GUILD", {{"guild_id", "g_1"}}), 1);

    REQUIRE(response.payload["code"] == "PROTOCOL_VIOLATION");
}

TEST_CASE("GuildHandler LEAVE_GUILD removes membership and notifies remaining members, including the leaver") {
    Fixture f;
    session::Session& owner = f.identify(1, "owner");
    f.identify(2, "member");

    f.guilds.createGuild("First", owner.user_id);
    f.sessions.addGuildMembership(1, "g_1");
    f.sessions.addGuildMembership(2, "g_1");
    f.guilds.createChannel("g_1", "general", guild::ChannelType::TEXT);
    f.sessions.setActiveChannel(2, "c_1"); // member is actively in this guild's channel

    const auto response = f.handler.handleLeaveGuild(make_message("LEAVE_GUILD", {{"guild_id", "g_1"}}), 2);

    REQUIRE(response.type == "MEMBER_LEFT");
    REQUIRE(response.scope == protocol::Scope::TARGETED);
    // Snapshot taken before membership was mutated: both the owner and the
    // leaver themselves should be in the notification list.
    REQUIRE(response.target_fds.size() == 2);
    REQUIRE(std::find(response.target_fds.begin(), response.target_fds.end(), 1) !=
            response.target_fds.end());
    REQUIRE(std::find(response.target_fds.begin(), response.target_fds.end(), 2) !=
            response.target_fds.end());

    REQUIRE_FALSE(f.sessions.isMemberOfGuild(2, "g_1"));
    REQUIRE(f.sessions.getSession(2)->active_channel_id.empty());
}

TEST_CASE("GuildHandler LEAVE_GUILD rejects the owner leaving their own guild") {
    Fixture f;
    session::Session& owner = f.identify(1, "owner");
    f.guilds.createGuild("First", owner.user_id);
    f.sessions.addGuildMembership(1, "g_1");

    const auto response = f.handler.handleLeaveGuild(make_message("LEAVE_GUILD", {{"guild_id", "g_1"}}), 1);

    REQUIRE(response.payload["code"] == "PROTOCOL_VIOLATION");
    REQUIRE(f.sessions.isMemberOfGuild(1, "g_1")); // unchanged
}

TEST_CASE("GuildHandler LEAVE_GUILD rejects a non-member") {
    Fixture f;
    f.identify(1, "alice");
    f.guilds.createGuild("First", "u_owner");

    const auto response = f.handler.handleLeaveGuild(make_message("LEAVE_GUILD", {{"guild_id", "g_1"}}), 1);

    REQUIRE(response.payload["code"] == "NOT_GUILD_MEMBER");
}

TEST_CASE("GuildHandler DELETE_GUILD cascades and notifies all members") {
    Fixture f;
    session::Session& owner = f.identify(1, "owner");
    f.identify(2, "member");

    f.guilds.createGuild("First", owner.user_id);
    f.sessions.addGuildMembership(1, "g_1");
    f.sessions.addGuildMembership(2, "g_1");
    f.guilds.createChannel("g_1", "general", guild::ChannelType::TEXT);
    f.sessions.setActiveChannel(2, "c_1");

    const auto response = f.handler.handleDeleteGuild(make_message("DELETE_GUILD", {{"guild_id", "g_1"}}), 1);

    REQUIRE(response.type == "GUILD_DELETED");
    REQUIRE(response.scope == protocol::Scope::TARGETED);
    REQUIRE(response.target_fds.size() == 2);

    REQUIRE_FALSE(f.guilds.hasGuild("g_1"));
    REQUIRE_FALSE(f.guilds.hasChannel("c_1"));
    REQUIRE_FALSE(f.sessions.isMemberOfGuild(1, "g_1"));
    REQUIRE_FALSE(f.sessions.isMemberOfGuild(2, "g_1"));
    REQUIRE(f.sessions.getSession(2)->active_channel_id.empty());
}

TEST_CASE("GuildHandler DELETE_GUILD rejects a non-owner") {
    Fixture f;
    f.identify(1, "not-owner");
    f.guilds.createGuild("First", "u_owner");
    f.sessions.addGuildMembership(1, "g_1");

    const auto response = f.handler.handleDeleteGuild(make_message("DELETE_GUILD", {{"guild_id", "g_1"}}), 1);

    REQUIRE(response.payload["code"] == "NOT_GUILD_OWNER");
    REQUIRE(f.guilds.hasGuild("g_1")); // unchanged
}
