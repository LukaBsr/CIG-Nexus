import { sql } from "drizzle-orm";
import { NextRequest } from "next/server";
import { afterEach, beforeAll, describe, expect, it } from "vitest";

import { db } from "@/db/client";
import { dmConversations, messages, users } from "@/db/schema";
import { resolveOrCreateDmConversationId } from "@/lib/internal/dmConversations";
import { toUserWireId } from "@/lib/internal/wireIds";

import { GET } from "./route";

const URL = "http://internal/internal/dm-conversations";
const SECRET_HEADERS = { "x-internal-secret": "test-internal-secret" };

beforeAll(() => {
  process.env.INTERNAL_API_SHARED_SECRET = "test-internal-secret";
});

afterEach(async () => {
  await db.execute(sql`TRUNCATE TABLE ${messages}, ${dmConversations}, ${users} RESTART IDENTITY CASCADE`);
});

async function insertUser(discordId: string) {
  const [user] = await db
    .insert(users)
    .values({ discordId, discordUsername: `user_${discordId}` })
    .returning();
  return user;
}

describe("GET /internal/dm-conversations", () => {
  it("returns 401 without the shared secret", async () => {
    const request = new NextRequest(`${URL}?user_id=u_x`);
    expect((await GET(request)).status).toBe(401);
  });

  it("returns 400 without user_id", async () => {
    const request = new NextRequest(URL, { headers: SECRET_HEADERS });
    expect((await GET(request)).status).toBe(400);
  });

  it("lists a user's conversations", async () => {
    const a = await insertUser("1");
    const b = await insertUser("2");
    await resolveOrCreateDmConversationId(a.id, b.id);

    const request = new NextRequest(`${URL}?user_id=${toUserWireId(a.id)}`, { headers: SECRET_HEADERS });
    const response = await GET(request);
    expect(response.status).toBe(200);
    const body = (await response.json()) as { conversations: { peer_id: string }[] };
    expect(body.conversations).toEqual([{ peer_id: toUserWireId(b.id), last_message_at: null }]);
  });
});
