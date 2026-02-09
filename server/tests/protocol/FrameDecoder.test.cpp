#include <catch2/catch_test_macros.hpp>

#include "FrameDecoder.hpp"

#include <cstdint>
#include <vector>

namespace {

std::vector<uint8_t> make_frame(const std::vector<uint8_t>& payload) {
    const std::uint32_t size = static_cast<std::uint32_t>(payload.size());
    std::vector<uint8_t> frame;
    frame.reserve(4 + payload.size());
    frame.push_back(static_cast<uint8_t>((size >> 24) & 0xFF));
    frame.push_back(static_cast<uint8_t>((size >> 16) & 0xFF));
    frame.push_back(static_cast<uint8_t>((size >> 8) & 0xFF));
    frame.push_back(static_cast<uint8_t>(size & 0xFF));
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

} // namespace

TEST_CASE("FrameDecoder extracts a single complete frame") {
    std::vector<uint8_t> buffer;
    const std::vector<uint8_t> payload = {'h', 'e', 'l', 'l', 'o'};
    const std::vector<uint8_t> frame = make_frame(payload);
    buffer.insert(buffer.end(), frame.begin(), frame.end());

    FrameDecoder decoder(buffer);
    const auto frames = decoder.extract_frames();

    REQUIRE(frames.size() == 1);
    REQUIRE(frames[0] == payload);
    REQUIRE(buffer.empty());
}

TEST_CASE("FrameDecoder extracts multiple frames from one buffer") {
    std::vector<uint8_t> buffer;
    const std::vector<uint8_t> payload_a = {'a', 'b'};
    const std::vector<uint8_t> payload_b = {'x', 'y', 'z'};

    const auto frame_a = make_frame(payload_a);
    const auto frame_b = make_frame(payload_b);

    buffer.insert(buffer.end(), frame_a.begin(), frame_a.end());
    buffer.insert(buffer.end(), frame_b.begin(), frame_b.end());

    FrameDecoder decoder(buffer);
    const auto frames = decoder.extract_frames();

    REQUIRE(frames.size() == 2);
    REQUIRE(frames[0] == payload_a);
    REQUIRE(frames[1] == payload_b);
    REQUIRE(buffer.empty());
}

TEST_CASE("FrameDecoder leaves partial frame in buffer") {
    std::vector<uint8_t> buffer;
    const std::vector<uint8_t> payload = {'p', 'a', 'r', 't', 'i', 'a', 'l'};
    const auto frame = make_frame(payload);

    buffer.insert(buffer.end(), frame.begin(), frame.begin() + 6);

    FrameDecoder decoder(buffer);
    const auto frames = decoder.extract_frames();

    REQUIRE(frames.empty());
    REQUIRE(buffer.size() == 6);
}

TEST_CASE("FrameDecoder rejects frame size 0") {
    std::vector<uint8_t> buffer = {0x00, 0x00, 0x00, 0x00};
    FrameDecoder decoder(buffer);

    REQUIRE_THROWS_AS(decoder.extract_frames(), std::runtime_error);
}

TEST_CASE("FrameDecoder rejects frame larger than MAX_FRAME_SIZE") {
    std::vector<uint8_t> buffer;
    const std::uint32_t size = static_cast<std::uint32_t>(FrameDecoder::MAX_FRAME_SIZE + 1);
    buffer.push_back(static_cast<uint8_t>((size >> 24) & 0xFF));
    buffer.push_back(static_cast<uint8_t>((size >> 16) & 0xFF));
    buffer.push_back(static_cast<uint8_t>((size >> 8) & 0xFF));
    buffer.push_back(static_cast<uint8_t>(size & 0xFF));

    FrameDecoder decoder(buffer);
    REQUIRE_THROWS_AS(decoder.extract_frames(), std::runtime_error);
}
