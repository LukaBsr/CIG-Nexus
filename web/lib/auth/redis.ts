import Redis from "ioredis";

// Same globalThis-attach pattern as db/client.ts, for the same reason:
// Next.js dev-mode module reloading would otherwise open a fresh connection
// every hot reload.
declare global {
  var __cigNexusRedis: Redis | undefined;
}

// lazyConnect: unlike pg.Pool (db/client.ts), ioredis connects eagerly at
// construction by default — that fires during `next build`'s route
// analysis, when no Redis is actually running, spamming ECONNREFUSED to
// stderr. Deferring to the first real command matches the Pool's lazy
// behavior and keeps module import side-effect-free.
const redis =
  global.__cigNexusRedis ??
  new Redis(process.env.REDIS_URL ?? "redis://localhost:6379", { lazyConnect: true });

if (process.env.NODE_ENV !== "production") {
  global.__cigNexusRedis = redis;
}

export { redis };
