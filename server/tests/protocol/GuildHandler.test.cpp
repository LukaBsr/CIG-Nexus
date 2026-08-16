#include <catch2/catch_test_macros.hpp>

#include "guild/GuildManager.hpp"
#include "guild/RoleRank.hpp"
#include "protocol/handlers/GuildHandler.hpp"
#include "session/SessionManager.hpp"

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
    guild::GuildManager guilds;
    test_helpers::FakeInternalApiClient api;
    protocol::GuildHandler handler;

    Fixture() {
        handler.setSessionManager(&sessions);
        handler.setGuildManager(&guilds);
        handler.setInternalApiClient(&api);
    }

    // Creates a session and completes "identification" without going
    // through IdentifyHandler, matching how ChatHandler/IdentifyHandler
    // tests set up identified sessions directly via SessionManager.
    session::Session& identify(int fd, const std::string& username) {
        session::Session& session = sessions.createSession(fd);
        session.username = username;
        session.user_id = "u_" + std::to_string(fd);
        return session;
    }
};

} // namespace

TEST_CASE("GuildHandler CREATE_GUILD creates and auto-joins the creator") {
    Fixture f;
    f.identify(1, "alice");

    const auto response =
        f.handler.handleCreateGuild(make_message("CREATE_GUILD", {{"name", "My Guild"}}), 1);

    REQUIRE(response.type == "GUILD_CREATED");
    REQUIRE(response.payload["name"] == "My Guild");
    REQUIRE(response.payload["guild_id"] ==
            "g_fake_1"); // assigned by (fake) InternalApiClient, not locally
    REQUIRE(response.payload["owner_id"] == f.sessions.getSession(1)->user_id);

    REQUIRE(f.sessions.isMemberOfGuild(1, "g_fake_1"));
    REQUIRE(f.guilds.hasGuild("g_fake_1"));
}

TEST_CASE("GuildHandler CREATE_GUILD requires identification") {
    Fixture f;

    const auto response =
        f.handler.handleCreateGuild(make_message("CREATE_GUILD", {{"name", "X"}}), 1);

    REQUIRE(response.type == "ERROR");
    REQUIRE(response.payload["code"] == "NOT_IDENTIFIED");
}

TEST_CASE("GuildHandler CREATE_GUILD rejects empty and oversized names") {
    Fixture f;
    f.identify(1, "alice");

    const auto empty = f.handler.handleCreateGuild(make_message("CREATE_GUILD", {{"name", ""}}), 1);
    REQUIRE(empty.payload["code"] == "MALFORMED_MESSAGE");

    const auto oversized = f.handler.handleCreateGuild(
        make_message("CREATE_GUILD", {{"name", std::string(65, 'x')}}), 1);
    REQUIRE(oversized.payload["code"] == "MALFORMED_MESSAGE");
}

TEST_CASE("GuildHandler CREATE_GUILD defaults visibility to open, and accepts an explicit visibility") {
    Fixture f;
    f.identify(1, "alice");

    const auto default_response =
        f.handler.handleCreateGuild(make_message("CREATE_GUILD", {{"name", "Guild A"}}), 1);
    REQUIRE(default_response.payload["visibility"] == "open");

    f.identify(2, "bob");
    const auto private_response = f.handler.handleCreateGuild(
        make_message("CREATE_GUILD", {{"name", "Guild B"}, {"visibility", "private"}}), 2);
    REQUIRE(private_response.payload["visibility"] == "private");
}

TEST_CASE("GuildHandler CREATE_GUILD rejects an invalid visibility") {
    Fixture f;
    f.identify(1, "alice");

    const auto response = f.handler.handleCreateGuild(
        make_message("CREATE_GUILD", {{"name", "My Guild"}, {"visibility", "secret"}}), 1);

    REQUIRE(response.payload["code"] == "MALFORMED_MESSAGE");
}

TEST_CASE(
    "GuildHandler CREATE_GUILD returns INTERNAL_ERROR without mutating state when the internal "
    "API call fails") {
    Fixture f;
    f.identify(1, "alice");
    f.api.fail_create_guild = true;

    const auto response =
        f.handler.handleCreateGuild(make_message("CREATE_GUILD", {{"name", "My Guild"}}), 1);

    REQUIRE(response.type == "ERROR");
    REQUIRE(response.payload["code"] == "INTERNAL_ERROR");
    REQUIRE(f.guilds.listGuilds().empty());
    REQUIRE_FALSE(f.sessions.isMemberOfGuild(1, "g_fake_1"));
}

TEST_CASE("GuildHandler LIST_GUILDS returns every guild") {
    Fixture f;
    f.identify(1, "alice");
    f.guilds.upsertGuild("g_1", "First", "u_1");
    f.guilds.upsertGuild("g_2", "Second", "u_2");

    const auto response = f.handler.handleListGuilds(make_message("LIST_GUILDS"), 1);

    REQUIRE(response.type == "GUILD_LIST");
    REQUIRE(response.payload["guilds"].size() == 2);
    REQUIRE(response.payload["guilds"][0]["visibility"] == "open");
}

TEST_CASE("GuildHandler LIST_GUILDS excludes a private guild for a non-member, includes it for a "
          "member") {
    Fixture f;
    f.identify(1, "alice");
    f.guilds.upsertGuild("g_open", "Open", "u_owner");
    f.guilds.upsertGuild("g_private", "Private", "u_owner", guild::GuildVisibility::PRIVATE);
    f.sessions.addGuildMembership(1, "g_private");

    const auto response = f.handler.handleListGuilds(make_message("LIST_GUILDS"), 1);

    REQUIRE(response.payload["guilds"].size() == 2); // alice is a member of g_private

    f.identify(2, "bob");
    const auto bobs_view = f.handler.handleListGuilds(make_message("LIST_GUILDS"), 2);
    REQUIRE(bobs_view.payload["guilds"].size() == 1);
    REQUIRE(bobs_view.payload["guilds"][0]["guild_id"] == "g_open");
}

TEST_CASE("GuildHandler LIST_GUILDS requires identification") {
    Fixture f;

    const auto response = f.handler.handleListGuilds(make_message("LIST_GUILDS"), 1);

    REQUIRE(response.payload["code"] == "NOT_IDENTIFIED");
}

TEST_CASE("GuildHandler JOIN_GUILD adds membership and returns the channel list") {
    Fixture f;
    f.identify(1, "alice");
    f.guilds.upsertGuild("g_1", "First", "u_owner");
    f.guilds.upsertChannel("c_1", "g_1", "general", guild::ChannelType::TEXT);

    const auto response =
        f.handler.handleJoinGuild(make_message("JOIN_GUILD", {{"guild_id", "g_1"}}), 1);

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

    const auto response =
        f.handler.handleJoinGuild(make_message("JOIN_GUILD", {{"guild_id", "g_404"}}), 1);

    REQUIRE(response.payload["code"] == "GUILD_NOT_FOUND");
}

TEST_CASE("GuildHandler JOIN_GUILD rejects a private guild with GUILD_NOT_FOUND (indistinguishable "
          "from nonexistent)") {
    Fixture f;
    f.identify(1, "alice");
    f.guilds.upsertGuild("g_1", "Private", "u_owner", guild::GuildVisibility::PRIVATE);

    const auto response =
        f.handler.handleJoinGuild(make_message("JOIN_GUILD", {{"guild_id", "g_1"}}), 1);

    REQUIRE(response.payload["code"] == "GUILD_NOT_FOUND");
    REQUIRE_FALSE(f.sessions.isMemberOfGuild(1, "g_1"));
}

TEST_CASE("GuildHandler JOIN_GUILD rejects an application guild with GUILD_REQUIRES_APPROVAL") {
    Fixture f;
    f.identify(1, "alice");
    f.guilds.upsertGuild("g_1", "Application", "u_owner", guild::GuildVisibility::APPLICATION);

    const auto response =
        f.handler.handleJoinGuild(make_message("JOIN_GUILD", {{"guild_id", "g_1"}}), 1);

    REQUIRE(response.payload["code"] == "GUILD_REQUIRES_APPROVAL");
    REQUIRE_FALSE(f.sessions.isMemberOfGuild(1, "g_1"));
}

TEST_CASE("GuildHandler JOIN_GUILD rejects double-join") {
    Fixture f;
    f.identify(1, "alice");
    f.guilds.upsertGuild("g_1", "First", "u_owner");
    f.sessions.addGuildMembership(1, "g_1");

    const auto response =
        f.handler.handleJoinGuild(make_message("JOIN_GUILD", {{"guild_id", "g_1"}}), 1);

    REQUIRE(response.payload["code"] == "PROTOCOL_VIOLATION");
}

TEST_CASE(
    "GuildHandler JOIN_GUILD returns INTERNAL_ERROR without mutating state when the internal API "
    "call fails") {
    Fixture f;
    f.identify(1, "alice");
    f.guilds.upsertGuild("g_1", "First", "u_owner");
    f.api.fail_create_membership = true;

    const auto response =
        f.handler.handleJoinGuild(make_message("JOIN_GUILD", {{"guild_id", "g_1"}}), 1);

    REQUIRE(response.payload["code"] == "INTERNAL_ERROR");
    REQUIRE_FALSE(f.sessions.isMemberOfGuild(1, "g_1"));
}

TEST_CASE("GuildHandler LEAVE_GUILD removes membership and notifies remaining members, including "
          "the leaver") {
    Fixture f;
    session::Session& owner = f.identify(1, "owner");
    f.identify(2, "member");

    f.guilds.upsertGuild("g_1", "First", owner.user_id);
    f.sessions.addGuildMembership(1, "g_1");
    f.sessions.addGuildMembership(2, "g_1");
    f.guilds.upsertChannel("c_1", "g_1", "general", guild::ChannelType::TEXT);
    f.sessions.setActiveChannel(2, "c_1"); // member is actively in this guild's channel

    const auto response =
        f.handler.handleLeaveGuild(make_message("LEAVE_GUILD", {{"guild_id", "g_1"}}), 2);

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
    f.guilds.upsertGuild("g_1", "First", owner.user_id);
    f.sessions.addGuildMembership(1, "g_1");

    const auto response =
        f.handler.handleLeaveGuild(make_message("LEAVE_GUILD", {{"guild_id", "g_1"}}), 1);

    REQUIRE(response.payload["code"] == "PROTOCOL_VIOLATION");
    REQUIRE(f.sessions.isMemberOfGuild(1, "g_1")); // unchanged
}

TEST_CASE("GuildHandler LEAVE_GUILD rejects a non-member") {
    Fixture f;
    f.identify(1, "alice");
    f.guilds.upsertGuild("g_1", "First", "u_owner");

    const auto response =
        f.handler.handleLeaveGuild(make_message("LEAVE_GUILD", {{"guild_id", "g_1"}}), 1);

    REQUIRE(response.payload["code"] == "NOT_GUILD_MEMBER");
}

TEST_CASE(
    "GuildHandler LEAVE_GUILD returns INTERNAL_ERROR without mutating state when the internal API "
    "call fails") {
    Fixture f;
    f.identify(1, "alice");
    f.guilds.upsertGuild("g_1", "First", "u_owner");
    f.sessions.addGuildMembership(1, "g_1");
    f.api.fail_delete_membership = true;

    const auto response =
        f.handler.handleLeaveGuild(make_message("LEAVE_GUILD", {{"guild_id", "g_1"}}), 1);

    REQUIRE(response.payload["code"] == "INTERNAL_ERROR");
    REQUIRE(f.sessions.isMemberOfGuild(1, "g_1")); // unchanged
}

TEST_CASE("GuildHandler DELETE_GUILD cascades and notifies all members") {
    Fixture f;
    session::Session& owner = f.identify(1, "owner");
    f.identify(2, "member");

    f.guilds.upsertGuild("g_1", "First", owner.user_id);
    f.sessions.addGuildMembership(1, "g_1");
    f.sessions.addGuildMembership(2, "g_1");
    f.guilds.upsertChannel("c_1", "g_1", "general", guild::ChannelType::TEXT);
    f.sessions.setActiveChannel(2, "c_1");

    const auto response =
        f.handler.handleDeleteGuild(make_message("DELETE_GUILD", {{"guild_id", "g_1"}}), 1);

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
    f.guilds.upsertGuild("g_1", "First", "u_owner");
    f.sessions.addGuildMembership(1, "g_1");

    const auto response =
        f.handler.handleDeleteGuild(make_message("DELETE_GUILD", {{"guild_id", "g_1"}}), 1);

    REQUIRE(response.payload["code"] == "NOT_GUILD_OWNER");
    REQUIRE(f.guilds.hasGuild("g_1")); // unchanged
}

TEST_CASE(
    "GuildHandler DELETE_GUILD returns INTERNAL_ERROR without mutating state when the internal "
    "API call fails") {
    Fixture f;
    session::Session& owner = f.identify(1, "owner");
    f.guilds.upsertGuild("g_1", "First", owner.user_id);
    f.sessions.addGuildMembership(1, "g_1");
    f.api.fail_delete_guild = true;

    const auto response =
        f.handler.handleDeleteGuild(make_message("DELETE_GUILD", {{"guild_id", "g_1"}}), 1);

    REQUIRE(response.payload["code"] == "INTERNAL_ERROR");
    REQUIRE(f.guilds.hasGuild("g_1"));             // unchanged
    REQUIRE(f.sessions.isMemberOfGuild(1, "g_1")); // unchanged
}

TEST_CASE("GuildHandler CREATE_GUILD seeds the owner's rank in GuildManager's cache") {
    Fixture f;
    f.identify(1, "alice");

    f.handler.handleCreateGuild(make_message("CREATE_GUILD", {{"name", "My Guild"}}), 1);

    REQUIRE(f.guilds.getMemberRank("g_fake_1", f.sessions.getSession(1)->user_id) ==
            guild::kOwnerRank);
}

TEST_CASE("GuildHandler JOIN_GUILD seeds the joiner's rank in GuildManager's cache") {
    Fixture f;
    f.identify(1, "alice");
    f.guilds.upsertGuild("g_1", "First", "u_owner");

    f.handler.handleJoinGuild(make_message("JOIN_GUILD", {{"guild_id", "g_1"}}), 1);

    REQUIRE(f.guilds.getMemberRank("g_1", f.sessions.getSession(1)->user_id) == guild::kMemberRank);
}

TEST_CASE("GuildHandler JOIN_GUILD trusts the internal API's returned rank over the requested "
          "default (idempotent rejoin of a previously-promoted member)") {
    Fixture f;
    f.identify(1, "alice");
    f.guilds.upsertGuild("g_1", "First", "u_owner");
    f.api.create_membership_returns_rank = guild::kOfficerRank;

    f.handler.handleJoinGuild(make_message("JOIN_GUILD", {{"guild_id", "g_1"}}), 1);

    REQUIRE(f.guilds.getMemberRank("g_1", f.sessions.getSession(1)->user_id) == guild::kOfficerRank);
}

TEST_CASE("GuildHandler LEAVE_GUILD clears the leaver's rank from GuildManager's cache") {
    Fixture f;
    session::Session& owner = f.identify(1, "owner");
    session::Session& member = f.identify(2, "member");
    f.guilds.upsertGuild("g_1", "First", owner.user_id);
    f.guilds.setMemberRank("g_1", owner.user_id, guild::kOwnerRank);
    f.guilds.setMemberRank("g_1", member.user_id, guild::kMemberRank);
    f.sessions.addGuildMembership(1, "g_1");
    f.sessions.addGuildMembership(2, "g_1");

    f.handler.handleLeaveGuild(make_message("LEAVE_GUILD", {{"guild_id", "g_1"}}), 2);

    REQUIRE_FALSE(f.guilds.getMemberRank("g_1", member.user_id).has_value());
}

TEST_CASE("GuildHandler LIST_MEMBERS returns the roster for a guild member") {
    Fixture f;
    session::Session& alice = f.identify(1, "alice");
    f.guilds.upsertGuild("g_1", "First", alice.user_id);
    f.sessions.addGuildMembership(1, "g_1");
    f.api.guild_members_to_return = {
        {"u_1", "alice", guild::kOwnerRank, "Captain", "2026-07-14T18:00:00Z", std::nullopt, std::nullopt}};

    const auto response =
        f.handler.handleListMembers(make_message("LIST_MEMBERS", {{"guild_id", "g_1"}}), 1);

    REQUIRE(response.type == "MEMBER_LIST");
    REQUIRE(response.payload["guild_id"] == "g_1");
    REQUIRE(response.payload["members"].size() == 1);
    REQUIRE(response.payload["members"][0]["username"] == "alice");
    REQUIRE(response.payload["members"][0]["role_rank"] == guild::kOwnerRank);
    REQUIRE(response.payload["members"][0]["role_label"] == "Captain");
}

TEST_CASE("GuildHandler LIST_MEMBERS rejects a non-member") {
    Fixture f;
    f.identify(1, "alice");
    f.guilds.upsertGuild("g_1", "First", "u_owner");

    const auto response =
        f.handler.handleListMembers(make_message("LIST_MEMBERS", {{"guild_id", "g_1"}}), 1);

    REQUIRE(response.payload["code"] == "NOT_GUILD_MEMBER");
}

TEST_CASE("GuildHandler LIST_MEMBERS rejects unknown guild") {
    Fixture f;
    f.identify(1, "alice");

    const auto response =
        f.handler.handleListMembers(make_message("LIST_MEMBERS", {{"guild_id", "g_404"}}), 1);

    REQUIRE(response.payload["code"] == "GUILD_NOT_FOUND");
}

TEST_CASE("GuildHandler LIST_MEMBERS requires identification") {
    Fixture f;

    const auto response =
        f.handler.handleListMembers(make_message("LIST_MEMBERS", {{"guild_id", "g_1"}}), 1);

    REQUIRE(response.payload["code"] == "NOT_IDENTIFIED");
}

TEST_CASE("GuildHandler LIST_MEMBERS returns INTERNAL_ERROR when the internal API call fails") {
    Fixture f;
    session::Session& alice = f.identify(1, "alice");
    f.guilds.upsertGuild("g_1", "First", alice.user_id);
    f.sessions.addGuildMembership(1, "g_1");
    f.api.fail_fetch_guild_members = true;

    const auto response =
        f.handler.handleListMembers(make_message("LIST_MEMBERS", {{"guild_id", "g_1"}}), 1);

    REQUIRE(response.payload["code"] == "INTERNAL_ERROR");
}

TEST_CASE("GuildHandler SET_MEMBER_ROLE promotes a member and notifies all guild members") {
    Fixture f;
    session::Session& owner = f.identify(1, "owner");
    session::Session& member = f.identify(2, "member");
    f.guilds.upsertGuild("g_1", "First", owner.user_id);
    f.guilds.setMemberRank("g_1", owner.user_id, guild::kOwnerRank);
    f.guilds.setMemberRank("g_1", member.user_id, guild::kMemberRank);
    f.sessions.addGuildMembership(1, "g_1");
    f.sessions.addGuildMembership(2, "g_1");
    f.api.set_member_role_label_to_return = "Officer";

    const auto response = f.handler.handleSetMemberRole(
        make_message("SET_MEMBER_ROLE",
                     {{"guild_id", "g_1"}, {"user_id", member.user_id}, {"role_rank", guild::kOfficerRank}}),
        1);

    REQUIRE(response.type == "MEMBER_ROLE_UPDATED");
    REQUIRE(response.scope == protocol::Scope::TARGETED);
    REQUIRE(response.target_fds.size() == 2);
    REQUIRE(response.payload["guild_id"] == "g_1");
    REQUIRE(response.payload["user_id"] == member.user_id);
    REQUIRE(response.payload["role_rank"] == guild::kOfficerRank);
    REQUIRE(response.payload["role_label"] == "Officer");
    REQUIRE(f.guilds.getMemberRank("g_1", member.user_id) == guild::kOfficerRank);
}

TEST_CASE("GuildHandler SET_MEMBER_ROLE rejects a caller below owner rank") {
    Fixture f;
    session::Session& officer = f.identify(1, "officer");
    session::Session& member = f.identify(2, "member");
    f.guilds.upsertGuild("g_1", "First", "u_owner");
    f.guilds.setMemberRank("g_1", officer.user_id, guild::kOfficerRank);
    f.guilds.setMemberRank("g_1", member.user_id, guild::kMemberRank);

    const auto response = f.handler.handleSetMemberRole(
        make_message("SET_MEMBER_ROLE",
                     {{"guild_id", "g_1"}, {"user_id", member.user_id}, {"role_rank", guild::kOfficerRank}}),
        1);

    REQUIRE(response.payload["code"] == "NOT_GUILD_OWNER");
}

TEST_CASE("GuildHandler SET_MEMBER_ROLE rejects a role_rank at or above the owner rank") {
    Fixture f;
    session::Session& owner = f.identify(1, "owner");
    session::Session& member = f.identify(2, "member");
    f.guilds.upsertGuild("g_1", "First", owner.user_id);
    f.guilds.setMemberRank("g_1", owner.user_id, guild::kOwnerRank);
    f.guilds.setMemberRank("g_1", member.user_id, guild::kMemberRank);

    const auto response = f.handler.handleSetMemberRole(
        make_message("SET_MEMBER_ROLE",
                     {{"guild_id", "g_1"}, {"user_id", member.user_id}, {"role_rank", guild::kOwnerRank}}),
        1);

    REQUIRE(response.payload["code"] == "MALFORMED_MESSAGE");
}

TEST_CASE("GuildHandler SET_MEMBER_ROLE rejects targeting the guild owner") {
    Fixture f;
    session::Session& owner = f.identify(1, "owner");
    f.guilds.upsertGuild("g_1", "First", owner.user_id);
    f.guilds.setMemberRank("g_1", owner.user_id, guild::kOwnerRank);

    const auto response = f.handler.handleSetMemberRole(
        make_message("SET_MEMBER_ROLE",
                     {{"guild_id", "g_1"}, {"user_id", owner.user_id}, {"role_rank", guild::kOfficerRank}}),
        1);

    REQUIRE(response.payload["code"] == "PROTOCOL_VIOLATION");
}

TEST_CASE("GuildHandler SET_MEMBER_ROLE rejects a target who isn't a guild member") {
    Fixture f;
    session::Session& owner = f.identify(1, "owner");
    f.guilds.upsertGuild("g_1", "First", owner.user_id);
    f.guilds.setMemberRank("g_1", owner.user_id, guild::kOwnerRank);

    const auto response = f.handler.handleSetMemberRole(
        make_message("SET_MEMBER_ROLE",
                     {{"guild_id", "g_1"}, {"user_id", "u_stranger"}, {"role_rank", guild::kOfficerRank}}),
        1);

    REQUIRE(response.payload["code"] == "NOT_GUILD_MEMBER");
}

TEST_CASE("GuildHandler SET_MEMBER_ROLE rejects unknown guild") {
    Fixture f;
    f.identify(1, "owner");

    const auto response = f.handler.handleSetMemberRole(
        make_message("SET_MEMBER_ROLE",
                     {{"guild_id", "g_404"}, {"user_id", "u_2"}, {"role_rank", guild::kOfficerRank}}),
        1);

    REQUIRE(response.payload["code"] == "GUILD_NOT_FOUND");
}

TEST_CASE("GuildHandler SET_MEMBER_ROLE requires identification") {
    Fixture f;

    const auto response = f.handler.handleSetMemberRole(
        make_message("SET_MEMBER_ROLE",
                     {{"guild_id", "g_1"}, {"user_id", "u_2"}, {"role_rank", guild::kOfficerRank}}),
        1);

    REQUIRE(response.payload["code"] == "NOT_IDENTIFIED");
}

TEST_CASE("GuildHandler SET_MEMBER_ROLE returns INTERNAL_ERROR when the internal API call fails") {
    Fixture f;
    session::Session& owner = f.identify(1, "owner");
    session::Session& member = f.identify(2, "member");
    f.guilds.upsertGuild("g_1", "First", owner.user_id);
    f.guilds.setMemberRank("g_1", owner.user_id, guild::kOwnerRank);
    f.guilds.setMemberRank("g_1", member.user_id, guild::kMemberRank);
    f.api.fail_set_member_role = true;

    const auto response = f.handler.handleSetMemberRole(
        make_message("SET_MEMBER_ROLE",
                     {{"guild_id", "g_1"}, {"user_id", member.user_id}, {"role_rank", guild::kOfficerRank}}),
        1);

    REQUIRE(response.payload["code"] == "INTERNAL_ERROR");
    REQUIRE(f.guilds.getMemberRank("g_1", member.user_id) == guild::kMemberRank); // unchanged
}

TEST_CASE("GuildHandler SET_GUILD_VISIBILITY updates visibility and notifies all guild members") {
    Fixture f;
    session::Session& owner = f.identify(1, "owner");
    f.identify(2, "member");
    f.guilds.upsertGuild("g_1", "First", owner.user_id);
    f.guilds.setMemberRank("g_1", owner.user_id, guild::kOwnerRank);
    f.sessions.addGuildMembership(1, "g_1");
    f.sessions.addGuildMembership(2, "g_1");

    const auto response = f.handler.handleSetGuildVisibility(
        make_message("SET_GUILD_VISIBILITY", {{"guild_id", "g_1"}, {"visibility", "private"}}), 1);

    REQUIRE(response.type == "GUILD_VISIBILITY_CHANGED");
    REQUIRE(response.scope == protocol::Scope::TARGETED);
    REQUIRE(response.target_fds.size() == 2);
    REQUIRE(response.payload["guild_id"] == "g_1");
    REQUIRE(response.payload["visibility"] == "private");
    REQUIRE(f.guilds.getGuild("g_1")->visibility == guild::GuildVisibility::PRIVATE);
}

TEST_CASE("GuildHandler SET_GUILD_VISIBILITY rejects a caller below owner rank") {
    Fixture f;
    session::Session& officer = f.identify(1, "officer");
    f.guilds.upsertGuild("g_1", "First", "u_owner");
    f.guilds.setMemberRank("g_1", officer.user_id, guild::kOfficerRank);

    const auto response = f.handler.handleSetGuildVisibility(
        make_message("SET_GUILD_VISIBILITY", {{"guild_id", "g_1"}, {"visibility", "private"}}), 1);

    REQUIRE(response.payload["code"] == "NOT_GUILD_OWNER");
}

TEST_CASE("GuildHandler SET_GUILD_VISIBILITY rejects an invalid visibility") {
    Fixture f;
    session::Session& owner = f.identify(1, "owner");
    f.guilds.upsertGuild("g_1", "First", owner.user_id);
    f.guilds.setMemberRank("g_1", owner.user_id, guild::kOwnerRank);

    const auto response = f.handler.handleSetGuildVisibility(
        make_message("SET_GUILD_VISIBILITY", {{"guild_id", "g_1"}, {"visibility", "secret"}}), 1);

    REQUIRE(response.payload["code"] == "MALFORMED_MESSAGE");
}

TEST_CASE("GuildHandler SET_GUILD_VISIBILITY rejects unknown guild") {
    Fixture f;
    f.identify(1, "owner");

    const auto response = f.handler.handleSetGuildVisibility(
        make_message("SET_GUILD_VISIBILITY", {{"guild_id", "g_404"}, {"visibility", "private"}}), 1);

    REQUIRE(response.payload["code"] == "GUILD_NOT_FOUND");
}

TEST_CASE("GuildHandler SET_GUILD_VISIBILITY requires identification") {
    Fixture f;

    const auto response = f.handler.handleSetGuildVisibility(
        make_message("SET_GUILD_VISIBILITY", {{"guild_id", "g_1"}, {"visibility", "private"}}), 1);

    REQUIRE(response.payload["code"] == "NOT_IDENTIFIED");
}

TEST_CASE("GuildHandler SET_GUILD_VISIBILITY returns INTERNAL_ERROR when the internal API call fails") {
    Fixture f;
    session::Session& owner = f.identify(1, "owner");
    f.guilds.upsertGuild("g_1", "First", owner.user_id);
    f.guilds.setMemberRank("g_1", owner.user_id, guild::kOwnerRank);
    f.api.fail_set_guild_visibility = true;

    const auto response = f.handler.handleSetGuildVisibility(
        make_message("SET_GUILD_VISIBILITY", {{"guild_id", "g_1"}, {"visibility", "private"}}), 1);

    REQUIRE(response.payload["code"] == "INTERNAL_ERROR");
    REQUIRE(f.guilds.getGuild("g_1")->visibility == guild::GuildVisibility::OPEN); // unchanged
}
