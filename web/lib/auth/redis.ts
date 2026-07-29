import Redis from "ioredis";

// Same globalThis-attach pattern as db/client.ts, for the same reason:
// Next.js dev-mode module reloading would otherwise open a fresh connection
// every hot reload.
declare global {
  var __cigNexusRedis: Redis | undefined;
}

// Deliberately reads process.env directly (no authEnv.redisUrl throw) for
// the same build-time reason as db/client.ts: this module's top level runs
// during `next build`'s page-data collection, before any real REDIS_URL
// exists. instrumentation.ts's register() is what actually enforces
// REDIS_URL is set, before the server starts serving requests.
//
// lazyConnect: unlike pg.Pool (db/client.ts), ioredis connects eagerly at
// construction by default — that fires during `next build`'s route
// analysis, when no Redis is actually running, spamming ECONNREFUSED to
// stderr. Deferring to the first real command matches the Pool's lazy
// behavior and keeps module import side-effect-free.
const redis =
  global.__cigNexusRedis ??
  (process.env.REDIS_URL
    ? new Redis(process.env.REDIS_URL, { lazyConnect: true })
    : new Redis({ lazyConnect: true }));

if (process.env.NODE_ENV !== "production") {
  global.__cigNexusRedis = redis;
}

export { redis };
