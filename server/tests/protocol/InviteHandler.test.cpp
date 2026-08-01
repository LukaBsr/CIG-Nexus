#include <catch2/catch_test_macros.hpp>

#include "guild/GuildManager.hpp"
#include "guild/RoleRank.hpp"
#include "protocol/handlers/InviteHandler.hpp"
#include "session/SessionManager.hpp"

#include "../http/FakeInternalApiClient.hpp"

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
    protocol::InviteHandler handler;

    Fixture() {
        handler.setSessionManager(&sessions);
        handler.setGuildManager(&guilds);
        handler.setInternalApiClient(&api);
    }

    session::Session& identify(int fd, const std::string& username) {
        session::Session& session = sessions.createSession(fd);
        session.username = username;
        session.user_id = "u_" + std::to_string(fd);
        return session;
    }
};

} // namespace

TEST_CASE("InviteHandler CREATE_INVITE creates an invite for an officer") {
    Fixture f;
    session::Session& owner = f.identify(1, "owner");
    f.guilds.upsertGuild("g_1", "First", owner.user_id);
    f.guilds.setMemberRank("g_1", owner.user_id, guild::kOwnerRank);
    f.api.create_invite_returns_code = "abc123";

    const auto response = f.handler.handleCreateInvite(
        make_message("CREATE_INVITE", {{"guild_id", "g_1"}, {"max_uses", nullptr}, {"expires_in_seconds", nullptr}}),
        1);

    REQUIRE(response.type == "INVITE_CREATED");
    REQUIRE(response.payload["guild_id"] == "g_1");
    REQUIRE(response.payload["code"] == "abc123");
    REQUIRE(response.payload.contains("max_uses"));
    REQUIRE(response.payload.contains("use_count"));
    REQUIRE_FALSE(response.payload.contains("revoked_at"));
}

TEST_CASE("InviteHandler CREATE_INVITE rejects a crew-rank caller") {
    Fixture f;
    session::Session& crew = f.identify(1, "crew");
    f.guilds.upsertGuild("g_1", "First", "u_owner");
    f.guilds.setMemberRank("g_1", crew.user_id, guild::kMemberRank);

    const auto response =
        f.handler.handleCreateInvite(make_message("CREATE_INVITE", {{"guild_id", "g_1"}}), 1);

    REQUIRE(response.payload["code"] == "NOT_GUILD_OFFICER");
}

TEST_CASE("InviteHandler CREATE_INVITE rejects a non-positive max_uses") {
    Fixture f;
    session::Session& owner = f.identify(1, "owner");
    f.guilds.upsertGuild("g_1", "First", owner.user_id);
    f.guilds.setMemberRank("g_1", owner.user_id, guild::kOwnerRank);

    const auto response = f.handler.handleCreateInvite(
        make_message("CREATE_INVITE", {{"guild_id", "g_1"}, {"max_uses", 0}}), 1);

    REQUIRE(response.payload["code"] == "MALFORMED_MESSAGE");
}

TEST_CASE("InviteHandler CREATE_INVITE rejects unknown guild") {
    Fixture f;
    f.identify(1, "owner");

    const auto response =
        f.handler.handleCreateInvite(make_message("CREATE_INVITE", {{"guild_id", "g_404"}}), 1);

    REQUIRE(response.payload["code"] == "GUILD_NOT_FOUND");
}

TEST_CASE("InviteHandler CREATE_INVITE requires identification") {
    Fixture f;

    const auto response =
        f.handler.handleCreateInvite(make_message("CREATE_INVITE", {{"guild_id", "g_1"}}), 1);

    REQUIRE(response.payload["code"] == "NOT_IDENTIFIED");
}

TEST_CASE("InviteHandler LIST_INVITES returns the invites for an officer") {
    Fixture f;
    session::Session& owner = f.identify(1, "owner");
    f.guilds.upsertGuild("g_1", "First", owner.user_id);
    f.guilds.setMemberRank("g_1", owner.user_id, guild::kOwnerRank);
    f.api.invites_to_return = {{"abc", std::nullopt, 2, std::nullopt, std::nullopt, "2026-01-01T00:00:00Z"}};

    const auto response =
        f.handler.handleListInvites(make_message("LIST_INVITES", {{"guild_id", "g_1"}}), 1);

    REQUIRE(response.type == "INVITE_LIST");
    REQUIRE(response.payload["invites"].size() == 1);
    REQUIRE(response.payload["invites"][0]["code"] == "abc");
    REQUIRE(response.payload["invites"][0]["use_count"] == 2);
}

TEST_CASE("InviteHandler LIST_INVITES rejects a crew-rank caller") {
    Fixture f;
    session::Session& crew = f.identify(1, "crew");
    f.guilds.upsertGuild("g_1", "First", "u_owner");
    f.guilds.setMemberRank("g_1", crew.user_id, guild::kMemberRank);

    const auto response =
        f.handler.handleListInvites(make_message("LIST_INVITES", {{"guild_id", "g_1"}}), 1);

    REQUIRE(response.payload["code"] == "NOT_GUILD_OFFICER");
}

TEST_CASE("InviteHandler REVOKE_INVITE revokes an existing invite for an officer") {
    Fixture f;
    session::Session& owner = f.identify(1, "owner");
    f.guilds.upsertGuild("g_1", "First", owner.user_id);
    f.guilds.setMemberRank("g_1", owner.user_id, guild::kOwnerRank);

    const auto response = f.handler.handleRevokeInvite(
        make_message("REVOKE_INVITE", {{"guild_id", "g_1"}, {"code", "abc123"}}), 1);

    REQUIRE(response.type == "INVITE_REVOKED");
    REQUIRE(response.payload["code"] == "abc123");
}

TEST_CASE("InviteHandler REVOKE_INVITE returns INVITE_NOT_FOUND when the internal API call fails") {
    Fixture f;
    session::Session& owner = f.identify(1, "owner");
    f.guilds.upsertGuild("g_1", "First", owner.user_id);
    f.guilds.setMemberRank("g_1", owner.user_id, guild::kOwnerRank);
    f.api.fail_revoke_invite = true;

    const auto response = f.handler.handleRevokeInvite(
        make_message("REVOKE_INVITE", {{"guild_id", "g_1"}, {"code", "nonexistent"}}), 1);

    REQUIRE(response.payload["code"] == "INVITE_NOT_FOUND");
}

TEST_CASE("InviteHandler REVOKE_INVITE rejects a crew-rank caller") {
    Fixture f;
    session::Session& crew = f.identify(1, "crew");
    f.guilds.upsertGuild("g_1", "First", "u_owner");
    f.guilds.setMemberRank("g_1", crew.user_id, guild::kMemberRank);

    const auto response = f.handler.handleRevokeInvite(
        make_message("REVOKE_INVITE", {{"guild_id", "g_1"}, {"code", "abc123"}}), 1);

    REQUIRE(response.payload["code"] == "NOT_GUILD_OFFICER");
}

TEST_CASE("InviteHandler JOIN_VIA_INVITE grants direct membership for an open guild") {
    Fixture f;
    session::Session& redeemer = f.identify(1, "bob");
    f.guilds.upsertGuild("g_1", "First", "u_owner");
    f.guilds.upsertChannel("c_1", "g_1", "general", guild::ChannelType::TEXT);
    f.api.redeem_invite_returns = {true, false, "g_1", 0, http::RedeemInviteError::NOT_FOUND};

    const auto responses =
        f.handler.handleJoinViaInvite(make_message("JOIN_VIA_INVITE", {{"code", "abc123"}}), 1);

    REQUIRE(responses.size() == 1);
    REQUIRE(responses[0].type == "GUILD_JOINED");
    REQUIRE(responses[0].payload["guild_id"] == "g_1");
    REQUIRE(responses[0].payload["channels"].size() == 1);
    REQUIRE(f.sessions.isMemberOfGuild(1, "g_1"));
    REQUIRE(f.guilds.getMemberRank("g_1", redeemer.user_id) == 0);
}

TEST_CASE("InviteHandler JOIN_VIA_INVITE creates a join request for an application guild, and "
          "notifies connected officers") {
    Fixture f;
    f.identify(1, "bob");
    session::Session& officer = f.identify(2, "officer");
    f.guilds.upsertGuild("g_1", "First", "u_owner", guild::GuildVisibility::APPLICATION);
    f.guilds.setMemberRank("g_1", officer.user_id, guild::kOfficerRank);
    f.sessions.addGuildMembership(2, "g_1");
    f.api.redeem_invite_returns = {true, true, "g_1", std::nullopt, http::RedeemInviteError::NOT_FOUND};

    const auto responses =
        f.handler.handleJoinViaInvite(make_message("JOIN_VIA_INVITE", {{"code", "abc123"}}), 1);

    REQUIRE(responses.size() == 2);
    REQUIRE(responses[0].type == "JOIN_REQUESTED");
    REQUIRE(responses[0].payload["guild_id"] == "g_1");
    REQUIRE(responses[1].type == "JOIN_REQUEST_RECEIVED");
    REQUIRE(responses[1].scope == protocol::Scope::TARGETED);
    REQUIRE(responses[1].target_fds == std::vector<int>{2});
    REQUIRE(responses[1].payload["user_id"] == "u_1");
    REQUIRE_FALSE(f.sessions.isMemberOfGuild(1, "g_1")); // not a member — only a request
}

TEST_CASE("InviteHandler JOIN_VIA_INVITE maps each redeem error to the matching error code") {
    Fixture f;
    f.identify(1, "bob");

    auto check = [&](http::RedeemInviteError error, const std::string& expected_code) {
        f.api.redeem_invite_returns = {false, false, "", std::nullopt, error};
        const auto responses =
            f.handler.handleJoinViaInvite(make_message("JOIN_VIA_INVITE", {{"code", "abc123"}}), 1);
        REQUIRE(responses.size() == 1);
        REQUIRE(responses[0].payload["code"] == expected_code);
    };

    check(http::RedeemInviteError::NOT_FOUND, "INVITE_NOT_FOUND");
    check(http::RedeemInviteError::REVOKED, "INVITE_REVOKED");
    check(http::RedeemInviteError::EXPIRED, "INVITE_EXPIRED");
    check(http::RedeemInviteError::MAX_USES_REACHED, "INVITE_MAX_USES_REACHED");
    check(http::RedeemInviteError::ALREADY_MEMBER, "PROTOCOL_VIOLATION");
    check(http::RedeemInviteError::FAILED, "INTERNAL_ERROR");
}

TEST_CASE("InviteHandler JOIN_VIA_INVITE requires identification") {
    Fixture f;

    const auto responses =
        f.handler.handleJoinViaInvite(make_message("JOIN_VIA_INVITE", {{"code", "abc123"}}), 1);

    REQUIRE(responses.size() == 1);
    REQUIRE(responses[0].payload["code"] == "NOT_IDENTIFIED");
}
