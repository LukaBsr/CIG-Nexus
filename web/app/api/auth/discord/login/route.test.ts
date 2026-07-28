import { randomBytes } from "node:crypto";

import { beforeAll, describe, expect, it } from "vitest";

import { GET } from "./route";

beforeAll(() => {
  process.env.DISCORD_CLIENT_ID = "test-client-id";
  process.env.DISCORD_REDIRECT_URI = "http://localhost:3000/api/auth/discord/callback";
  process.env.OAUTH_TXN_SECRET = randomBytes(32).toString("hex");
});

describe("GET /api/auth/discord/login", () => {
  it("redirects to Discord's authorize endpoint with PKCE + state params", async () => {
    const response = await GET();

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
    const response = await GET();
    const setCookie = response.headers.get("set-cookie")!;
    expect(setCookie).toContain("oauth_txn=");
    expect(setCookie).toContain("HttpOnly");
    expect(setCookie).toContain("SameSite=lax");
  });

  it("uses a different state/verifier on each call", async () => {
    const first = new URL((await GET()).headers.get("location")!);
    const second = new URL((await GET()).headers.get("location")!);
    expect(first.searchParams.get("state")).not.toBe(second.searchParams.get("state"));
  });
});
