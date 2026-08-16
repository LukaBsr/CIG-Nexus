#include <catch2/catch_test_macros.hpp>

#include "persistence/MessagePersistenceWorker.hpp"
#include "protocol/Message.hpp"
#include "protocol/handlers/ChatHandler.hpp"
#include "session/SessionManager.hpp"

#include "../http/FakeInternalApiClient.hpp"

TEST_CASE("ChatHandler accepts valid CHAT_MESSAGE") {
    protocol::ChatHandler handler;

    protocol::Message message;
    message.type = "CHAT_MESSAGE";
    message.payload = {{"type", "CHAT_MESSAGE"}, {"content", "hello"}};

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
    message.payload = {{"type", "CHAT_MESSAGE"}, {"content", "hello"}};

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
    message.payload = {{"type", "CHAT_MESSAGE"}, {"content", "hello world"}};

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
    message.payload = {{"type", "CHAT_MESSAGE"}};

    const auto response = handler.handle(message, -1);

    REQUIRE(response.type == "ERROR");
    REQUIRE(response.payload["code"] == "MALFORMED_MESSAGE");
}

TEST_CASE("ChatHandler rejects CHAT_MESSAGE with non-string content") {
    protocol::ChatHandler handler;

    protocol::Message message;
    message.type = "CHAT_MESSAGE";
    message.payload = {{"type", "CHAT_MESSAGE"}, {"content", 12345}};

    const auto response = handler.handle(message, -1);

    REQUIRE(response.type == "ERROR");
    REQUIRE(response.payload["code"] == "MALFORMED_MESSAGE");
}

TEST_CASE("ChatHandler rejects CHAT_MESSAGE with empty content") {
    protocol::ChatHandler handler;

    protocol::Message message;
    message.type = "CHAT_MESSAGE";
    message.payload = {{"type", "CHAT_MESSAGE"}, {"content", ""}};

    const auto response = handler.handle(message, -1);

    REQUIRE(response.type == "ERROR");
    REQUIRE(response.payload["code"] == "MALFORMED_MESSAGE");
}

TEST_CASE("ChatHandler message_id increments across calls") {
    protocol::ChatHandler handler;

    protocol::Message message;
    message.type = "CHAT_MESSAGE";
    message.payload = {{"type", "CHAT_MESSAGE"}, {"content", "a"}};

    const auto r1 = handler.handle(message, -1);
    const auto r2 = handler.handle(message, -1);
    const auto r3 = handler.handle(message, -1);

    const int id1 = r1.payload["message_id"].get<int>();
    const int id2 = r2.payload["message_id"].get<int>();
    const int id3 = r3.payload["message_id"].get<int>();

    REQUIRE(id2 == id1 + 1);
    REQUIRE(id3 == id2 + 1);
}

TEST_CASE("ChatHandler rejects CHAT_MESSAGE with oversized content") {
    protocol::ChatHandler handler;

    protocol::Message message;
    message.type = "CHAT_MESSAGE";
    message.payload = {{"type", "CHAT_MESSAGE"}, {"content", std::string(501, 'a')}};

    const auto response = handler.handle(message, -1);

    REQUIRE(response.type == "ERROR");
    REQUIRE(response.payload["code"] == "MALFORMED_MESSAGE");
}

TEST_CASE(
    "ChatHandler enqueues a persisted message with std::nullopt channel_id on successful send") {
    test_helpers::FakeInternalApiClient api;
    persistence::MessagePersistenceWorker worker(&api);
    worker.start();

    protocol::ChatHandler handler;
    session::SessionManager sessions;
    handler.setSessionManager(&sessions);
    handler.setMessagePersistenceWorker(&worker);

    auto& session = sessions.createSession(77);
    session.username = "alice";
    session.user_id = "u_77";

    protocol::Message message;
    message.type = "CHAT_MESSAGE";
    message.payload = {{"type", "CHAT_MESSAGE"}, {"content", "hello world"}};

    const auto response = handler.handle(message, 77);
    worker.stop(); // drains the queue before returning, so this establishes a happens-before

    REQUIRE(api.created_messages.size() == 1);
    REQUIRE_FALSE(api.created_messages[0].channel_id.has_value());
    REQUIRE(api.created_messages[0].user_id == "u_77");
    REQUIRE(api.created_messages[0].content == "hello world");
    REQUIRE(api.created_messages[0].message_id == response.payload["message_id"].get<int>());
}

TEST_CASE("ChatHandler does not enqueue when no persistence worker is set") {
    protocol::ChatHandler handler;
    session::SessionManager sessions;
    handler.setSessionManager(&sessions);

    auto& session = sessions.createSession(77);
    session.username = "alice";

    protocol::Message message;
    message.type = "CHAT_MESSAGE";
    message.payload = {{"type", "CHAT_MESSAGE"}, {"content", "hello"}};

    const auto response = handler.handle(message, 77);

    REQUIRE(response.type == "CHAT_MESSAGE"); // no crash without a worker
}

TEST_CASE("ChatHandler seedMessageCounter seeds the next message_id") {
    protocol::ChatHandler handler;
    handler.seedMessageCounter(41);

    protocol::Message message;
    message.type = "CHAT_MESSAGE";
    message.payload = {{"type", "CHAT_MESSAGE"}, {"content", "hello"}};

    const auto response = handler.handle(message, -1);

    REQUIRE(response.payload["message_id"].get<int>() == 42);
}

TEST_CASE("ChatHandler seedMessageCounter is a no-op for std::nullopt") {
    protocol::ChatHandler handler;
    handler.seedMessageCounter(std::nullopt);

    protocol::Message message;
    message.type = "CHAT_MESSAGE";
    message.payload = {{"type", "CHAT_MESSAGE"}, {"content", "hello"}};

    const auto response = handler.handle(message, -1);

    REQUIRE(response.payload["message_id"].get<int>() == 1);
}
