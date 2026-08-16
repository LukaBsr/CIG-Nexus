#include <catch2/catch_test_macros.hpp>

#include "protocol/handlers/FriendHandler.hpp"
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
    protocol::FriendHandler handler;

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

// --- SEND_FRIEND_REQUEST -----------------------------------------------

TEST_CASE("FriendHandler SEND_FRIEND_REQUEST requires identification") {
    Fixture f;
    const auto responses =
        f.handler.handleSendFriendRequest(make_message("SEND_FRIEND_REQUEST", {{"user_id", "u_2"}}), 1);
    REQUIRE(responses.size() == 1);
    REQUIRE(responses[0].payload["code"] == "NOT_IDENTIFIED");
}

TEST_CASE("FriendHandler SEND_FRIEND_REQUEST requires user_id") {
    Fixture f;
    f.identify(1, "alice");
    const auto responses = f.handler.handleSendFriendRequest(make_message("SEND_FRIEND_REQUEST"), 1);
    REQUIRE(responses.size() == 1);
    REQUIRE(responses[0].payload["code"] == "MALFORMED_MESSAGE");
}

TEST_CASE("FriendHandler SEND_FRIEND_REQUEST success sends SENT to caller and RECEIVED to target") {
    Fixture f;
    f.identify(1, "alice");
    f.identify(2, "bob");
    f.api.send_friend_request_returns =
        http::SendFriendRequestResult{http::SendFriendRequestOutcome::REQUEST_CREATED, "u_2", "bob"};

    const auto responses =
        f.handler.handleSendFriendRequest(make_message("SEND_FRIEND_REQUEST", {{"user_id", "u_2"}}), 1);

    REQUIRE(responses.size() == 2);
    REQUIRE(responses[0].type == "FRIEND_REQUEST_SENT");
    REQUIRE(responses[0].payload["user_id"] == "u_2");
    REQUIRE(responses[1].type == "FRIEND_REQUEST_RECEIVED");
    REQUIRE(responses[1].scope == protocol::Scope::TARGETED);
    REQUIRE(responses[1].target_fds == std::vector<int>{2});
    REQUIRE(responses[1].payload["user_id"] == "u_1");
}

TEST_CASE("FriendHandler SEND_FRIEND_REQUEST omits the targeted copy when the target is offline") {
    Fixture f;
    f.identify(1, "alice");
    f.api.send_friend_request_returns =
        http::SendFriendRequestResult{http::SendFriendRequestOutcome::REQUEST_CREATED, "u_2", "bob"};

    const auto responses =
        f.handler.handleSendFriendRequest(make_message("SEND_FRIEND_REQUEST", {{"user_id", "u_2"}}), 1);

    REQUIRE(responses.size() == 1);
    REQUIRE(responses[0].type == "FRIEND_REQUEST_SENT");
}

TEST_CASE("FriendHandler SEND_FRIEND_REQUEST auto-accept sends FRIEND_ADDED to both") {
    Fixture f;
    f.identify(1, "alice");
    f.identify(2, "bob");
    f.api.send_friend_request_returns =
        http::SendFriendRequestResult{http::SendFriendRequestOutcome::FRIENDS_ADDED, "u_2", "bob"};

    const auto responses =
        f.handler.handleSendFriendRequest(make_message("SEND_FRIEND_REQUEST", {{"user_id", "u_2"}}), 1);

    REQUIRE(responses.size() == 2);
    REQUIRE(responses[0].type == "FRIEND_ADDED");
    REQUIRE(responses[0].payload["user_id"] == "u_2");
    REQUIRE(responses[1].type == "FRIEND_ADDED");
    REQUIRE(responses[1].scope == protocol::Scope::TARGETED);
    REQUIRE(responses[1].payload["user_id"] == "u_1");
}

TEST_CASE("FriendHandler SEND_FRIEND_REQUEST maps each failure outcome to the right error code") {
    Fixture f;
    f.identify(1, "alice");

    SECTION("user not found") {
        f.api.send_friend_request_returns.outcome = http::SendFriendRequestOutcome::USER_NOT_FOUND;
        const auto responses =
            f.handler.handleSendFriendRequest(make_message("SEND_FRIEND_REQUEST", {{"user_id", "u_2"}}), 1);
        REQUIRE(responses[0].payload["code"] == "USER_NOT_FOUND");
    }
    SECTION("self") {
        f.api.send_friend_request_returns.outcome = http::SendFriendRequestOutcome::SELF;
        const auto responses =
            f.handler.handleSendFriendRequest(make_message("SEND_FRIEND_REQUEST", {{"user_id", "u_1"}}), 1);
        REQUIRE(responses[0].payload["code"] == "PROTOCOL_VIOLATION");
    }
    SECTION("already friends") {
        f.api.send_friend_request_returns.outcome = http::SendFriendRequestOutcome::ALREADY_FRIENDS;
        const auto responses =
            f.handler.handleSendFriendRequest(make_message("SEND_FRIEND_REQUEST", {{"user_id", "u_2"}}), 1);
        REQUIRE(responses[0].payload["code"] == "PROTOCOL_VIOLATION");
    }
    SECTION("failed") {
        f.api.send_friend_request_returns.outcome = http::SendFriendRequestOutcome::FAILED;
        const auto responses =
            f.handler.handleSendFriendRequest(make_message("SEND_FRIEND_REQUEST", {{"user_id", "u_2"}}), 1);
        REQUIRE(responses[0].payload["code"] == "INTERNAL_ERROR");
    }
}

// --- ADD_FRIEND_BY_CODE --------------------------------------------------

TEST_CASE("FriendHandler ADD_FRIEND_BY_CODE requires code") {
    Fixture f;
    f.identify(1, "alice");
    const auto responses = f.handler.handleAddFriendByCode(make_message("ADD_FRIEND_BY_CODE"), 1);
    REQUIRE(responses[0].payload["code"] == "MALFORMED_MESSAGE");
}

TEST_CASE("FriendHandler ADD_FRIEND_BY_CODE reuses SEND_FRIEND_REQUEST's response shape on success") {
    Fixture f;
    f.identify(1, "alice");
    f.api.add_friend_by_code_returns =
        http::SendFriendRequestResult{http::SendFriendRequestOutcome::REQUEST_CREATED, "u_2", "bob"};

    const auto responses =
        f.handler.handleAddFriendByCode(make_message("ADD_FRIEND_BY_CODE", {{"code", "abc123"}}), 1);

    REQUIRE(responses[0].type == "FRIEND_REQUEST_SENT");
}

TEST_CASE("FriendHandler ADD_FRIEND_BY_CODE maps CODE_NOT_FOUND to FRIEND_CODE_NOT_FOUND") {
    Fixture f;
    f.identify(1, "alice");
    f.api.add_friend_by_code_returns.outcome = http::SendFriendRequestOutcome::CODE_NOT_FOUND;

    const auto responses =
        f.handler.handleAddFriendByCode(make_message("ADD_FRIEND_BY_CODE", {{"code", "nope"}}), 1);

    REQUIRE(responses[0].payload["code"] == "FRIEND_CODE_NOT_FOUND");
}

// --- ACCEPT/REJECT/CANCEL_FRIEND_REQUEST ---------------------------------

TEST_CASE("FriendHandler ACCEPT_FRIEND_REQUEST success notifies both parties") {
    Fixture f;
    f.identify(1, "alice"); // recipient/caller
    f.identify(2, "bob");   // original requester
    f.api.accept_friend_request_returns = http::AcceptFriendRequestResult{true, "u_2", "bob"};

    const auto responses =
        f.handler.handleAcceptFriendRequest(make_message("ACCEPT_FRIEND_REQUEST", {{"user_id", "u_2"}}), 1);

    REQUIRE(responses.size() == 2);
    REQUIRE(responses[0].type == "FRIEND_ADDED");
    REQUIRE(responses[0].payload["user_id"] == "u_2");
    REQUIRE(responses[1].type == "FRIEND_ADDED");
    REQUIRE(responses[1].scope == protocol::Scope::TARGETED);
    REQUIRE(responses[1].payload["user_id"] == "u_1");
}

TEST_CASE("FriendHandler ACCEPT_FRIEND_REQUEST returns FRIEND_REQUEST_NOT_FOUND on miss") {
    Fixture f;
    f.identify(1, "alice");
    f.api.accept_friend_request_returns.ok = false;

    const auto responses =
        f.handler.handleAcceptFriendRequest(make_message("ACCEPT_FRIEND_REQUEST", {{"user_id", "u_2"}}), 1);

    REQUIRE(responses[0].payload["code"] == "FRIEND_REQUEST_NOT_FOUND");
}

TEST_CASE("FriendHandler REJECT_FRIEND_REQUEST success notifies both parties") {
    Fixture f;
    f.identify(1, "alice"); // recipient/caller, rejecting
    f.identify(2, "bob");   // original requester
    f.api.fail_delete_friend_request = false;

    const auto responses =
        f.handler.handleRejectFriendRequest(make_message("REJECT_FRIEND_REQUEST", {{"user_id", "u_2"}}), 1);

    REQUIRE(responses.size() == 2);
    REQUIRE(responses[0].type == "FRIEND_REQUEST_REJECTED");
    REQUIRE(responses[0].payload["user_id"] == "u_2");
    REQUIRE(responses[1].scope == protocol::Scope::TARGETED);
    REQUIRE(responses[1].payload["user_id"] == "u_1");
}

TEST_CASE("FriendHandler REJECT_FRIEND_REQUEST returns FRIEND_REQUEST_NOT_FOUND on miss") {
    Fixture f;
    f.identify(1, "alice");
    f.api.fail_delete_friend_request = true;

    const auto responses =
        f.handler.handleRejectFriendRequest(make_message("REJECT_FRIEND_REQUEST", {{"user_id", "u_2"}}), 1);

    REQUIRE(responses[0].payload["code"] == "FRIEND_REQUEST_NOT_FOUND");
}

TEST_CASE("FriendHandler CANCEL_FRIEND_REQUEST success notifies both parties") {
    Fixture f;
    f.identify(1, "alice"); // requester/caller, canceling
    f.identify(2, "bob");   // recipient

    const auto responses =
        f.handler.handleCancelFriendRequest(make_message("CANCEL_FRIEND_REQUEST", {{"user_id", "u_2"}}), 1);

    REQUIRE(responses.size() == 2);
    REQUIRE(responses[0].type == "FRIEND_REQUEST_CANCELED");
    REQUIRE(responses[1].scope == protocol::Scope::TARGETED);
}

// --- REMOVE_FRIEND --------------------------------------------------------

TEST_CASE("FriendHandler REMOVE_FRIEND success notifies both parties") {
    Fixture f;
    f.identify(1, "alice");
    f.identify(2, "bob");

    const auto responses = f.handler.handleRemoveFriend(make_message("REMOVE_FRIEND", {{"user_id", "u_2"}}), 1);

    REQUIRE(responses.size() == 2);
    REQUIRE(responses[0].type == "FRIEND_REMOVED");
    REQUIRE(responses[1].scope == protocol::Scope::TARGETED);
}

TEST_CASE("FriendHandler REMOVE_FRIEND returns FRIEND_NOT_FOUND on miss") {
    Fixture f;
    f.identify(1, "alice");
    f.api.fail_remove_friend = true;

    const auto responses = f.handler.handleRemoveFriend(make_message("REMOVE_FRIEND", {{"user_id", "u_2"}}), 1);

    REQUIRE(responses[0].payload["code"] == "FRIEND_NOT_FOUND");
}

// --- LIST_FRIENDS / LIST_FRIEND_REQUESTS ----------------------------------

TEST_CASE("FriendHandler LIST_FRIENDS returns FRIEND_LIST") {
    Fixture f;
    f.identify(1, "alice");
    f.api.friends_to_return = {{"u_2", "bob", "Bobby", std::nullopt}};

    const auto response = f.handler.handleListFriends(make_message("LIST_FRIENDS"), 1);

    REQUIRE(response.type == "FRIEND_LIST");
    REQUIRE(response.payload["friends"].size() == 1);
    REQUIRE(response.payload["friends"][0]["user_id"] == "u_2");
    REQUIRE(response.payload["friends"][0]["display_name"] == "Bobby");
    REQUIRE(response.payload["friends"][0]["avatar_url"].is_null());
}

TEST_CASE("FriendHandler LIST_FRIEND_REQUESTS separates incoming/outgoing") {
    Fixture f;
    f.identify(1, "alice");
    f.api.friend_requests_to_return.incoming = {{"u_3", "carol", "2026-01-01T00:00:00Z", std::nullopt, std::nullopt}};
    f.api.friend_requests_to_return.outgoing = {{"u_2", "bob", "2026-01-01T00:00:00Z", std::nullopt, std::nullopt}};

    const auto response = f.handler.handleListFriendRequests(make_message("LIST_FRIEND_REQUESTS"), 1);

    REQUIRE(response.type == "FRIEND_REQUEST_LIST");
    REQUIRE(response.payload["incoming"].size() == 1);
    REQUIRE(response.payload["incoming"][0]["user_id"] == "u_3");
    REQUIRE(response.payload["outgoing"].size() == 1);
    REQUIRE(response.payload["outgoing"][0]["user_id"] == "u_2");
}

// --- FETCH/REGENERATE_FRIEND_CODE -----------------------------------------

TEST_CASE("FriendHandler FETCH_FRIEND_CODE returns FRIEND_CODE") {
    Fixture f;
    f.identify(1, "alice");
    f.api.friend_code_to_return = "abc123";

    const auto response = f.handler.handleFetchFriendCode(make_message("FETCH_FRIEND_CODE"), 1);

    REQUIRE(response.type == "FRIEND_CODE");
    REQUIRE(response.payload["code"] == "abc123");
}

TEST_CASE("FriendHandler REGENERATE_FRIEND_CODE returns a new FRIEND_CODE") {
    Fixture f;
    f.identify(1, "alice");
    f.api.friend_code_to_return = "new-code";

    const auto response = f.handler.handleRegenerateFriendCode(make_message("REGENERATE_FRIEND_CODE"), 1);

    REQUIRE(response.type == "FRIEND_CODE");
    REQUIRE(response.payload["code"] == "new-code");
}
