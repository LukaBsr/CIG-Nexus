#include <catch2/catch_test_macros.hpp>

#include "protocol/handlers/ChatHandler.hpp"
#include "protocol/Message.hpp"

TEST_CASE("ChatHandler accepts valid CHAT_MESSAGE") {
    protocol::ChatHandler handler;
    
    protocol::Message message;
    message.type = "CHAT_MESSAGE";
    message.payload = {
        {"type", "CHAT_MESSAGE"},
        {"content", "hello"}
    };
    
    const auto response = handler.handle(message);
    
    REQUIRE(response.type == "CHAT_MESSAGE");
    REQUIRE(response.payload["content"] == "hello");
    REQUIRE(response.payload["from"] == "anonymous");
}

TEST_CASE("ChatHandler rejects CHAT_MESSAGE with missing content") {
    protocol::ChatHandler handler;
    
    protocol::Message message;
    message.type = "CHAT_MESSAGE";
    message.payload = {
        {"type", "CHAT_MESSAGE"}
    };
    
    const auto response = handler.handle(message);
    
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
    
    const auto response = handler.handle(message);
    
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
    
    const auto response = handler.handle(message);
    
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
    
    const auto response = handler.handle(message);
    
    REQUIRE(response.type == "ERROR");
    REQUIRE(response.payload["code"] == "MALFORMED_MESSAGE");
}
