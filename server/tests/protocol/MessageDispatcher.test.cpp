#include <catch2/catch_test_macros.hpp>

#include "protocol/MessageDispatcher.hpp"

TEST_CASE("MessageDispatcher dispatches to registered handler") {
    protocol::MessageDispatcher dispatcher;

    dispatcher.registerHandler("HELLO", [](const protocol::Message& message, int /*fd*/) {
        protocol::Message response;
        response.type = "WELCOME";
        response.payload = message.payload;
        return response;
    });

    protocol::Message message;
    message.type = "HELLO";
    message.payload = { {"type", "HELLO"} };

    const auto response = dispatcher.dispatch(message, -1);

    REQUIRE(response.type == "WELCOME");
}

TEST_CASE("MessageDispatcher throws on unknown message type") {
    protocol::MessageDispatcher dispatcher;

    protocol::Message message;
    message.type = "UNKNOWN";
    message.payload = { {"type", "UNKNOWN"} };

    REQUIRE_THROWS(dispatcher.dispatch(message, -1));
}

TEST_CASE("MessageDispatcher returns handler response") {
    protocol::MessageDispatcher dispatcher;

    dispatcher.registerHandler("PING", [](const protocol::Message& message, int /*fd*/) {
        protocol::Message response;
        response.type = "PONG";
        response.payload = message.payload;
        return response;
    });

    protocol::Message message;
    message.type = "PING";
    message.payload = { {"type", "PING"} };

    const auto response = dispatcher.dispatch(message, -1);

    REQUIRE(response.type == "PONG");
}
