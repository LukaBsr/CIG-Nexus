import { randomBytes } from "node:crypto";

import { NextRequest } from "next/server";
import { beforeAll, describe, expect, it } from "vitest";

import { GET } from "./route";

beforeAll(() => {
  process.env.DISCORD_CLIENT_ID = "test-client-id";
  process.env.DISCORD_REDIRECT_URI = "http://localhost:3000/api/auth/discord/callback";
  process.env.OAUTH_TXN_SECRET = randomBytes(32).toString("hex");
});

// A distinct IP per test keeps each test's requests in their own rate-limit
// bucket (lib/auth/rateLimit.ts) — otherwise every call in this file would
// share the "unknown" bucket and could spuriously trip the limit as more
// tests are added.
let ipCounter = 0;
function request(): NextRequest {
  ipCounter += 1;
  return new NextRequest("http://localhost:3000/api/auth/discord/login", {
    headers: { "x-forwarded-for": `10.0.0.${ipCounter}` }
  });
}

describe("GET /api/auth/discord/login", () => {
  it("redirects to Discord's authorize endpoint with PKCE + state params", async () => {
    const response = await GET(request());

    expect(response.status).toBe(307);
    const location = new URL(response.headers.get("location")!);
    expect(location.origin + location.pathname).toBe("https://discord.com/oauth2/authorize");
    expect(location.searchParams.get("client_id")).toBe("test-client-id");
    expect(location.searchParams.get("redirect_uri")).toBe(
      "http://localhost:3000/api/auth/discord/callback"
    );
    expect(location.searchParams.get("response_type")).toBe("code");
    expect(location.searchParams.get("scope")).toBe("identify");
    expect(location.searchParams.get("code_challenge_method")).toBe("S256");
    expect(location.searchParams.get("state")).toBeTruthy();
    expect(location.searchParams.get("code_challenge")).toBeTruthy();
  });

  it("sets an httpOnly oauth_txn cookie", async () => {
    const response = await GET(request());
    const setCookie = response.headers.get("set-cookie")!;
    expect(setCookie).toContain("oauth_txn=");
    expect(setCookie).toContain("HttpOnly");
    expect(setCookie).toContain("SameSite=lax");
  });

  it("uses a different state/verifier on each call", async () => {
    const req = request();
    const first = new URL((await GET(req)).headers.get("location")!);
    const second = new URL((await GET(req)).headers.get("location")!);
    expect(first.searchParams.get("state")).not.toBe(second.searchParams.get("state"));
  });

  it("returns 429 once the per-IP limit is exceeded", async () => {
    const req = request();
    for (let i = 0; i < 10; i += 1) {
      const response = await GET(req);
      expect(response.status).toBe(307);
    }
    const limited = await GET(req);
    expect(limited.status).toBe(429);
  });
});
