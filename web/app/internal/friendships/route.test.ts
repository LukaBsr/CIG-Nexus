import { sql } from "drizzle-orm";
import { NextRequest } from "next/server";
import { afterEach, beforeAll, describe, expect, it } from "vitest";

import { db } from "@/db/client";
import { friendRequests, friendships, users } from "@/db/schema";
import { toUserWireId } from "@/lib/internal/wireIds";

import { GET } from "./route";

const URL = "http://internal/internal/friendships";
const SECRET_HEADERS = { "x-internal-secret": "test-internal-secret" };

beforeAll(() => {
  process.env.INTERNAL_API_SHARED_SECRET = "test-internal-secret";
});

afterEach(async () => {
  await db.execute(sql`TRUNCATE TABLE ${friendRequests}, ${friendships}, ${users} RESTART IDENTITY CASCADE`);
});

describe("GET /internal/friendships", () => {
  it("returns 401 without the shared secret", async () => {
    const request = new NextRequest(`${URL}?user_id=u_x`);
    expect((await GET(request)).status).toBe(401);
  });

  it("returns 400 without user_id", async () => {
    const request = new NextRequest(URL, { headers: SECRET_HEADERS });
    expect((await GET(request)).status).toBe(400);
  });

  it("lists friends", async () => {
    const [a] = await db.insert(users).values({ discordId: "1", discordUsername: "alice" }).returning();
    const [b] = await db.insert(users).values({ discordId: "2", discordUsername: "bob" }).returning();
    const [lo, hi] = a.id < b.id ? [a.id, b.id] : [b.id, a.id];
    await db.insert(friendships).values({ userIdA: lo, userIdB: hi });

    const request = new NextRequest(`${URL}?user_id=${toUserWireId(a.id)}`, { headers: SECRET_HEADERS });
    const response = await GET(request);
    expect(response.status).toBe(200);
    const body = (await response.json()) as { friends: { user_id: string }[] };
    expect(body.friends).toEqual([
      { user_id: toUserWireId(b.id), username: "bob", display_name: "bob", avatar_url: null }
    ]);
  });
});
