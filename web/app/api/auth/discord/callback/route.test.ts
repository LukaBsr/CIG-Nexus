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

  const headers = new Headers();
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
});
