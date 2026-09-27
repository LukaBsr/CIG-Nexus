#include <catch2/catch_test_macros.hpp>

#include "util/RateLimiter.hpp"

#include <chrono>

namespace {

// A controllable clock so tests don't sleep for real windows.
class FakeClock {
  public:
    std::chrono::steady_clock::time_point operator()() const { return now_; }
    void advance(std::chrono::milliseconds delta) { now_ += delta; }

  private:
    std::chrono::steady_clock::time_point now_{};
};

} // namespace

TEST_CASE("RateLimiter allows requests up to the limit and rejects the next one") {
    FakeClock clock;
    util::RateLimiter limiter([&clock] { return clock(); });

    REQUIRE(limiter.allow("bucket", "u_1", std::chrono::minutes(1), 3));
    REQUIRE(limiter.allow("bucket", "u_1", std::chrono::minutes(1), 3));
    REQUIRE(limiter.allow("bucket", "u_1", std::chrono::minutes(1), 3));
    REQUIRE_FALSE(limiter.allow("bucket", "u_1", std::chrono::minutes(1), 3));
}

TEST_CASE("RateLimiter keeps separate counts per identifier") {
    FakeClock clock;
    util::RateLimiter limiter([&clock] { return clock(); });

    REQUIRE(limiter.allow("bucket", "u_1", std::chrono::minutes(1), 1));
    REQUIRE_FALSE(limiter.allow("bucket", "u_1", std::chrono::minutes(1), 1));
    // u_2 has never made a request, so it isn't affected by u_1's usage.
    REQUIRE(limiter.allow("bucket", "u_2", std::chrono::minutes(1), 1));
}

TEST_CASE("RateLimiter keeps separate counts per bucket for the same identifier") {
    FakeClock clock;
    util::RateLimiter limiter([&clock] { return clock(); });

    REQUIRE(limiter.allow("bucket-a", "u_1", std::chrono::minutes(1), 1));
    REQUIRE_FALSE(limiter.allow("bucket-a", "u_1", std::chrono::minutes(1), 1));
    // Same identifier, different bucket — not affected by bucket-a's usage.
    REQUIRE(limiter.allow("bucket-b", "u_1", std::chrono::minutes(1), 1));
}

TEST_CASE("RateLimiter allows requests again once the window has elapsed") {
    FakeClock clock;
    util::RateLimiter limiter([&clock] { return clock(); });

    REQUIRE(limiter.allow("bucket", "u_1", std::chrono::milliseconds(200), 1));
    REQUIRE_FALSE(limiter.allow("bucket", "u_1", std::chrono::milliseconds(200), 1));

    clock.advance(std::chrono::milliseconds(250));

    REQUIRE(limiter.allow("bucket", "u_1", std::chrono::milliseconds(200), 1));
}

TEST_CASE("RateLimiter is a sliding window, not a fixed one — only expired calls drop off") {
    FakeClock clock;
    util::RateLimiter limiter([&clock] { return clock(); });

    REQUIRE(limiter.allow("bucket", "u_1", std::chrono::milliseconds(1000), 2)); // t=0
    clock.advance(std::chrono::milliseconds(600));
    REQUIRE(limiter.allow("bucket", "u_1", std::chrono::milliseconds(1000), 2)); // t=600
    REQUIRE_FALSE(limiter.allow("bucket", "u_1", std::chrono::milliseconds(1000),
                                2)); // t=600, still 2 in window

    clock.advance(std::chrono::milliseconds(500)); // t=1100: the t=0 call is now outside the window
    REQUIRE(limiter.allow("bucket", "u_1", std::chrono::milliseconds(1000), 2)); // t=1100
    REQUIRE_FALSE(limiter.allow("bucket", "u_1", std::chrono::milliseconds(1000),
                                2)); // t=600 and t=1100 both still in window
}
