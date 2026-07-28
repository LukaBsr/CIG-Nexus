import { describe, expect, it } from "vitest";

import { checkRateLimit } from "./rateLimit";
import { redis } from "./redis";

describe("checkRateLimit", () => {
  it("allows requests up to the limit and rejects the next one", async () => {
    const identifier = `test-${crypto.randomUUID()}`;
    const options = { windowMs: 60_000, limit: 3 };

    expect(await checkRateLimit("test-bucket", identifier, options)).toBe(true);
    expect(await checkRateLimit("test-bucket", identifier, options)).toBe(true);
    expect(await checkRateLimit("test-bucket", identifier, options)).toBe(true);
    expect(await checkRateLimit("test-bucket", identifier, options)).toBe(false);
  });

  it("keeps separate counts per identifier", async () => {
    const options = { windowMs: 60_000, limit: 1 };
    const a = `test-${crypto.randomUUID()}`;
    const b = `test-${crypto.randomUUID()}`;

    expect(await checkRateLimit("test-bucket", a, options)).toBe(true);
    expect(await checkRateLimit("test-bucket", a, options)).toBe(false);
    // b has never made a request, so it isn't affected by a's usage.
    expect(await checkRateLimit("test-bucket", b, options)).toBe(true);
  });

  it("keeps separate counts per bucket for the same identifier", async () => {
    const identifier = `test-${crypto.randomUUID()}`;
    const options = { windowMs: 60_000, limit: 1 };

    expect(await checkRateLimit("bucket-a", identifier, options)).toBe(true);
    expect(await checkRateLimit("bucket-a", identifier, options)).toBe(false);
    // Same identifier, different bucket — not affected by bucket-a's usage.
    expect(await checkRateLimit("bucket-b", identifier, options)).toBe(true);
  });

  it("allows requests again once the window has elapsed", async () => {
    const identifier = `test-${crypto.randomUUID()}`;
    const options = { windowMs: 200, limit: 1 };

    expect(await checkRateLimit("test-bucket", identifier, options)).toBe(true);
    expect(await checkRateLimit("test-bucket", identifier, options)).toBe(false);

    await new Promise((resolve) => setTimeout(resolve, 250));

    expect(await checkRateLimit("test-bucket", identifier, options)).toBe(true);
  });

  it("expires its Redis key so a quiet identifier doesn't leak memory forever", async () => {
    const identifier = `test-${crypto.randomUUID()}`;
    await checkRateLimit("test-bucket", identifier, { windowMs: 500, limit: 5 });

    const ttl = await redis.pttl(`ratelimit:test-bucket:${identifier}`);
    expect(ttl).toBeGreaterThan(0);
    expect(ttl).toBeLessThanOrEqual(500);
  });
});
