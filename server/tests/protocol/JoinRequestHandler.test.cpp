#include <catch2/catch_test_macros.hpp>

#include "guild/GuildManager.hpp"
#include "guild/RoleRank.hpp"
#include "protocol/handlers/JoinRequestHandler.hpp"
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
    protocol::JoinRequestHandler handler;

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

TEST_CASE("JoinRequestHandler REQUEST_JOIN creates a request against an application guild, and "
          "notifies connected officers") {
    Fixture f;
    f.identify(1, "bob");
    session::Session& officer = f.identify(2, "officer");
    f.guilds.upsertGuild("g_1", "First", "u_owner", guild::GuildVisibility::APPLICATION);
    f.guilds.setMemberRank("g_1", officer.user_id, guild::kOfficerRank);
    f.sessions.addGuildMembership(2, "g_1");
    f.api.create_join_request_returns = http::CreateJoinRequestResult::CREATED;

    const auto responses =
        f.handler.handleRequestJoin(make_message("REQUEST_JOIN", {{"guild_id", "g_1"}}), 1);

    REQUIRE(responses.size() == 2);
    REQUIRE(responses[0].type == "JOIN_REQUESTED");
    REQUIRE(responses[0].payload["guild_id"] == "g_1");
    REQUIRE(responses[1].type == "JOIN_REQUEST_RECEIVED");
    REQUIRE(responses[1].scope == protocol::Scope::TARGETED);
    REQUIRE(responses[1].target_fds == std::vector<int>{2});
    REQUIRE(responses[1].payload["user_id"] == "u_1");
    REQUIRE(responses[1].payload["username"] == "bob");
}

TEST_CASE("JoinRequestHandler REQUEST_JOIN omits JOIN_REQUEST_RECEIVED when no officer is connected") {
    Fixture f;
    f.identify(1, "bob");
    f.guilds.upsertGuild("g_1", "First", "u_owner", guild::GuildVisibility::APPLICATION);
    f.api.create_join_request_returns = http::CreateJoinRequestResult::CREATED;

    const auto responses =
        f.handler.handleRequestJoin(make_message("REQUEST_JOIN", {{"guild_id", "g_1"}}), 1);

    REQUIRE(responses.size() == 1);
    REQUIRE(responses[0].type == "JOIN_REQUESTED");
}

TEST_CASE("JoinRequestHandler REQUEST_JOIN rejects an already-pending request") {
    Fixture f;
    f.identify(1, "bob");
    f.guilds.upsertGuild("g_1", "First", "u_owner", guild::GuildVisibility::APPLICATION);
    f.api.create_join_request_returns = http::CreateJoinRequestResult::ALREADY_PENDING;

    const auto responses =
        f.handler.handleRequestJoin(make_message("REQUEST_JOIN", {{"guild_id", "g_1"}}), 1);

    REQUIRE(responses.size() == 1);
    REQUIRE(responses[0].payload["code"] == "JOIN_REQUEST_ALREADY_PENDING");
}

TEST_CASE("JoinRequestHandler REQUEST_JOIN rejects a private guild with GUILD_NOT_FOUND") {
    Fixture f;
    f.identify(1, "bob");
    f.guilds.upsertGuild("g_1", "First", "u_owner", guild::GuildVisibility::PRIVATE);

    const auto responses =
        f.handler.handleRequestJoin(make_message("REQUEST_JOIN", {{"guild_id", "g_1"}}), 1);

    REQUIRE(responses[0].payload["code"] == "GUILD_NOT_FOUND");
}

TEST_CASE("JoinRequestHandler REQUEST_JOIN rejects an open guild with PROTOCOL_VIOLATION") {
    Fixture f;
    f.identify(1, "bob");
    f.guilds.upsertGuild("g_1", "First", "u_owner", guild::GuildVisibility::OPEN);

    const auto responses =
        f.handler.handleRequestJoin(make_message("REQUEST_JOIN", {{"guild_id", "g_1"}}), 1);

    REQUIRE(responses[0].payload["code"] == "PROTOCOL_VIOLATION");
}

TEST_CASE("JoinRequestHandler REQUEST_JOIN rejects an existing member") {
    Fixture f;
    f.identify(1, "bob");
    f.guilds.upsertGuild("g_1", "First", "u_owner", guild::GuildVisibility::APPLICATION);
    f.sessions.addGuildMembership(1, "g_1");

    const auto responses =
        f.handler.handleRequestJoin(make_message("REQUEST_JOIN", {{"guild_id", "g_1"}}), 1);

    REQUIRE(responses[0].payload["code"] == "PROTOCOL_VIOLATION");
}

TEST_CASE("JoinRequestHandler REQUEST_JOIN requires identification") {
    Fixture f;

    const auto responses =
        f.handler.handleRequestJoin(make_message("REQUEST_JOIN", {{"guild_id", "g_1"}}), 1);

    REQUIRE(responses[0].payload["code"] == "NOT_IDENTIFIED");
}

TEST_CASE("JoinRequestHandler LIST_JOIN_REQUESTS returns pending requests for an officer") {
    Fixture f;
    session::Session& owner = f.identify(1, "owner");
    f.guilds.upsertGuild("g_1", "First", owner.user_id, guild::GuildVisibility::APPLICATION);
    f.guilds.setMemberRank("g_1", owner.user_id, guild::kOwnerRank);
    f.api.join_requests_to_return = {{"u_2", "bob", "2026-01-01T00:00:00Z"}};

    const auto response =
        f.handler.handleListJoinRequests(make_message("LIST_JOIN_REQUESTS", {{"guild_id", "g_1"}}), 1);

    REQUIRE(response.type == "JOIN_REQUEST_LIST");
    REQUIRE(response.payload["requests"].size() == 1);
    REQUIRE(response.payload["requests"][0]["username"] == "bob");
}

TEST_CASE("JoinRequestHandler LIST_JOIN_REQUESTS rejects a crew-rank caller") {
    Fixture f;
    session::Session& crew = f.identify(1, "crew");
    f.guilds.upsertGuild("g_1", "First", "u_owner", guild::GuildVisibility::APPLICATION);
    f.guilds.setMemberRank("g_1", crew.user_id, guild::kMemberRank);

    const auto response =
        f.handler.handleListJoinRequests(make_message("LIST_JOIN_REQUESTS", {{"guild_id", "g_1"}}), 1);

    REQUIRE(response.payload["code"] == "NOT_GUILD_OFFICER");
}

TEST_CASE("JoinRequestHandler APPROVE_JOIN_REQUEST creates the membership and notifies the "
          "approved user's connections") {
    Fixture f;
    session::Session& owner = f.identify(1, "owner");
    session::Session& requester_tab1 = f.identify(2, "bob");
    session::Session& requester_tab2 = f.identify(3, "bob");
    f.guilds.upsertGuild("g_1", "First", owner.user_id, guild::GuildVisibility::APPLICATION);
    f.guilds.setMemberRank("g_1", owner.user_id, guild::kOwnerRank);
    requester_tab2.user_id = requester_tab1.user_id; // same user, second tab
    f.api.approve_join_request_returns_rank = guild::kMemberRank;

    const auto responses = f.handler.handleApproveJoinRequest(
        make_message("APPROVE_JOIN_REQUEST", {{"guild_id", "g_1"}, {"user_id", requester_tab1.user_id}}), 1);

    REQUIRE(responses.size() == 2);
    REQUIRE(responses[0].type == "JOIN_REQUEST_APPROVED");
    REQUIRE(responses[0].payload["user_id"] == requester_tab1.user_id);
    REQUIRE(responses[1].type == "GUILD_JOINED");
    REQUIRE(responses[1].scope == protocol::Scope::TARGETED);
    const auto& fds = responses[1].target_fds;
    REQUIRE(std::find(fds.begin(), fds.end(), 2) != fds.end());
    REQUIRE(std::find(fds.begin(), fds.end(), 3) != fds.end());
    REQUIRE(f.guilds.getMemberRank("g_1", requester_tab1.user_id) == guild::kMemberRank);
    REQUIRE(f.sessions.isMemberOfGuild(2, "g_1"));
    REQUIRE(f.sessions.isMemberOfGuild(3, "g_1"));
}

TEST_CASE("JoinRequestHandler APPROVE_JOIN_REQUEST omits GUILD_JOINED when the approved user isn't "
          "currently connected") {
    Fixture f;
    session::Session& owner = f.identify(1, "owner");
    f.guilds.upsertGuild("g_1", "First", owner.user_id, guild::GuildVisibility::APPLICATION);
    f.guilds.setMemberRank("g_1", owner.user_id, guild::kOwnerRank);

    const auto responses = f.handler.handleApproveJoinRequest(
        make_message("APPROVE_JOIN_REQUEST", {{"guild_id", "g_1"}, {"user_id", "u_offline"}}), 1);

    REQUIRE(responses.size() == 1);
    REQUIRE(responses[0].type == "JOIN_REQUEST_APPROVED");
}

TEST_CASE("JoinRequestHandler APPROVE_JOIN_REQUEST returns JOIN_REQUEST_NOT_FOUND when no request "
          "exists") {
    Fixture f;
    session::Session& owner = f.identify(1, "owner");
    f.guilds.upsertGuild("g_1", "First", owner.user_id, guild::GuildVisibility::APPLICATION);
    f.guilds.setMemberRank("g_1", owner.user_id, guild::kOwnerRank);
    f.api.fail_approve_join_request = true;

    const auto responses = f.handler.handleApproveJoinRequest(
        make_message("APPROVE_JOIN_REQUEST", {{"guild_id", "g_1"}, {"user_id", "u_2"}}), 1);

    REQUIRE(responses.size() == 1);
    REQUIRE(responses[0].payload["code"] == "JOIN_REQUEST_NOT_FOUND");
}

TEST_CASE("JoinRequestHandler APPROVE_JOIN_REQUEST rejects a crew-rank caller") {
    Fixture f;
    session::Session& crew = f.identify(1, "crew");
    f.guilds.upsertGuild("g_1", "First", "u_owner", guild::GuildVisibility::APPLICATION);
    f.guilds.setMemberRank("g_1", crew.user_id, guild::kMemberRank);

    const auto responses = f.handler.handleApproveJoinRequest(
        make_message("APPROVE_JOIN_REQUEST", {{"guild_id", "g_1"}, {"user_id", "u_2"}}), 1);

    REQUIRE(responses[0].payload["code"] == "NOT_GUILD_OFFICER");
}

TEST_CASE("JoinRequestHandler REJECT_JOIN_REQUEST deletes the request and notifies the rejected "
          "user") {
    Fixture f;
    session::Session& owner = f.identify(1, "owner");
    session::Session& requester = f.identify(2, "bob");
    f.guilds.upsertGuild("g_1", "First", owner.user_id, guild::GuildVisibility::APPLICATION);
    f.guilds.setMemberRank("g_1", owner.user_id, guild::kOwnerRank);

    const auto responses = f.handler.handleRejectJoinRequest(
        make_message("REJECT_JOIN_REQUEST", {{"guild_id", "g_1"}, {"user_id", requester.user_id}}), 1);

    REQUIRE(responses.size() == 2);
    REQUIRE(responses[0].type == "JOIN_REQUEST_REJECTED");
    REQUIRE(responses[0].scope == protocol::Scope::DIRECT);
    REQUIRE(responses[1].type == "JOIN_REQUEST_REJECTED");
    REQUIRE(responses[1].scope == protocol::Scope::TARGETED);
    REQUIRE(responses[1].target_fds == std::vector<int>{2});
    REQUIRE_FALSE(f.sessions.isMemberOfGuild(2, "g_1"));
}

TEST_CASE("JoinRequestHandler REJECT_JOIN_REQUEST omits the second message when the rejected user "
          "isn't currently connected") {
    Fixture f;
    session::Session& owner = f.identify(1, "owner");
    f.guilds.upsertGuild("g_1", "First", owner.user_id, guild::GuildVisibility::APPLICATION);
    f.guilds.setMemberRank("g_1", owner.user_id, guild::kOwnerRank);

    const auto responses = f.handler.handleRejectJoinRequest(
        make_message("REJECT_JOIN_REQUEST", {{"guild_id", "g_1"}, {"user_id", "u_offline"}}), 1);

    REQUIRE(responses.size() == 1);
}

TEST_CASE("JoinRequestHandler REJECT_JOIN_REQUEST returns JOIN_REQUEST_NOT_FOUND when no request "
          "exists") {
    Fixture f;
    session::Session& owner = f.identify(1, "owner");
    f.guilds.upsertGuild("g_1", "First", owner.user_id, guild::GuildVisibility::APPLICATION);
    f.guilds.setMemberRank("g_1", owner.user_id, guild::kOwnerRank);
    f.api.fail_reject_join_request = true;

    const auto responses = f.handler.handleRejectJoinRequest(
        make_message("REJECT_JOIN_REQUEST", {{"guild_id", "g_1"}, {"user_id", "u_2"}}), 1);

    REQUIRE(responses.size() == 1);
    REQUIRE(responses[0].payload["code"] == "JOIN_REQUEST_NOT_FOUND");
}

TEST_CASE("JoinRequestHandler REJECT_JOIN_REQUEST rejects a crew-rank caller") {
    Fixture f;
    session::Session& crew = f.identify(1, "crew");
    f.guilds.upsertGuild("g_1", "First", "u_owner", guild::GuildVisibility::APPLICATION);
    f.guilds.setMemberRank("g_1", crew.user_id, guild::kMemberRank);

    const auto responses = f.handler.handleRejectJoinRequest(
        make_message("REJECT_JOIN_REQUEST", {{"guild_id", "g_1"}, {"user_id", "u_2"}}), 1);

    REQUIRE(responses[0].payload["code"] == "NOT_GUILD_OFFICER");
}
