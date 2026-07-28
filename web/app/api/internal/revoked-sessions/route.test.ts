import { sql } from "drizzle-orm";
import { NextRequest } from "next/server";
import { afterEach, beforeAll, describe, expect, it } from "vitest";

import { db } from "@/db/client";
import { sessions, users } from "@/db/schema";

import { GET } from "./route";

const SECRET_HEADERS = { "x-internal-secret": "test-internal-secret" };

beforeAll(() => {
  process.env.INTERNAL_API_SHARED_SECRET = "test-internal-secret";
});

afterEach(async () => {
  await db.execute(sql`TRUNCATE TABLE ${sessions}, ${users} RESTART IDENTITY CASCADE`);
});

describe("GET /api/internal/revoked-sessions", () => {
  it("returns 401 without the shared secret", async () => {
    const response = await GET(new NextRequest("http://internal/api/internal/revoked-sessions"));
    expect(response.status).toBe(401);
  });

  it("returns sessions revoked since the given timestamp", async () => {
    const [user] = await db
      .insert(users)
      .values({ discordId: "1", discordUsername: "alice" })
      .returning();
    const since = new Date();
    const [revoked] = await db
      .insert(sessions)
      .values({
        userId: user.id,
        expiresAt: new Date(Date.now() + 60_000),
        refreshTokenHash: "hash",
        revokedAt: new Date(since.getTime() + 1000)
      })
      .returning();

    const request = new NextRequest(
      `http://internal/api/internal/revoked-sessions?since=${since.toISOString()}`,
      { headers: SECRET_HEADERS }
    );
    const response = await GET(request);
    expect(response.status).toBe(200);
    const body = (await response.json()) as { revoked_session_ids: string[] };
    expect(body.revoked_session_ids).toEqual([revoked.id]);
  });

  it("defaults to since=epoch when omitted", async () => {
    const request = new NextRequest("http://internal/api/internal/revoked-sessions", {
      headers: SECRET_HEADERS
    });
    const response = await GET(request);
    expect(response.status).toBe(200);
  });

  it("returns 400 for an invalid since param", async () => {
    const request = new NextRequest("http://internal/api/internal/revoked-sessions?since=not-a-date", {
      headers: SECRET_HEADERS
    });
    expect((await GET(request)).status).toBe(400);
  });
});
