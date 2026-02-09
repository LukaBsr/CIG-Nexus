#include <catch2/catch_test_macros.hpp>

#include "protocol/MessageParser.hpp"

#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::vector<uint8_t> to_bytes(const std::string& text) {
    return std::vector<uint8_t>(text.begin(), text.end());
}

} // namespace

TEST_CASE("MessageParser parses valid JSON with type") {
    const auto bytes = to_bytes("{\"type\":\"HELLO\",\"version\":\"0.1\"}");
    const auto message = protocol::parse_message(bytes);

    REQUIRE(message.type == "HELLO");
    REQUIRE(message.payload.is_object());
}

TEST_CASE("MessageParser rejects invalid JSON payload") {
    const auto bytes = to_bytes("{invalid_json");
    REQUIRE_THROWS_AS(protocol::parse_message(bytes), std::runtime_error);
}

TEST_CASE("MessageParser rejects missing type field") {
    const auto bytes = to_bytes("{\"version\":\"0.1\"}");
    REQUIRE_THROWS_AS(protocol::parse_message(bytes), std::runtime_error);
}

TEST_CASE("MessageParser rejects non-object JSON") {
    const auto bytes = to_bytes("[1,2,3]");
    REQUIRE_THROWS_AS(protocol::parse_message(bytes), std::runtime_error);
}
