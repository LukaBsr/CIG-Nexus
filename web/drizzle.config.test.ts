import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

let originalDatabaseUrl: string | undefined;

beforeEach(() => {
  originalDatabaseUrl = process.env.DATABASE_URL;
  // Node/Vite cache a module (including one that threw while evaluating)
  // by specifier — without this, the second test's import would just
  // replay the first test's cached failure instead of re-evaluating.
  vi.resetModules();
});

afterEach(() => {
  if (originalDatabaseUrl === undefined) {
    delete process.env.DATABASE_URL;
  } else {
    process.env.DATABASE_URL = originalDatabaseUrl;
  }
});

describe("drizzle.config.ts", () => {
  it("throws a clear error instead of falling back to a placeholder connection string", async () => {
    delete process.env.DATABASE_URL;

    // Previously this fell back to a placeholder Postgres URL, which made
    // `drizzle-kit migrate` hang trying to reach a database that was never
    // going to exist, instead of failing immediately with a clear cause.
    await expect(import("./drizzle.config")).rejects.toThrow(
      "Missing required environment variable: DATABASE_URL"
    );
  });

  it("loads normally when DATABASE_URL is set", async () => {
    process.env.DATABASE_URL = "postgres://user:pass@localhost:5432/db";

    const config = await import("./drizzle.config");
    const dbCredentials = (config.default as { dbCredentials?: { url?: string } }).dbCredentials;
    expect(dbCredentials).toMatchObject({
      url: "postgres://user:pass@localhost:5432/db"
    });
  });
});
