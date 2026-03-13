#include <catch2/catch_test_macros.hpp>

#include "protocol/handlers/IdentifyHandler.hpp"
#include "session/SessionManager.hpp"

namespace {

protocol::Message make_identify(const std::string& username) {
    protocol::Message message;
    message.type = "IDENTIFY";
    message.payload = {
        {"type", "IDENTIFY"},
        {"username", username}
    };
    return message;
}

} // namespace

TEST_CASE("IdentifyHandler creates session and returns IDENTIFIED") {
    protocol::IdentifyHandler handler;
    session::SessionManager sessions;
    handler.setSessionManager(&sessions);

    const auto response = handler.handle(make_identify("web_user"), 10);

    REQUIRE(response.type == "IDENTIFIED");
    REQUIRE(response.payload["username"] == "web_user");
    REQUIRE(response.payload.contains("user_id"));
    REQUIRE(sessions.hasSession(10));

    const auto* session = sessions.getSession(10);
    REQUIRE(session != nullptr);
    REQUIRE(session->username == "web_user");
}

TEST_CASE("IdentifyHandler rejects second IDENTIFY on same connection") {
    protocol::IdentifyHandler handler;
    session::SessionManager sessions;
    handler.setSessionManager(&sessions);

    const auto first = handler.handle(make_identify("alice"), 10);
    REQUIRE(first.type == "IDENTIFIED");

    const auto second = handler.handle(make_identify("bob"), 10);
    REQUIRE(second.type == "ERROR");
    REQUIRE(second.payload["code"] == "PROTOCOL_VIOLATION");
}

TEST_CASE("IdentifyHandler rejects missing username") {
    protocol::IdentifyHandler handler;
    session::SessionManager sessions;
    handler.setSessionManager(&sessions);

    protocol::Message message;
    message.type = "IDENTIFY";
    message.payload = {{"type", "IDENTIFY"}};

    const auto response = handler.handle(message, 12);

    REQUIRE(response.type == "ERROR");
    REQUIRE(response.payload["code"] == "MALFORMED_MESSAGE");
}

TEST_CASE("IdentifyHandler rejects oversized username") {
    protocol::IdentifyHandler handler;
    session::SessionManager sessions;
    handler.setSessionManager(&sessions);

    const auto response = handler.handle(make_identify(std::string(33, 'x')), 13);

    REQUIRE(response.type == "ERROR");
    REQUIRE(response.payload["code"] == "MALFORMED_MESSAGE");
}

TEST_CASE("IdentifyHandler returns INTERNAL_ERROR when session manager is missing") {
    protocol::IdentifyHandler handler;

    const auto response = handler.handle(make_identify("web_user"), 14);

    REQUIRE(response.type == "ERROR");
    REQUIRE(response.payload["code"] == "INTERNAL_ERROR");
}
