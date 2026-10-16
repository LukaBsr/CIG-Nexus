#include <catch2/catch_test_macros.hpp>

#include "protocol/MessageBuilders.hpp"
#include "protocol/MessageDispatcher.hpp"

TEST_CASE("MessageDispatcher dispatches to registered handler") {
    protocol::MessageDispatcher dispatcher;

    dispatcher.registerHandler("HELLO", [](const protocol::Message& message, int /*fd*/) {
        protocol::Message response;
        response.type = "WELCOME";
        response.payload = message.payload;
        return std::vector<protocol::Message>{response};
    });

    protocol::Message message;
    message.type = "HELLO";
    message.payload = {{"type", "HELLO"}};

    const auto responses = dispatcher.dispatch(message, -1);

    REQUIRE(responses.size() == 1);
    REQUIRE(responses[0].type == "WELCOME");
}

TEST_CASE("MessageDispatcher throws on unknown message type") {
    protocol::MessageDispatcher dispatcher;

    protocol::Message message;
    message.type = "UNKNOWN";
    message.payload = {{"type", "UNKNOWN"}};

    REQUIRE_THROWS(dispatcher.dispatch(message, -1));
}

TEST_CASE("MessageDispatcher returns handler response") {
    protocol::MessageDispatcher dispatcher;

    dispatcher.registerHandler("PING", [](const protocol::Message& message, int /*fd*/) {
        protocol::Message response;
        response.type = "PONG";
        response.payload = message.payload;
        return std::vector<protocol::Message>{response};
    });

    protocol::Message message;
    message.type = "PING";
    message.payload = {{"type", "PING"}};

    const auto responses = dispatcher.dispatch(message, -1);

    REQUIRE(responses.size() == 1);
    REQUIRE(responses[0].type == "PONG");
}

// docs/guilds/social-presence-design.md §1.9's ARBITRATION: the whole point of the
// widened contract — a handler notifying two different recipients with two
// different payloads from one client action (e.g. a future
// APPROVE_JOIN_REQUEST). Nothing shipped today actually returns more than
// one element, so this is the mechanism's only coverage until Step 6.
TEST_CASE("MessageDispatcher supports a handler returning multiple messages") {
    protocol::MessageDispatcher dispatcher;

    dispatcher.registerHandler("APPROVE_SOMETHING", [](const protocol::Message&, int /*fd*/) {
        protocol::Message to_approver;
        to_approver.type = "APPROVED";
        to_approver.scope = protocol::Scope::DIRECT;

        protocol::Message to_target;
        to_target.type = "YOU_WERE_APPROVED";
        to_target.scope = protocol::Scope::TARGETED;
        to_target.target_fds = {7};

        return std::vector<protocol::Message>{to_approver, to_target};
    });

    protocol::Message message;
    message.type = "APPROVE_SOMETHING";
    message.payload = {{"type", "APPROVE_SOMETHING"}};

    const auto responses = dispatcher.dispatch(message, 3);

    REQUIRE(responses.size() == 2);
    REQUIRE(responses[0].type == "APPROVED");
    REQUIRE(responses[0].scope == protocol::Scope::DIRECT);
    REQUIRE(responses[1].type == "YOU_WERE_APPROVED");
    REQUIRE(responses[1].scope == protocol::Scope::TARGETED);
    REQUIRE(responses[1].target_fds == std::vector<int>{7});
}

TEST_CASE("MessageDispatcher supports a handler returning zero messages") {
    protocol::MessageDispatcher dispatcher;

    dispatcher.registerHandler("SILENT", [](const protocol::Message&, int /*fd*/) {
        return std::vector<protocol::Message>{};
    });

    protocol::Message message;
    message.type = "SILENT";
    message.payload = {{"type", "SILENT"}};

    const auto responses = dispatcher.dispatch(message, -1);

    REQUIRE(responses.empty());
}

// shared/protocol/README.md's ERROR section: every ERROR answering a client
// request carries that request's type as request_type, so a client can tell
// which of its requests failed. Stamped here, centrally, not per handler.
TEST_CASE("MessageDispatcher stamps request_type on a handler's ERROR response") {
    protocol::MessageDispatcher dispatcher;

    dispatcher.registerHandler("MARK_DM_READ", [](const protocol::Message&, int /*fd*/) {
        return std::vector<protocol::Message>{
            protocol::make_error_message("MALFORMED_MESSAGE", "No such DM conversation")};
    });

    protocol::Message message;
    message.type = "MARK_DM_READ";
    message.payload = {{"type", "MARK_DM_READ"}};

    const auto responses = dispatcher.dispatch(message, -1);

    REQUIRE(responses.size() == 1);
    REQUIRE(responses[0].payload["type"] == "ERROR");
    REQUIRE(responses[0].payload["code"] == "MALFORMED_MESSAGE");
    REQUIRE(responses[0].payload["request_type"] == "MARK_DM_READ");
}

TEST_CASE("MessageDispatcher leaves non-ERROR and non-DIRECT responses unstamped") {
    protocol::MessageDispatcher dispatcher;

    dispatcher.registerHandler("PING", [](const protocol::Message&, int /*fd*/) {
        protocol::Message pong;
        pong.type = "PONG";
        pong.payload = {{"type", "PONG"}};

        protocol::Message targeted_error =
            protocol::make_error_message("SOMETHING", "sent to someone else");
        targeted_error.scope = protocol::Scope::TARGETED;
        targeted_error.target_fds = {7};

        return std::vector<protocol::Message>{pong, targeted_error};
    });

    protocol::Message message;
    message.type = "PING";
    message.payload = {{"type", "PING"}};

    const auto responses = dispatcher.dispatch(message, -1);

    REQUIRE(responses.size() == 2);
    REQUIRE_FALSE(responses[0].payload.contains("request_type"));
    REQUIRE_FALSE(responses[1].payload.contains("request_type"));
}
