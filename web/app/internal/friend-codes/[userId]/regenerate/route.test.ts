import { sql } from "drizzle-orm";
import { NextRequest } from "next/server";
import { afterEach, beforeAll, describe, expect, it } from "vitest";

import { db } from "@/db/client";
import { users } from "@/db/schema";
import { toUserWireId } from "@/lib/internal/wireIds";

import { POST } from "./route";

const SECRET_HEADERS = { "x-internal-secret": "test-internal-secret" };

beforeAll(() => {
  process.env.INTERNAL_API_SHARED_SECRET = "test-internal-secret";
});

afterEach(async () => {
  await db.execute(sql`TRUNCATE TABLE ${users} RESTART IDENTITY CASCADE`);
});

describe("POST /internal/friend-codes/:userId/regenerate", () => {
  it("returns 401 without the shared secret", async () => {
    const request = new NextRequest("http://internal/x", { method: "POST" });
    const response = await POST(request, { params: Promise.resolve({ userId: "u_x" }) });
    expect(response.status).toBe(401);
  });

  it("replaces the stored code", async () => {
    const [a] = await db.insert(users).values({ discordId: "1", discordUsername: "alice", friendCode: "old-code" }).returning();

    const request = new NextRequest("http://internal/x", { method: "POST", headers: SECRET_HEADERS });
    const response = await POST(request, { params: Promise.resolve({ userId: toUserWireId(a.id) }) });
    expect(response.status).toBe(200);
    const body = (await response.json()) as { code: string };
    expect(body.code).not.toBe("old-code");

    const [row] = await db.select({ friendCode: users.friendCode }).from(users).where(sql`${users.id} = ${a.id}`);
    expect(row.friendCode).toBe(body.code);
  });
});
