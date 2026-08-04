import { sql } from "drizzle-orm";
import { NextRequest } from "next/server";
import { afterEach, beforeAll, describe, expect, it } from "vitest";

import { db } from "@/db/client";
import { friendRequests, friendships, users } from "@/db/schema";
import { toUserWireId } from "@/lib/internal/wireIds";

import { DELETE } from "./route";

const SECRET_HEADERS = { "x-internal-secret": "test-internal-secret" };

beforeAll(() => {
  process.env.INTERNAL_API_SHARED_SECRET = "test-internal-secret";
});

afterEach(async () => {
  await db.execute(sql`TRUNCATE TABLE ${friendRequests}, ${friendships}, ${users} RESTART IDENTITY CASCADE`);
});

describe("DELETE /internal/friend-requests/:requesterId/:recipientId", () => {
  it("returns 401 without the shared secret", async () => {
    const request = new NextRequest("http://internal/x", { method: "DELETE" });
    const response = await DELETE(request, { params: Promise.resolve({ requesterId: "u_x", recipientId: "u_y" }) });
    expect(response.status).toBe(401);
  });

  it("deletes an existing pending request (reject or cancel — same operation)", async () => {
    const [a] = await db.insert(users).values({ discordId: "1", discordUsername: "alice" }).returning();
    const [b] = await db.insert(users).values({ discordId: "2", discordUsername: "bob" }).returning();
    await db.insert(friendRequests).values({ requesterId: a.id, recipientId: b.id });

    const request = new NextRequest("http://internal/x", { method: "DELETE", headers: SECRET_HEADERS });
    const response = await DELETE(request, {
      params: Promise.resolve({ requesterId: toUserWireId(a.id), recipientId: toUserWireId(b.id) })
    });
    expect(response.status).toBe(200);
    expect(await db.select().from(friendRequests)).toHaveLength(0);
  });

  it("returns 404 when no such pending request exists", async () => {
    const request = new NextRequest("http://internal/x", { method: "DELETE", headers: SECRET_HEADERS });
    const response = await DELETE(request, {
      params: Promise.resolve({
        requesterId: "u_00000000-0000-0000-0000-000000000000",
        recipientId: "u_00000000-0000-0000-0000-000000000001"
      })
    });
    expect(response.status).toBe(404);
  });
});
