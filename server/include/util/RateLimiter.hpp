#ifndef CIG_NEXUS_UTIL_RATE_LIMITER_HPP
#define CIG_NEXUS_UTIL_RATE_LIMITER_HPP

#include <chrono>
#include <deque>
#include <functional>
#include <string>
#include <unordered_map>

namespace util {

// In-memory sliding-window-log limiter, one process, no persistence across
// restarts — the C++-side counterpart to web/lib/auth/rateLimit.ts's
// Redis-backed sliding window, for protocol actions that never touch the
// internal API's HTTP layer at all (or where paying a Redis round trip on
// every check would defeat the point, e.g. CHAT_MESSAGE). Single-threaded,
// like every other in-memory state this server holds (SessionManager,
// GuildManager) — no locking.
//
// A key with no calls within its own window naturally prunes down to an
// empty deque and then just sits there rather than being erased — bounded
// by the number of distinct (bucket, identifier) pairs actually used by
// real, authenticated callers, not attacker-controlled growth, so this
// isn't the same unbounded-growth concern an unauthenticated surface would
// have.
class RateLimiter {
  public:
    using Clock = std::function<std::chrono::steady_clock::time_point()>;

    // Default clock is the real one; tests inject a fake to control time
    // without sleeping.
    explicit RateLimiter(Clock clock = &std::chrono::steady_clock::now);

    // Returns true (and records this call) if fewer than `limit` calls for
    // this (bucket, identifier) pair have happened within the trailing
    // `window`; returns false (and does not record) otherwise.
    bool allow(const std::string& bucket, const std::string& identifier,
               std::chrono::milliseconds window, int limit);

  private:
    Clock clock_;
    std::unordered_map<std::string, std::deque<std::chrono::steady_clock::time_point>> events_;
};

} // namespace util

#endif // CIG_NEXUS_UTIL_RATE_LIMITER_HPP
