#include <catch2/catch_test_macros.hpp>

#include "protocol/handlers/BlockHandler.hpp"
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
    test_helpers::FakeInternalApiClient api;
    protocol::BlockHandler handler;

    Fixture() {
        handler.setSessionManager(&sessions);
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

TEST_CASE("BlockHandler BLOCK_USER requires identification") {
    Fixture f;
    const auto response = f.handler.handleBlockUser(make_message("BLOCK_USER", {{"user_id", "u_2"}}), 1);
    REQUIRE(response.payload["code"] == "NOT_IDENTIFIED");
}

TEST_CASE("BlockHandler BLOCK_USER requires user_id") {
    Fixture f;
    f.identify(1, "alice");
    const auto response = f.handler.handleBlockUser(make_message("BLOCK_USER"), 1);
    REQUIRE(response.payload["code"] == "MALFORMED_MESSAGE");
}

TEST_CASE("BlockHandler BLOCK_USER rejects blocking self") {
    Fixture f;
    f.identify(1, "alice");
    const auto response = f.handler.handleBlockUser(make_message("BLOCK_USER", {{"user_id", "u_1"}}), 1);
    REQUIRE(response.payload["code"] == "PROTOCOL_VIOLATION");
}

TEST_CASE("BlockHandler BLOCK_USER success returns USER_BLOCKED to the caller only") {
    Fixture f;
    f.identify(1, "alice");
    f.identify(2, "bob");

    const auto response = f.handler.handleBlockUser(make_message("BLOCK_USER", {{"user_id", "u_2"}}), 1);

    REQUIRE(response.type == "USER_BLOCKED");
    REQUIRE(response.payload["user_id"] == "u_2");
    REQUIRE(response.scope == protocol::Scope::DIRECT);
}

TEST_CASE("BlockHandler BLOCK_USER success updates every connection of the caller's own account") {
    Fixture f;
    f.identify(1, "alice");
    f.identify(11, "alice-second-tab");
    f.sessions.getSession(11)->user_id = "u_1"; // same account, second connection
    f.identify(2, "bob");

    f.handler.handleBlockUser(make_message("BLOCK_USER", {{"user_id", "u_2"}}), 1);

    REQUIRE(f.sessions.getSession(1)->blocked_user_ids == std::vector<std::string>{"u_2"});
    REQUIRE(f.sessions.getSession(11)->blocked_user_ids == std::vector<std::string>{"u_2"});
}

TEST_CASE("BlockHandler BLOCK_USER maps internal API failure to USER_NOT_FOUND") {
    Fixture f;
    f.identify(1, "alice");
    f.api.fail_block_user = true;

    const auto response = f.handler.handleBlockUser(make_message("BLOCK_USER", {{"user_id", "u_2"}}), 1);

    REQUIRE(response.payload["code"] == "USER_NOT_FOUND");
}

TEST_CASE("BlockHandler UNBLOCK_USER success returns USER_UNBLOCKED and clears blocked_user_ids") {
    Fixture f;
    session::Session& alice = f.identify(1, "alice");
    alice.blocked_user_ids = {"u_2"};

    const auto response = f.handler.handleUnblockUser(make_message("UNBLOCK_USER", {{"user_id", "u_2"}}), 1);

    REQUIRE(response.type == "USER_UNBLOCKED");
    REQUIRE(response.scope == protocol::Scope::DIRECT);
    REQUIRE(f.sessions.getSession(1)->blocked_user_ids.empty());
}

TEST_CASE("BlockHandler UNBLOCK_USER maps internal API failure to USER_NOT_FOUND") {
    Fixture f;
    f.identify(1, "alice");
    f.api.fail_unblock_user = true;

    const auto response = f.handler.handleUnblockUser(make_message("UNBLOCK_USER", {{"user_id", "u_2"}}), 1);

    REQUIRE(response.payload["code"] == "USER_NOT_FOUND");
}

TEST_CASE("BlockHandler LIST_BLOCKS returns BLOCK_LIST") {
    Fixture f;
    f.identify(1, "alice");
    f.api.blocks_to_return = {{"u_2", "bob", "2026-01-01T00:00:00Z"}};

    const auto response = f.handler.handleListBlocks(make_message("LIST_BLOCKS"), 1);

    REQUIRE(response.type == "BLOCK_LIST");
    REQUIRE(response.payload["blocked"].size() == 1);
    REQUIRE(response.payload["blocked"][0]["user_id"] == "u_2");
}
