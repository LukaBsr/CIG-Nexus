#include <catch2/catch_test_macros.hpp>

#include "protocol/handlers/HelloHandler.hpp"

#include <string>

namespace {

protocol::Message make_hello(const std::string& version, const std::string& client) {
    protocol::Message message;
    message.type = "HELLO";
    message.payload = {
        {"type", "HELLO"},
        {"version", version},
        {"client", client}
    };
    return message;
}

} // namespace

TEST_CASE("HelloHandler returns WELCOME for valid HELLO") {
    protocol::HelloHandler handler;
    const auto message = make_hello("0.1", "web");

    const auto response = handler.handle(message);

    REQUIRE(response.type == "WELCOME");
    REQUIRE(response.payload["type"] == "WELCOME");
    REQUIRE(response.payload["server_version"] == "0.1");
}

TEST_CASE("HelloHandler rejects unsupported protocol version") {
    protocol::HelloHandler handler;
    const auto message = make_hello("0.2", "web");

    const auto response = handler.handle(message);

    REQUIRE(response.type == "ERROR");
    REQUIRE(response.payload["code"] == "UNSUPPORTED_VERSION");
}

TEST_CASE("HelloHandler rejects invalid client type") {
    protocol::HelloHandler handler;
    const auto message = make_hello("0.1", "bot");

    const auto response = handler.handle(message);

    REQUIRE(response.type == "ERROR");
}

TEST_CASE("HelloHandler rejects missing fields") {
    protocol::HelloHandler handler;
    protocol::Message message;
    message.type = "HELLO";
    message.payload = {
        {"type", "HELLO"},
        {"version", "0.1"}
    };

    const auto response = handler.handle(message);

    REQUIRE(response.type == "ERROR");
}
