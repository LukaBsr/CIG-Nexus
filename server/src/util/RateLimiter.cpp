#include "util/RateLimiter.hpp"

namespace util {

RateLimiter::RateLimiter(Clock clock) : clock_(std::move(clock)) {}

bool RateLimiter::allow(const std::string& bucket, const std::string& identifier,
                        std::chrono::milliseconds window, int limit) {
    auto& timestamps = events_[bucket + ":" + identifier];
    const auto now = clock_();

    while (!timestamps.empty() && now - timestamps.front() > window) {
        timestamps.pop_front();
    }

    if (static_cast<int>(timestamps.size()) >= limit) {
        return false;
    }

    timestamps.push_back(now);
    return true;
}

} // namespace util
