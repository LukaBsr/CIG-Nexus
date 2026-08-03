import { randomBytes } from "node:crypto";

import { sql } from "drizzle-orm";
import { NextRequest } from "next/server";
import { afterEach, beforeAll, describe, expect, it, vi } from "vitest";

import { db } from "@/db/client";
import { sessions, users } from "@/db/schema";
import { signOAuthTxn } from "@/lib/auth/oauthTxnCookie";

import { GET } from "./route";

const CALLBACK_URL = "http://localhost:3000/api/auth/discord/callback";

beforeAll(() => {
  process.env.DISCORD_CLIENT_ID = "test-client-id";
  process.env.DISCORD_CLIENT_SECRET = "test-client-secret";
  process.env.DISCORD_REDIRECT_URI = CALLBACK_URL;
  process.env.OAUTH_TXN_SECRET = randomBytes(32).toString("hex");
});

afterEach(async () => {
  await db.execute(sql`TRUNCATE TABLE ${sessions}, ${users} RESTART IDENTITY CASCADE`);
  vi.restoreAllMocks();
});

function mockDiscordApi(discordUserId: string) {
  const fetchSpy = vi.spyOn(globalThis, "fetch");
  fetchSpy.mockResolvedValueOnce(
    new Response(
      JSON.stringify({
        access_token: "discord-access-token",
        refresh_token: "discord-refresh-token",
        expires_in: 604800,
        scope: "identify",
        token_type: "Bearer"
      }),
      { status: 200 }
    )
  );
  fetchSpy.mockResolvedValueOnce(
    new Response(
      JSON.stringify({ id: discordUserId, username: "alice", global_name: "Alice", avatar: "hash" }),
      { status: 200 }
    )
  );
  return fetchSpy;
}

// A distinct IP per call keeps every test in its own rate-limit bucket
// (lib/auth/rateLimit.ts) instead of all sharing "unknown".
let ipCounter = 0;

async function requestWithTxn(params: {
  code?: string;
  state?: string;
  txnState?: string;
  omitTxnCookie?: boolean;
}) {
  const txnState = params.txnState ?? "matching-state";
  const url = new URL(CALLBACK_URL);
  if (params.code !== undefined) url.searchParams.set("code", params.code);
  if (params.state !== undefined) url.searchParams.set("state", params.state);

  ipCounter += 1;
  const headers = new Headers({ "x-cig-nexus-remote-addr": `10.0.1.${ipCounter}` });
  if (!params.omitTxnCookie) {
    const txn = await signOAuthTxn({ codeVerifier: "verifier-abc", state: txnState });
    headers.set("cookie", `oauth_txn=${txn}`);
  }

  return new NextRequest(url, { headers });
}

describe("GET /api/auth/discord/callback", () => {
  it("redirects to the app and sets a refresh cookie on success", async () => {
    mockDiscordApi("42");
    const request = await requestWithTxn({ code: "auth-code", state: "matching-state" });

    const response = await GET(request);

    expect(response.status).toBe(307);
    expect(new URL(response.headers.get("location")!).pathname).toBe("/");
    const setCookie = response.headers.get("set-cookie")!;
    expect(setCookie).toContain("__session=");
    expect(setCookie).toContain("HttpOnly");

    const [user] = await db.select().from(users).where(sql`${users.discordId} = '42'`);
    expect(user).toBeDefined();
    expect(user.discordUsername).toBe("alice");

    const userSessions = await db.select().from(sessions).where(sql`${sessions.userId} = ${user.id}`);
    expect(userSessions).toHaveLength(1);
  });

  // docs/settings-appearance-design.md §3.6: pull-on-login.
  it("sets theme_sync=0 and does not touch the theme cookie for a sync-off account", async () => {
    mockDiscordApi("50");
    const request = await requestWithTxn({ code: "auth-code", state: "matching-state" });

    const response = await GET(request);
    const cookies = response.headers.getSetCookie();

    expect(cookies.some((c) => c.startsWith("theme_sync=0"))).toBe(true);
    expect(cookies.some((c) => c.startsWith("theme="))).toBe(false);
  });

  it("pulls the account's theme and sets theme_sync=1 for a sync-on account", async () => {
    await db
      .insert(users)
      .values({ discordId: "51", discordUsername: "prior", theme: "ember", themeSyncEnabled: true });
    mockDiscordApi("51");
    const request = await requestWithTxn({ code: "auth-code", state: "matching-state" });

    const response = await GET(request);
    const cookies = response.headers.getSetCookie();

    expect(cookies.some((c) => c.startsWith("theme_sync=1"))).toBe(true);
    expect(cookies.some((c) => c.startsWith("theme=ember"))).toBe(true);
  });

  it("resolves a sync-on account with no theme yet set to the default, not a literal null", async () => {
    await db
      .insert(users)
      .values({ discordId: "52", discordUsername: "prior", themeSyncEnabled: true });
    mockDiscordApi("52");
    const request = await requestWithTxn({ code: "auth-code", state: "matching-state" });

    const response = await GET(request);
    const cookies = response.headers.getSetCookie();

    expect(cookies.some((c) => c.startsWith("theme=abyss"))).toBe(true);
  });

  it("corrects a stale local theme_sync=1 to 0 when the account has since disabled sync", async () => {
    // Simulates: sync was on, this device's local cookie still says "1",
    // but a *different* device turned sync off in between. theme_sync must
    // be corrected even though theme itself isn't touched in this branch.
    await db
      .insert(users)
      .values({ discordId: "53", discordUsername: "prior", theme: "ember", themeSyncEnabled: false });
    mockDiscordApi("53");
    const request = await requestWithTxn({ code: "auth-code", state: "matching-state" });

    const response = await GET(request);
    const cookies = response.headers.getSetCookie();

    expect(cookies.some((c) => c.startsWith("theme_sync=0"))).toBe(true);
    expect(cookies.some((c) => c.startsWith("theme="))).toBe(false);
  });

  it("never persists the Discord access/refresh token (design doc §5)", async () => {
    mockDiscordApi("43");
    const request = await requestWithTxn({ code: "auth-code", state: "matching-state" });
    await GET(request);

    const columns = await db.execute(
      sql`SELECT column_name FROM information_schema.columns WHERE table_name = 'users'`
    );
    const columnNames = columns.rows.map((row) => (row as { column_name: string }).column_name);
    expect(columnNames).not.toContain("discord_access_token_enc");
    expect(columnNames).not.toContain("discord_refresh_token_enc");
  });

  it("redirects to the error page when code is missing", async () => {
    const request = await requestWithTxn({ state: "matching-state" });
    const response = await GET(request);
    expect(new URL(response.headers.get("location")!).pathname).toBe("/login-error");
  });

  it("redirects to the error page when the oauth_txn cookie is missing", async () => {
    const request = await requestWithTxn({ code: "auth-code", state: "matching-state", omitTxnCookie: true });
    const response = await GET(request);
    expect(new URL(response.headers.get("location")!).pathname).toBe("/login-error");
  });

  it("redirects to the error page on state mismatch (CSRF defense)", async () => {
    const request = await requestWithTxn({ code: "auth-code", state: "attacker-state", txnState: "real-state" });
    const response = await GET(request);
    expect(new URL(response.headers.get("location")!).pathname).toBe("/login-error");
  });

  it("redirects to the error page when Discord's token exchange fails", async () => {
    vi.spyOn(globalThis, "fetch").mockResolvedValueOnce(new Response("bad request", { status: 400 }));
    const request = await requestWithTxn({ code: "auth-code", state: "matching-state" });
    const response = await GET(request);
    expect(new URL(response.headers.get("location")!).pathname).toBe("/login-error");
  });

  it("returns 429 once the per-IP limit is exceeded", async () => {
    const url = new URL(CALLBACK_URL);
    const headers = new Headers({ "x-cig-nexus-remote-addr": "10.0.2.1" });
    const request = new NextRequest(url, { headers });

    for (let i = 0; i < 10; i += 1) {
      const response = await GET(request);
      expect(response.status).not.toBe(429);
    }
    const limited = await GET(request);
    expect(limited.status).toBe(429);
  });
});
