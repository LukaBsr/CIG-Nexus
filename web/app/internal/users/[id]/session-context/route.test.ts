import { sql } from "drizzle-orm";
import { NextRequest } from "next/server";
import { afterEach, beforeAll, describe, expect, it } from "vitest";

import { db } from "@/db/client";
import { friendships, userBlocks, users } from "@/db/schema";
import { toUserWireId } from "@/lib/internal/wireIds";

import { GET } from "./route";

const SECRET_HEADERS = { "x-internal-secret": "test-internal-secret" };

beforeAll(() => {
  process.env.INTERNAL_API_SHARED_SECRET = "test-internal-secret";
});

afterEach(async () => {
  await db.execute(
    sql`TRUNCATE TABLE ${friendships}, ${userBlocks}, ${users} RESTART IDENTITY CASCADE`
  );
});

function orderedPair(a: string, b: string): [string, string] {
  return a < b ? [a, b] : [b, a];
}

describe("GET /internal/users/:id/session-context", () => {
  it("returns 401 without the shared secret", async () => {
    const request = new NextRequest("http://internal/x");
    const response = await GET(request, { params: Promise.resolve({ id: "u_x" }) });
    expect(response.status).toBe(401);
  });

  it("returns 404 for a nonexistent user", async () => {
    const request = new NextRequest("http://internal/x", { headers: SECRET_HEADERS });
    const response = await GET(request, {
      params: Promise.resolve({ id: "u_00000000-0000-0000-0000-000000000000" })
    });
    expect(response.status).toBe(404);
  });

  it("returns 404 for a malformed user id", async () => {
    const request = new NextRequest("http://internal/x", { headers: SECRET_HEADERS });
    const response = await GET(request, { params: Promise.resolve({ id: "not-a-wire-id" }) });
    expect(response.status).toBe(404);
  });

  it("combines profile, blocks, and friends for a user with none of either", async () => {
    const [alice] = await db.insert(users).values({ discordId: "1", discordUsername: "alice" }).returning();

    const request = new NextRequest("http://internal/x", { headers: SECRET_HEADERS });
    const response = await GET(request, { params: Promise.resolve({ id: toUserWireId(alice.id) }) });

    expect(response.status).toBe(200);
    expect(await response.json()).toEqual({
      user_id: toUserWireId(alice.id),
      username: "alice",
      display_name: "alice",
      avatar_url: null,
      blocks: [],
      friends: []
    });
  });

  it("combines a customized profile with real blocks and friends", async () => {
    const [alice] = await db
      .insert(users)
      .values({ discordId: "1", discordUsername: "alice", displayName: "Al", customAvatarPath: "/uploads/avatars/a.png" })
      .returning();
    const [bob] = await db.insert(users).values({ discordId: "2", discordUsername: "bob" }).returning();
    const [carol] = await db.insert(users).values({ discordId: "3", discordUsername: "carol" }).returning();

    await db.insert(userBlocks).values({ blockerId: alice.id, blockedId: carol.id });
    const [userIdA, userIdB] = orderedPair(alice.id, bob.id);
    await db.insert(friendships).values({ userIdA, userIdB });

    const request = new NextRequest("http://internal/x", { headers: SECRET_HEADERS });
    const response = await GET(request, { params: Promise.resolve({ id: toUserWireId(alice.id) }) });

    expect(response.status).toBe(200);
    const body = (await response.json()) as {
      user_id: string;
      username: string;
      display_name: string;
      avatar_url: string | null;
      blocks: Array<{ user_id: string }>;
      friends: Array<{ user_id: string }>;
    };
    expect(body.user_id).toBe(toUserWireId(alice.id));
    expect(body.display_name).toBe("Al");
    expect(body.avatar_url).toBe("/uploads/avatars/a.png");
    expect(body.blocks.map((b) => b.user_id)).toEqual([toUserWireId(carol.id)]);
    expect(body.friends.map((f) => f.user_id)).toEqual([toUserWireId(bob.id)]);
  });
});
