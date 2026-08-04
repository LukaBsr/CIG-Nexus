import { sql } from "drizzle-orm";
import { NextRequest } from "next/server";
import { afterEach, beforeAll, describe, expect, it } from "vitest";

import { db } from "@/db/client";
import { friendRequests, friendships, userBlocks, users } from "@/db/schema";
import { toUserWireId } from "@/lib/internal/wireIds";

import { GET, POST } from "./route";

const URL = "http://internal/internal/blocks";
const SECRET_HEADERS = { "x-internal-secret": "test-internal-secret", "content-type": "application/json" };

beforeAll(() => {
  process.env.INTERNAL_API_SHARED_SECRET = "test-internal-secret";
});

afterEach(async () => {
  await db.execute(
    sql`TRUNCATE TABLE ${userBlocks}, ${friendRequests}, ${friendships}, ${users} RESTART IDENTITY CASCADE`
  );
});

async function insertUser(discordId: string) {
  const [user] = await db
    .insert(users)
    .values({ discordId, discordUsername: `user_${discordId}` })
    .returning();
  return user;
}

describe("POST /internal/blocks", () => {
  it("returns 401 without the shared secret", async () => {
    const request = new NextRequest(URL, { method: "POST", body: "{}" });
    expect((await POST(request)).status).toBe(401);
  });

  it("creates a block", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    const request = new NextRequest(URL, {
      method: "POST",
      headers: SECRET_HEADERS,
      body: JSON.stringify({ blocker_id: toUserWireId(a.id), blocked_id: toUserWireId(b.id) })
    });
    const response = await POST(request);
    expect(response.status).toBe(200);
    expect(await db.select().from(userBlocks)).toHaveLength(1);
  });

  it("returns 400 (not a 500) for a nonexistent blocked_id", async () => {
    const a = await insertUser("1");
    const request = new NextRequest(URL, {
      method: "POST",
      headers: SECRET_HEADERS,
      body: JSON.stringify({
        blocker_id: toUserWireId(a.id),
        blocked_id: "u_00000000-0000-0000-0000-000000000000"
      })
    });
    const response = await POST(request);
    expect(response.status).toBe(400);
  });
});

describe("GET /internal/blocks", () => {
  it("returns 401 without the shared secret", async () => {
    const request = new NextRequest(`${URL}?user_id=u_x`);
    expect((await GET(request)).status).toBe(401);
  });

  it("lists blocks", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    await db.insert(userBlocks).values({ blockerId: a.id, blockedId: b.id });

    const request = new NextRequest(`${URL}?user_id=${toUserWireId(a.id)}`, { headers: SECRET_HEADERS });
    const response = await GET(request);
    expect(response.status).toBe(200);
    const body = (await response.json()) as { blocks: { user_id: string }[] };
    expect(body.blocks).toEqual([{ user_id: toUserWireId(b.id), username: "user_2", blocked_at: expect.any(String) }]);
  });
});
