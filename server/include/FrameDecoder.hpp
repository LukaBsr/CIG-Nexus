#ifndef CIG_NEXUS_FRAME_DECODER_HPP
#define CIG_NEXUS_FRAME_DECODER_HPP

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

class FrameDecoder {
public:
    static constexpr std::size_t MAX_FRAME_SIZE = 1024 * 1024;

    explicit FrameDecoder(std::vector<uint8_t>& buffer)
        : buffer_(buffer) {}

    std::vector<std::vector<uint8_t>> extract_frames() {
        std::vector<std::vector<uint8_t>> frames;

        while (true) {
            if (buffer_.size() < kHeaderSize) {
                break;
            }

            const std::uint32_t size = read_size_be();
            if (size == 0 || size > MAX_FRAME_SIZE) {
                throw std::runtime_error("Invalid frame size");
            }

            const std::size_t total_size = kHeaderSize + size;
            if (buffer_.size() < total_size) {
                break;
            }

            std::vector<uint8_t> payload(buffer_.begin() + kHeaderSize,
                                         buffer_.begin() + total_size);
            frames.emplace_back(std::move(payload));
            // Note: buffer erase is O(n). Acceptable for v0, may be optimized later.
            buffer_.erase(buffer_.begin(), buffer_.begin() + total_size);
        }

        return frames;
    }

private:
    static constexpr std::size_t kHeaderSize = 4;

    std::uint32_t read_size_be() const {
        return (static_cast<std::uint32_t>(buffer_[0]) << 24) |
               (static_cast<std::uint32_t>(buffer_[1]) << 16) |
               (static_cast<std::uint32_t>(buffer_[2]) << 8) |
               (static_cast<std::uint32_t>(buffer_[3]));
    }

    std::vector<uint8_t>& buffer_;
};

#endif // CIG_NEXUS_FRAME_DECODER_HPP
