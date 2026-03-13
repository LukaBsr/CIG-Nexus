#include <catch2/catch_test_macros.hpp>

#include "protocol/handlers/ChatHandler.hpp"
#include "protocol/Message.hpp"
#include "session/SessionManager.hpp"

TEST_CASE("ChatHandler accepts valid CHAT_MESSAGE") {
    protocol::ChatHandler handler;
    
    protocol::Message message;
    message.type = "CHAT_MESSAGE";
    message.payload = {
        {"type", "CHAT_MESSAGE"},
        {"content", "hello"}
    };
    
    const auto response = handler.handle(message, -1);
    
    REQUIRE(response.type == "CHAT_MESSAGE");
    REQUIRE(response.scope == protocol::Scope::BROADCAST);
    REQUIRE(response.payload["content"] == "hello");
    REQUIRE(response.payload["from"] == "anonymous");
}

TEST_CASE("ChatHandler returns NOT_IDENTIFIED when session manager is set but session is missing") {
    protocol::ChatHandler handler;
    session::SessionManager sessions;
    handler.setSessionManager(&sessions);

    protocol::Message message;
    message.type = "CHAT_MESSAGE";
    message.payload = {
        {"type", "CHAT_MESSAGE"},
        {"content", "hello"}
    };

    const auto response = handler.handle(message, 55);

    REQUIRE(response.type == "ERROR");
    REQUIRE(response.payload["code"] == "NOT_IDENTIFIED");
    REQUIRE(response.scope == protocol::Scope::DIRECT);
}

TEST_CASE("ChatHandler enriches message with user identity after IDENTIFY") {
    protocol::ChatHandler handler;
    session::SessionManager sessions;
    handler.setSessionManager(&sessions);

    auto& session = sessions.createSession(77);
    session.username = "alice";

    protocol::Message message;
    message.type = "CHAT_MESSAGE";
    message.payload = {
        {"type", "CHAT_MESSAGE"},
        {"content", "hello world"}
    };

    const auto response = handler.handle(message, 77);

    REQUIRE(response.type == "CHAT_MESSAGE");
    REQUIRE(response.scope == protocol::Scope::BROADCAST);
    REQUIRE(response.payload["user_id"] == session.user_id);
    REQUIRE(response.payload["username"] == "alice");
    REQUIRE(response.payload["content"] == "hello world");
}

TEST_CASE("ChatHandler rejects CHAT_MESSAGE with missing content") {
    protocol::ChatHandler handler;
    
    protocol::Message message;
    message.type = "CHAT_MESSAGE";
    message.payload = {
        {"type", "CHAT_MESSAGE"}
    };
    
    const auto response = handler.handle(message, -1);
    
    REQUIRE(response.type == "ERROR");
    REQUIRE(response.payload["code"] == "MALFORMED_MESSAGE");
}

TEST_CASE("ChatHandler rejects CHAT_MESSAGE with non-string content") {
    protocol::ChatHandler handler;
    
    protocol::Message message;
    message.type = "CHAT_MESSAGE";
    message.payload = {
        {"type", "CHAT_MESSAGE"},
        {"content", 12345}
    };
    
    const auto response = handler.handle(message, -1);
    
    REQUIRE(response.type == "ERROR");
    REQUIRE(response.payload["code"] == "MALFORMED_MESSAGE");
}

TEST_CASE("ChatHandler rejects CHAT_MESSAGE with empty content") {
    protocol::ChatHandler handler;
    
    protocol::Message message;
    message.type = "CHAT_MESSAGE";
    message.payload = {
        {"type", "CHAT_MESSAGE"},
        {"content", ""}
    };
    
    const auto response = handler.handle(message, -1);
    
    REQUIRE(response.type == "ERROR");
    REQUIRE(response.payload["code"] == "MALFORMED_MESSAGE");
}

TEST_CASE("ChatHandler rejects CHAT_MESSAGE with oversized content") {
    protocol::ChatHandler handler;
    
    protocol::Message message;
    message.type = "CHAT_MESSAGE";
    message.payload = {
        {"type", "CHAT_MESSAGE"},
        {"content", std::string(501, 'a')}
    };
    
    const auto response = handler.handle(message, -1);
    
    REQUIRE(response.type == "ERROR");
    REQUIRE(response.payload["code"] == "MALFORMED_MESSAGE");
}
