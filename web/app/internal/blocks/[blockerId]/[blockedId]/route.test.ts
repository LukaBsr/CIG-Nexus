import { sql } from "drizzle-orm";
import { NextRequest } from "next/server";
import { afterEach, beforeAll, describe, expect, it } from "vitest";

import { db } from "@/db/client";
import { userBlocks, users } from "@/db/schema";
import { toUserWireId } from "@/lib/internal/wireIds";

import { DELETE } from "./route";

const SECRET_HEADERS = { "x-internal-secret": "test-internal-secret" };

beforeAll(() => {
  process.env.INTERNAL_API_SHARED_SECRET = "test-internal-secret";
});

afterEach(async () => {
  await db.execute(sql`TRUNCATE TABLE ${userBlocks}, ${users} RESTART IDENTITY CASCADE`);
});

describe("DELETE /internal/blocks/:blockerId/:blockedId", () => {
  it("returns 401 without the shared secret", async () => {
    const request = new NextRequest("http://internal/x", { method: "DELETE" });
    const response = await DELETE(request, { params: Promise.resolve({ blockerId: "u_x", blockedId: "u_y" }) });
    expect(response.status).toBe(401);
  });

  it("removes an existing block", async () => {
    const [a] = await db.insert(users).values({ discordId: "1", discordUsername: "alice" }).returning();
    const [b] = await db.insert(users).values({ discordId: "2", discordUsername: "bob" }).returning();
    await db.insert(userBlocks).values({ blockerId: a.id, blockedId: b.id });

    const request = new NextRequest("http://internal/x", { method: "DELETE", headers: SECRET_HEADERS });
    const response = await DELETE(request, {
      params: Promise.resolve({ blockerId: toUserWireId(a.id), blockedId: toUserWireId(b.id) })
    });
    expect(response.status).toBe(200);
    expect(await db.select().from(userBlocks)).toHaveLength(0);
  });

  it("returns 404 when there's no such block", async () => {
    const request = new NextRequest("http://internal/x", { method: "DELETE", headers: SECRET_HEADERS });
    const response = await DELETE(request, {
      params: Promise.resolve({
        blockerId: "u_00000000-0000-0000-0000-000000000000",
        blockedId: "u_00000000-0000-0000-0000-000000000001"
      })
    });
    expect(response.status).toBe(404);
  });
});
