import { sql } from "drizzle-orm";
import { NextRequest } from "next/server";
import { afterEach, beforeAll, describe, expect, it } from "vitest";

import { db } from "@/db/client";
import { friendRequests, friendships, users } from "@/db/schema";
import { toUserWireId } from "@/lib/internal/wireIds";

import { GET, POST } from "./route";

const URL = "http://internal/internal/friend-requests";
const SECRET_HEADERS = { "x-internal-secret": "test-internal-secret", "content-type": "application/json" };

beforeAll(() => {
  process.env.INTERNAL_API_SHARED_SECRET = "test-internal-secret";
});

afterEach(async () => {
  await db.execute(sql`TRUNCATE TABLE ${friendRequests}, ${friendships}, ${users} RESTART IDENTITY CASCADE`);
});

async function insertUser(discordId: string) {
  const [user] = await db
    .insert(users)
    .values({ discordId, discordUsername: `user_${discordId}` })
    .returning();
  return user;
}

describe("POST /internal/friend-requests", () => {
  it("returns 401 without the shared secret", async () => {
    const request = new NextRequest(URL, { method: "POST", body: "{}" });
    expect((await POST(request)).status).toBe(401);
  });

  it("returns 400 when neither recipient_id nor code is given", async () => {
    const a = await insertUser("1");
    const request = new NextRequest(URL, {
      method: "POST",
      headers: SECRET_HEADERS,
      body: JSON.stringify({ requester_id: toUserWireId(a.id) })
    });
    expect((await POST(request)).status).toBe(400);
  });

  it("creates a request via recipient_id", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    const request = new NextRequest(URL, {
      method: "POST",
      headers: SECRET_HEADERS,
      body: JSON.stringify({ requester_id: toUserWireId(a.id), recipient_id: toUserWireId(b.id) })
    });
    const response = await POST(request);
    expect(response.status).toBe(200);
    const body = (await response.json()) as { ok: boolean; kind: string };
    expect(body).toEqual({ ok: true, kind: "request", user_id: toUserWireId(b.id), username: "user_2" });
  });

  it("creates a request via code", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    await db.update(users).set({ friendCode: "abc123" }).where(sql`${users.id} = ${b.id}`);

    const request = new NextRequest(URL, {
      method: "POST",
      headers: SECRET_HEADERS,
      body: JSON.stringify({ requester_id: toUserWireId(a.id), code: "abc123" })
    });
    const response = await POST(request);
    expect(response.status).toBe(200);
    const body = (await response.json()) as { ok: boolean };
    expect(body.ok).toBe(true);
  });
});

describe("GET /internal/friend-requests", () => {
  it("returns 401 without the shared secret", async () => {
    const request = new NextRequest(`${URL}?user_id=u_x`);
    expect((await GET(request)).status).toBe(401);
  });

  it("returns 400 without user_id", async () => {
    const request = new NextRequest(URL, { headers: SECRET_HEADERS });
    expect((await GET(request)).status).toBe(400);
  });

  it("lists incoming/outgoing requests", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    await db.insert(friendRequests).values({ requesterId: a.id, recipientId: b.id });

    const request = new NextRequest(`${URL}?user_id=${toUserWireId(a.id)}`, { headers: SECRET_HEADERS });
    const response = await GET(request);
    expect(response.status).toBe(200);
    const body = (await response.json()) as { incoming: unknown[]; outgoing: unknown[] };
    expect(body.outgoing).toHaveLength(1);
    expect(body.incoming).toHaveLength(0);
  });
});
