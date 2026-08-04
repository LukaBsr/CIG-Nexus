#include <catch2/catch_test_macros.hpp>

#include "protocol/handlers/DMHandler.hpp"
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
    test_helpers::FakeInternalApiClient api;
    protocol::DMHandler handler;

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

// --- DM_SEND: validation ---------------------------------------------

TEST_CASE("DMHandler DM_SEND requires identification") {
    Fixture f;
    const auto response =
        f.handler.handleDmSend(make_message("DM_SEND", {{"user_id", "u_2"}, {"content", "hi"}}), 1);
    REQUIRE(response.payload["code"] == "NOT_IDENTIFIED");
}

TEST_CASE("DMHandler DM_SEND requires user_id and content") {
    Fixture f;
    f.identify(1, "alice");
    REQUIRE(f.handler.handleDmSend(make_message("DM_SEND", {{"content", "hi"}}), 1).payload["code"] ==
            "MALFORMED_MESSAGE");
    REQUIRE(f.handler.handleDmSend(make_message("DM_SEND", {{"user_id", "u_2"}}), 1).payload["code"] ==
            "MALFORMED_MESSAGE");
}

TEST_CASE("DMHandler DM_SEND rejects messaging self") {
    Fixture f;
    f.identify(1, "alice");
    const auto response =
        f.handler.handleDmSend(make_message("DM_SEND", {{"user_id", "u_1"}, {"content", "hi"}}), 1);
    REQUIRE(response.payload["code"] == "PROTOCOL_VIOLATION");
}

TEST_CASE("DMHandler DM_SEND rejects empty and overlong content") {
    Fixture f;
    session::Session& alice = f.identify(1, "alice");
    alice.friend_ids = {"u_2"};

    REQUIRE(f.handler.handleDmSend(make_message("DM_SEND", {{"user_id", "u_2"}, {"content", ""}}), 1)
                .payload["code"] == "MALFORMED_MESSAGE");
    REQUIRE(f.handler
                .handleDmSend(make_message("DM_SEND", {{"user_id", "u_2"}, {"content", std::string(501, 'x')}}), 1)
                .payload["code"] == "MALFORMED_MESSAGE");
}

// --- DM_SEND: canSendDm, peer online -----------------------------------

TEST_CASE("DMHandler DM_SEND permitted via friendship, delivered to both participants' connections") {
    Fixture f;
    session::Session& alice = f.identify(1, "alice");
    f.identify(2, "bob");
    alice.friend_ids = {"u_2"};

    const auto response =
        f.handler.handleDmSend(make_message("DM_SEND", {{"user_id", "u_2"}, {"content", "hi"}}), 1);

    REQUIRE(response.type == "DM_MESSAGE");
    REQUIRE(response.scope == protocol::Scope::TARGETED);
    REQUIRE(response.payload["user_id"] == "u_1");
    REQUIRE(response.payload["content"] == "hi");
    std::vector<int> fds = response.target_fds;
    std::sort(fds.begin(), fds.end());
    REQUIRE(fds == std::vector<int>{1, 2});
}

TEST_CASE("DMHandler DM_SEND permitted via shared guild membership (peer online, no friendship)") {
    Fixture f;
    session::Session& alice = f.identify(1, "alice");
    session::Session& bob = f.identify(2, "bob");
    alice.guild_ids = {"g_1"};
    bob.guild_ids = {"g_1", "g_2"};

    const auto response =
        f.handler.handleDmSend(make_message("DM_SEND", {{"user_id", "u_2"}, {"content", "hi"}}), 1);

    REQUIRE(response.type == "DM_MESSAGE");
}

TEST_CASE("DMHandler DM_SEND rejected when neither friends nor sharing a guild") {
    Fixture f;
    f.identify(1, "alice");
    f.identify(2, "bob");

    const auto response =
        f.handler.handleDmSend(make_message("DM_SEND", {{"user_id", "u_2"}, {"content", "hi"}}), 1);

    REQUIRE(response.payload["code"] == "DM_NOT_PERMITTED");
}

TEST_CASE("DMHandler DM_SEND rejected when the caller has blocked the peer, even if friends") {
    Fixture f;
    session::Session& alice = f.identify(1, "alice");
    f.identify(2, "bob");
    alice.friend_ids = {"u_2"};
    alice.blocked_user_ids = {"u_2"};

    const auto response =
        f.handler.handleDmSend(make_message("DM_SEND", {{"user_id", "u_2"}, {"content", "hi"}}), 1);

    REQUIRE(response.payload["code"] == "DM_NOT_PERMITTED");
}

TEST_CASE("DMHandler DM_SEND rejected when the peer has blocked the caller (peer online)") {
    Fixture f;
    session::Session& alice = f.identify(1, "alice");
    session::Session& bob = f.identify(2, "bob");
    alice.friend_ids = {"u_2"};
    bob.blocked_user_ids = {"u_1"};

    const auto response =
        f.handler.handleDmSend(make_message("DM_SEND", {{"user_id", "u_2"}, {"content", "hi"}}), 1);

    REQUIRE(response.payload["code"] == "DM_NOT_PERMITTED");
}

// --- DM_SEND: canSendDm, peer offline (live-fallback paths) -------------

TEST_CASE("DMHandler DM_SEND permitted via live shared-guild fallback when peer is offline") {
    Fixture f;
    session::Session& alice = f.identify(1, "alice");
    alice.guild_ids = {"g_1"};
    f.api.guild_ids_for_user_to_return = {"g_1", "g_9"}; // peer's guilds, fetched live

    const auto response =
        f.handler.handleDmSend(make_message("DM_SEND", {{"user_id", "u_2"}, {"content", "hi"}}), 1);

    REQUIRE(response.type == "DM_MESSAGE");
}

TEST_CASE("DMHandler DM_SEND rejected when live shared-guild fallback finds no overlap") {
    Fixture f;
    session::Session& alice = f.identify(1, "alice");
    alice.guild_ids = {"g_1"};
    f.api.guild_ids_for_user_to_return = {"g_9"}; // no overlap

    const auto response =
        f.handler.handleDmSend(make_message("DM_SEND", {{"user_id", "u_2"}, {"content", "hi"}}), 1);

    REQUIRE(response.payload["code"] == "DM_NOT_PERMITTED");
}

TEST_CASE("DMHandler DM_SEND rejected via live block fallback when the offline peer has blocked the caller") {
    Fixture f;
    session::Session& alice = f.identify(1, "alice");
    alice.friend_ids = {"u_2"}; // permitted on the friend/guild axis
    f.api.blocks_to_return = {{"u_1", "alice", "2026-01-01T00:00:00Z"}}; // peer (offline) has blocked u_1

    const auto response =
        f.handler.handleDmSend(make_message("DM_SEND", {{"user_id", "u_2"}, {"content", "hi"}}), 1);

    REQUIRE(response.payload["code"] == "DM_NOT_PERMITTED");
}

TEST_CASE("DMHandler DM_SEND permitted when the offline peer's block list doesn't include the caller") {
    Fixture f;
    session::Session& alice = f.identify(1, "alice");
    alice.friend_ids = {"u_2"};
    f.api.blocks_to_return = {{"u_999", "someone-else", "2026-01-01T00:00:00Z"}};

    const auto response =
        f.handler.handleDmSend(make_message("DM_SEND", {{"user_id", "u_2"}, {"content", "hi"}}), 1);

    REQUIRE(response.type == "DM_MESSAGE");
}

// --- FETCH_HISTORY (peer_id) / LIST_DM_CONVERSATIONS ---------------------

TEST_CASE("DMHandler FETCH_HISTORY returns MESSAGE_HISTORY with peer_id echoed") {
    Fixture f;
    f.identify(1, "alice");
    f.api.history_page_to_return.messages = {{1, std::nullopt, 1741104000, "u_2", "bob", "hey"}};
    f.api.history_page_to_return.has_more = false;

    const auto response = f.handler.handleFetchHistory(make_message("FETCH_HISTORY", {{"peer_id", "u_2"}}), 1);

    REQUIRE(response.type == "MESSAGE_HISTORY");
    REQUIRE(response.payload["peer_id"] == "u_2");
    REQUIRE(response.payload["channel_id"].is_null());
    REQUIRE(response.payload["messages"].size() == 1);
}

TEST_CASE("DMHandler FETCH_HISTORY requires peer_id to be a string") {
    Fixture f;
    f.identify(1, "alice");
    const auto response = f.handler.handleFetchHistory(make_message("FETCH_HISTORY"), 1);
    REQUIRE(response.payload["code"] == "MALFORMED_MESSAGE");
}

TEST_CASE("DMHandler LIST_DM_CONVERSATIONS returns DM_CONVERSATION_LIST") {
    Fixture f;
    f.identify(1, "alice");
    f.api.dm_conversations_to_return = {{"u_2", "2026-01-01T00:00:00Z"}};

    const auto response = f.handler.handleListDmConversations(make_message("LIST_DM_CONVERSATIONS"), 1);

    REQUIRE(response.type == "DM_CONVERSATION_LIST");
    REQUIRE(response.payload["conversations"].size() == 1);
    REQUIRE(response.payload["conversations"][0]["peer_id"] == "u_2");
}
