import { redis } from "./redis";

// design doc §9.1: per-IP sliding window, backed by Redis. Sliding-window-log
// via a sorted set (score = request timestamp) rather than a fixed-window
// counter — a fixed window lets a client burst up to 2x the limit right
// across a window boundary; a sliding log doesn't. Implemented as a single
// Lua script so the read-check-write sequence is atomic under concurrent
// requests from the same IP, which separate ioredis calls would not be.
const SLIDING_WINDOW_SCRIPT = `
local key = KEYS[1]
local now = tonumber(ARGV[1])
local window_ms = tonumber(ARGV[2])
local limit = tonumber(ARGV[3])
local member = ARGV[4]

redis.call("ZREMRANGEBYSCORE", key, "-inf", now - window_ms)
local count = redis.call("ZCARD", key)

if count >= limit then
  return 0
end

redis.call("ZADD", key, now, member)
redis.call("PEXPIRE", key, window_ms)
return 1
`;

export interface RateLimitOptions {
  windowMs: number;
  limit: number;
}

// bucket namespaces the limit (e.g. "oauth-login" vs "oauth-callback") so
// the two routes don't share one counter; identifier is normally the
// client IP.
export async function checkRateLimit(
  bucket: string,
  identifier: string,
  options: RateLimitOptions
): Promise<boolean> {
  const key = `ratelimit:${bucket}:${identifier}`;
  const now = Date.now();
  // Random suffix: two requests in the same millisecond must still count as
  // two distinct sorted-set members, not overwrite each other.
  const member = `${now}-${Math.random().toString(36).slice(2)}`;

  const result = await redis.eval(
    SLIDING_WINDOW_SCRIPT,
    1,
    key,
    now,
    options.windowMs,
    options.limit,
    member
  );

  return result === 1;
}
